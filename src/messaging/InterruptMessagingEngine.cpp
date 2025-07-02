#include "InterruptMessagingEngine.h"
#include "MessageAPI.h"
#include "MessageConfig.h"
#include "MessageData.h"
#include "../include/MessagingConfig.h"
#include <esp_log.h>
#include <driver/gpio.h>

static const char* TAG = "Core1::MessagingEngine";

namespace Messaging {
namespace Core1 {

// =============================================================================
// STATIC MEMBER DEFINITIONS
// =============================================================================

// Core state
bool InterruptMessagingEngine::initialized = false;
bool InterruptMessagingEngine::running = false;
TaskHandle_t InterruptMessagingEngine::messagingTaskHandle = nullptr;
MessageCore* InterruptMessagingEngine::messageCore = nullptr;

// Queues and synchronization
QueueHandle_t InterruptMessagingEngine::incomingDataQueue = nullptr;
QueueHandle_t InterruptMessagingEngine::outgoingMessageQueue = nullptr;
QueueHandle_t InterruptMessagingEngine::core1ProcessingQueue = nullptr;
QueueHandle_t InterruptMessagingEngine::core0NotificationQueue = nullptr;
SemaphoreHandle_t InterruptMessagingEngine::uartMutex = nullptr;
SemaphoreHandle_t InterruptMessagingEngine::routingMutex = nullptr;

// Message buffers
char InterruptMessagingEngine::rxBuffer[UART_RX_BUFFER_SIZE];
size_t InterruptMessagingEngine::rxBufferPos = 0;
char InterruptMessagingEngine::txBuffer[UART_TX_BUFFER_SIZE];

// Statistics
uint32_t InterruptMessagingEngine::messagesReceived = 0;
uint32_t InterruptMessagingEngine::messagesSent = 0;
uint32_t InterruptMessagingEngine::bufferOverruns = 0;
uint32_t InterruptMessagingEngine::core1RoutedMessages = 0;
uint32_t InterruptMessagingEngine::interruptCount = 0;

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool InterruptMessagingEngine::init() {
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    ESP_LOGW(TAG, "Initializing Core 1 Interrupt Messaging Engine");

    // Get MessageCore instance
    messageCore = &MessageCore::getInstance();
    if (!messageCore) {
        ESP_LOGE(TAG, "Failed to get MessageCore instance");
        return false;
    }

    // Create synchronization objects
    uartMutex = xSemaphoreCreateMutex();
    routingMutex = xSemaphoreCreateMutex();

    if (!uartMutex || !routingMutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return false;
    }

    // Create message queues
    incomingDataQueue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char));
    outgoingMessageQueue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(String*));
    core1ProcessingQueue = xQueueCreate(INTERNAL_MSG_QUEUE_SIZE, sizeof(ExternalMessage));
    core0NotificationQueue = xQueueCreate(INTERNAL_MSG_QUEUE_SIZE, sizeof(InternalMessage));

    if (!incomingDataQueue || !outgoingMessageQueue || !core1ProcessingQueue || !core0NotificationQueue) {
        ESP_LOGE(TAG, "Failed to create message queues");
        return false;
    }

    // Initialize UART with interrupt support
    if (!initUART()) {
        ESP_LOGE(TAG, "Failed to initialize UART");
        return false;
    }

    // Register with MessageCore as Serial transport
    if (!registerWithMessageCore()) {
        ESP_LOGE(TAG, "Failed to register with MessageCore");
        return false;
    }

    // Reset statistics
    messagesReceived = 0;
    messagesSent = 0;
    bufferOverruns = 0;
    core1RoutedMessages = 0;
    interruptCount = 0;
    rxBufferPos = 0;

    initialized = true;
    ESP_LOGW(TAG, "Core 1 Messaging Engine initialized successfully");
    return true;
}

bool InterruptMessagingEngine::start() {
    if (!initialized) {
        ESP_LOGE(TAG, "Cannot start - not initialized");
        return false;
    }

    if (running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    ESP_LOGW(TAG, "Starting Core 1 Messaging Engine task");

    // CRITICAL: Set running = true BEFORE creating task to avoid race condition
    running = true;

    // Create messaging task on Core 1
    BaseType_t result = xTaskCreatePinnedToCore(
        messagingTask,
        "Core1_Messaging",
        8 * 1024,  // 8KB stack directly specified
        nullptr,
        configMAX_PRIORITIES - 2,  // High priority for messaging
        &messagingTaskHandle,
        1  // Core 1
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create messaging task on Core 1");
        running = false;  // Reset on failure
        return false;
    }

    ESP_LOGW(TAG, "Core 1 Messaging Engine started successfully");
    return true;
}

void InterruptMessagingEngine::stop() {
    if (!running) {
        return;
    }

    ESP_LOGW(TAG, "Stopping Core 1 Messaging Engine");
    running = false;

    if (messagingTaskHandle) {
        vTaskDelete(messagingTaskHandle);
        messagingTaskHandle = nullptr;
    }

    ESP_LOGW(TAG, "Core 1 Messaging Engine stopped");
}

bool InterruptMessagingEngine::isRunning() {
    return running;
}

void InterruptMessagingEngine::getStats(uint32_t& msgReceived, uint32_t& msgSent,
                                        uint32_t& bufOverruns, uint32_t& core1Routed) {
    msgReceived = messagesReceived;
    msgSent = messagesSent;
    bufOverruns = bufferOverruns;
    core1Routed = core1RoutedMessages;
}

// =============================================================================
// CORE 1 MESSAGING TASK IMPLEMENTATION
// =============================================================================

void InterruptMessagingEngine::messagingTask(void* parameter) {
    ESP_LOGW(TAG, "Core 1 Messaging Task started on Core %d", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t taskFrequency = pdMS_TO_TICKS(5);  // 5ms cycle time

    while (running) {
        // Process incoming UART data (highest priority)
        processIncomingData();

        // Process outgoing messages
        processOutgoingMessages();

        // Process Core 1 routed messages (external message processing)
        processCore1Messages();

        // Update MessageCore
        if (messageCore) {
            messageCore->update();
        }

        // Yield periodically to prevent watchdog
        vTaskDelayUntil(&lastWakeTime, taskFrequency);
    }

    ESP_LOGW(TAG, "Core 1 Messaging Task ended");
    vTaskDelete(nullptr);
}

void InterruptMessagingEngine::processIncomingData() {
    // Use standard UART read instead of interrupt-based approach
    uint8_t data[64];
    int length = uart_read_bytes(UART_NUM_0, data, sizeof(data), pdMS_TO_TICKS(1));

    if (length > 0) {
        for (int i = 0; i < length; i++) {
            char byte = data[i];

            // Add to message buffer
            if (rxBufferPos < UART_RX_BUFFER_SIZE - 1) {
                rxBuffer[rxBufferPos++] = byte;

                // Check for complete message (assuming newline termination)
                if (byte == '\n' || byte == '\r') {
                    rxBuffer[rxBufferPos] = '\0';

                    // Parse and route message
                    ExternalMessage message;
                    if (parseCompleteMessage(rxBuffer, rxBufferPos, message)) {
                        messagesReceived++;
                        routeExternalMessage(message);
                    }

                    // Reset buffer for next message
                    rxBufferPos = 0;
                }
            } else {
                // Buffer overflow - reset and count overrun
                bufferOverruns++;
                rxBufferPos = 0;
                ESP_LOGW(TAG, "RX buffer overflow - message dropped");
            }
        }
    }
}

void InterruptMessagingEngine::processOutgoingMessages() {
    String* messagePtr;

    // Process all queued outgoing messages
    while (xQueueReceive(outgoingMessageQueue, &messagePtr, 0) == pdTRUE) {
        if (messagePtr) {
            // Send via UART
            if (sendRawData(messagePtr->c_str(), messagePtr->length())) {
                messagesSent++;
            }

            // Clean up message
            delete messagePtr;
        }
    }
}

void InterruptMessagingEngine::processCore1Messages() {
    ExternalMessage message;

    // Process messages specifically routed to Core 1
    while (xQueueReceive(core1ProcessingQueue, &message, 0) == pdTRUE) {
        core1RoutedMessages++;
        processExternalMessageOnCore1(message);
    }
}

// =============================================================================
// SIMPLIFIED UART HANDLING (No low-level interrupts)
// =============================================================================

void InterruptMessagingEngine::uartISR(void* arg) {
    // Simplified: Remove low-level UART interrupt handler
    // Use standard UART driver polling instead
}

bool InterruptMessagingEngine::initUART() {
    ESP_LOGW(TAG, "Initializing UART with standard driver");

    // Check if UART driver is already installed (likely by logging system)
    bool driverAlreadyInstalled = uart_is_driver_installed(UART_NUM_0);

    if (driverAlreadyInstalled) {
        ESP_LOGW(TAG, "UART driver already installed by system (likely logging) - using existing driver");

        // Configure UART parameters on existing driver
        uart_config_t uart_config = {
            .baud_rate = MESSAGING_SERIAL_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_APB,
        };

        esp_err_t err = uart_param_config(UART_NUM_0, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to reconfigure existing UART driver: %s - using current settings", esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "Successfully reconfigured existing UART driver for messaging");
        }

        // Flush any existing data in buffers
        uart_flush(UART_NUM_0);

        ESP_LOGW(TAG, "UART messaging interface ready (using existing driver)");
        return true;
    } else {
        // No driver installed yet - install our own
        ESP_LOGW(TAG, "No existing UART driver found - installing new driver");

        // Configure UART parameters
        uart_config_t uart_config = {
            .baud_rate = MESSAGING_SERIAL_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_APB,
        };

        // Install UART driver with standard buffers
        esp_err_t err = uart_driver_install(UART_NUM_0,
                                            UART_RX_BUFFER_SIZE,
                                            UART_TX_BUFFER_SIZE,
                                            0, nullptr, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
            return false;
        }

        // Configure UART parameters
        err = uart_param_config(UART_NUM_0, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
            return false;
        }

        // Set pins (using default pins)
        err = uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGW(TAG, "UART initialized with new driver installation");
        return true;
    }
}

bool InterruptMessagingEngine::sendRawData(const char* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int written = uart_write_bytes(UART_NUM_0, data, length);
        xSemaphoreGive(uartMutex);

        return (written == length);
    }

    return false;
}

// =============================================================================
// MessageCore TRANSPORT INTEGRATION
// =============================================================================

bool InterruptMessagingEngine::registerWithMessageCore() {
    ESP_LOGW(TAG, "Registering with MessageCore as Serial transport");

    TransportInterface transport;
    transport.sendRaw = transportSend;
    transport.isConnected = transportIsConnected;
    transport.update = transportUpdate;
    transport.getStatus = transportGetStatus;
    // CRITICAL FIX: Do NOT set transport.init to avoid infinite recursion
    // The transport is already initialized by the time we register it
    transport.init = nullptr;  // Transport already initialized
    transport.deinit = transportDeinit;

    // Use the standard Serial transport name for compatibility
    messageCore->registerTransport(Config::TRANSPORT_NAME_SERIAL, transport);

    ESP_LOGW(TAG, "Registered with MessageCore as '%s' transport successfully", Config::TRANSPORT_NAME_SERIAL);
    return true;
}

// Transport interface implementations
bool InterruptMessagingEngine::transportSend(const String& payload) {
    if (!running) {
        return false;
    }

    // Queue message for sending (allocate on heap for queue)
    String* messagePtr = new String(payload + "\n");

    if (xQueueSend(outgoingMessageQueue, &messagePtr, pdMS_TO_TICKS(10)) != pdTRUE) {
        delete messagePtr;
        return false;
    }

    return true;
}

bool InterruptMessagingEngine::transportIsConnected() {
    return running && initialized;
}

void InterruptMessagingEngine::transportUpdate() {
    // Update is handled by the main messaging task
}

String InterruptMessagingEngine::transportGetStatus() {
    return String("Core1 Interrupt Engine - Running: ") + (running ? "Yes" : "No") +
           ", Messages RX: " + String(messagesReceived) +
           ", Messages TX: " + String(messagesSent);
}

// REMOVED: transportInit() method removed to prevent infinite recursion
// Transport initialization is handled directly by init() and start() methods

void InterruptMessagingEngine::transportDeinit() {
    stop();
}

// =============================================================================
// MESSAGE PARSING AND ROUTING
// =============================================================================

bool InterruptMessagingEngine::parseCompleteMessage(const char* buffer, size_t length, ExternalMessage& message) {
    if (!buffer || length == 0) {
        return false;
    }

    // Use MessageCore's existing parsing
    String messageStr(buffer);
    message = MessageParser::parseExternalMessage(messageStr);

    return (message.messageType != MessageProtocol::ExternalMessageType::INVALID);
}

void InterruptMessagingEngine::routeExternalMessage(const ExternalMessage& message) {
    if (xSemaphoreTake(routingMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Check if message should be processed on Core 1
        // MessageProtocol::InternalMessageCategory category =
        //     MessageProtocol::getExternalMessageCategory(message.messageType);

        // Use MessageCore's routing logwc by converting to internal message first
        std::vector<InternalMessage> internalMessages =
            MessageConverter::externalToInternal(message);

        for (const auto& internalMsg : internalMessages) {
            if (internalMsg.shouldRouteToCore1()) {
                // Process on Core 1 (current core)
                xQueueSend(core1ProcessingQueue, &message, 0);
            } else {
                // Route to Core 0
                routeInternalMessage(internalMsg);
            }
        }

        xSemaphoreGive(routingMutex);
    }
}

void InterruptMessagingEngine::processExternalMessageOnCore1(const ExternalMessage& message) {
    ESP_LOGD(TAG, "Processing external message on Core 1: type %d",
             static_cast<int>(message.messageType));

    // Use MessageCore to handle the message
    if (messageCore) {
        messageCore->handleExternalMessage(message);
    }
}

void InterruptMessagingEngine::routeInternalMessage(const InternalMessage& message) {
    // Send to Core 0 for processing
    if (xQueueSend(core0NotificationQueue, &message, 0) == pdTRUE) {
        // Successfully queued for Core 0
    } else {
        ESP_LOGW(TAG, "Failed to route internal message to Core 0 - queue full");
    }
}

void InterruptMessagingEngine::notifyCore0(const InternalMessage& message) {
    routeInternalMessage(message);
}

}  // namespace Core1

// =============================================================================
// CORE 1 UTILITY FUNCTIONS
// =============================================================================

namespace Core1Utils {

bool processExternalMessage(const ExternalMessage& message) {
    // Leverage MessageCore's existing processing
    MessageCore& core = MessageCore::getInstance();
    core.handleExternalMessage(message);
    return true;
}

std::vector<InternalMessage> convertExternalToInternal(const ExternalMessage& external) {
    return MessageConverter::externalToInternal(external);
}

bool validateExternalMessage(ExternalMessage& message) {
    return message.validate();
}

}  // namespace Core1Utils

}  // namespace Messaging
