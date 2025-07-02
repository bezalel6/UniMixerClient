#include "InterruptMessagingEngine.h"
#include "MessageAPI.h"
#include "MessageConfig.h"
#include "MessageData.h"
#include "../include/MessagingConfig.h"
#include "../include/BinaryProtocol.h"
#include "../include/CoreLoggingFilter.h"
#include <esp_log.h>
#include <driver/gpio.h>

static const char* TAG = "Core1::MessagingEngine";

namespace Messaging {
namespace Core1 {

// Type alias for convenience
using BinaryMessage = InterruptMessagingEngine::BinaryMessage;

// =============================================================================
// STATIC MEMBER DEFINITIONS
// =============================================================================

// Core state
bool InterruptMessagingEngine::initialized = false;
bool InterruptMessagingEngine::running = false;
TaskHandle_t InterruptMessagingEngine::messagingTaskHandle = nullptr;
MessageCore* InterruptMessagingEngine::messageCore = nullptr;

// Binary protocol framing
BinaryProtocol::BinaryProtocolFramer* InterruptMessagingEngine::binaryFramer = nullptr;

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

    ESP_LOGW(TAG, "Initializing Core 1 Binary Protocol Messaging Engine");

    // Get MessageCore instance
    messageCore = &MessageCore::getInstance();
    if (!messageCore) {
        ESP_LOGE(TAG, "Failed to get MessageCore instance");
        return false;
    }

    // Initialize binary protocol framer
    binaryFramer = new BinaryProtocol::BinaryProtocolFramer();
    if (!binaryFramer) {
        ESP_LOGE(TAG, "Failed to create binary protocol framer");
        return false;
    }

    // Binary protocol initialized - using working CRC-16-MODBUS algorithm from previous SerialBridge
    ESP_LOGW(TAG, "Binary protocol framer ready with compatible CRC-16-MODBUS algorithm");

    // Create synchronization objects
    uartMutex = xSemaphoreCreateMutex();
    routingMutex = xSemaphoreCreateMutex();

    if (!uartMutex || !routingMutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return false;
    }

    // Create message queues
    incomingDataQueue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char));
    outgoingMessageQueue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(BinaryMessage*));
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
    ESP_LOGW(TAG, "Core 1 Binary Protocol Messaging Engine initialized successfully");
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

    ESP_LOGW(TAG, "Starting Core 1 Binary Protocol Messaging Engine task");

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

    ESP_LOGW(TAG, "Core 1 Binary Protocol Messaging Engine started successfully");

    // Log from Core 1 to demonstrate logging filter is working
    ESP_LOGI(TAG, "Core 1 messaging active - logging filter allows Core 1 output");
    return true;
}

void InterruptMessagingEngine::stop() {
    if (!running) {
        return;
    }

    ESP_LOGW(TAG, "Stopping Core 1 Binary Protocol Messaging Engine");
    running = false;

    if (messagingTaskHandle) {
        vTaskDelete(messagingTaskHandle);
        messagingTaskHandle = nullptr;
    }

    // Cleanup binary framer
    if (binaryFramer) {
        delete binaryFramer;
        binaryFramer = nullptr;
    }

    ESP_LOGW(TAG, "Core 1 Binary Protocol Messaging Engine stopped");
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

const BinaryProtocol::ProtocolStatistics& InterruptMessagingEngine::getBinaryStats() {
    if (binaryFramer) {
        return binaryFramer->getStatistics();
    }

    // Return a static empty stats if framer not initialized
    static BinaryProtocol::ProtocolStatistics emptyStats;
    return emptyStats;
}

// =============================================================================
// CORE 1 MESSAGING TASK IMPLEMENTATION
// =============================================================================

void InterruptMessagingEngine::messagingTask(void* parameter) {
    ESP_LOGW(TAG, "Core 1 Messaging Task started on Core %d", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t taskFrequency = pdMS_TO_TICKS(5);  // 5ms cycle time

    // Track logging filter statistics
    TickType_t lastLogStatsTime = lastWakeTime;
    const TickType_t logStatsInterval = pdMS_TO_TICKS(30000);  // 30 seconds

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

        // Periodically report logging filter statistics
        TickType_t currentTime = xTaskGetTickCount();
        if ((currentTime - lastLogStatsTime) >= logStatsInterval) {
            uint32_t core0Filtered, core1Allowed;
            CoreLoggingFilter::getStats(core0Filtered, core1Allowed);
            ESP_LOGI(TAG, "Logging Filter Stats - Core 0 filtered: %u, Core 1 allowed: %u",
                     core0Filtered, core1Allowed);
            lastLogStatsTime = currentTime;
        }

        // Yield periodically to prevent watchdog
        vTaskDelayUntil(&lastWakeTime, taskFrequency);
    }

    ESP_LOGW(TAG, "Core 1 Messaging Task ended");
    vTaskDelete(nullptr);
}

void InterruptMessagingEngine::processIncomingData() {
    if (!binaryFramer) {
        ESP_LOGE(TAG, "Binary framer not initialized");
        return;
    }

    // Use standard UART read instead of interrupt-based approach
    uint8_t data[64];
    int length = uart_read_bytes(UART_NUM_0, data, sizeof(data), pdMS_TO_TICKS(1));

    if (length > 0) {
        ESP_LOGD(TAG, "Received %d bytes from UART", length);

        // Process incoming bytes through binary protocol framer
        std::vector<String> decodedMessages = binaryFramer->processIncomingBytes(data, length);

        ESP_LOGD(TAG, "Binary framer decoded %zu messages", decodedMessages.size());

        // Process each decoded JSON message
        for (const String& jsonMessage : decodedMessages) {
            ESP_LOGD(TAG, "Decoded JSON: %s", jsonMessage.c_str());

            ExternalMessage message;
            if (parseCompleteMessage(jsonMessage.c_str(), jsonMessage.length(), message)) {
                messagesReceived++;
                routeExternalMessage(message);
                ESP_LOGD(TAG, "Message parsed and routed successfully");
            } else {
                bufferOverruns++;
                ESP_LOGW(TAG, "Failed to parse decoded JSON message: %s", jsonMessage.c_str());
            }
        }
    }
}

void InterruptMessagingEngine::processOutgoingMessages() {
    BinaryMessage* messagePtr;

    // Process all queued outgoing messages
    while (xQueueReceive(outgoingMessageQueue, &messagePtr, 0) == pdTRUE) {
        if (messagePtr && messagePtr->data && messagePtr->length > 0) {
            ESP_LOGD(TAG, "Sending binary frame: %zu bytes", messagePtr->length);

            // Send binary data via UART
            if (sendRawData(reinterpret_cast<const char*>(messagePtr->data), messagePtr->length)) {
                messagesSent++;
                ESP_LOGD(TAG, "Binary frame transmitted successfully");
            } else {
                ESP_LOGE(TAG, "UART transmission failed");
            }

            // Clean up message
            delete[] messagePtr->data;
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
    if (!running || !binaryFramer) {
        return false;
    }

    ESP_LOGD(TAG, "Encoding JSON message: %d bytes", payload.length());

    // Encode JSON payload using binary protocol
    std::vector<uint8_t> binaryFrame = binaryFramer->encodeMessage(payload);
    if (binaryFrame.empty()) {
        ESP_LOGE(TAG, "Failed to encode message with binary protocol");
        return false;
    }

    ESP_LOGD(TAG, "Binary frame encoded: %zu bytes", binaryFrame.size());

    // Create binary message for queue (allocate on heap for queue)
    BinaryMessage* messagePtr = new BinaryMessage();
    messagePtr->length = binaryFrame.size();
    messagePtr->data = new uint8_t[messagePtr->length];
    memcpy(messagePtr->data, binaryFrame.data(), messagePtr->length);

    if (xQueueSend(outgoingMessageQueue, &messagePtr, pdMS_TO_TICKS(10)) != pdTRUE) {
        delete[] messagePtr->data;
        delete messagePtr;
        ESP_LOGE(TAG, "Failed to queue binary message");
        return false;
    }

    ESP_LOGD(TAG, "Binary message queued for transmission");
    return true;
}

bool InterruptMessagingEngine::transportIsConnected() {
    return running && initialized;
}

void InterruptMessagingEngine::transportUpdate() {
    // Update is handled by the main messaging task
}

String InterruptMessagingEngine::transportGetStatus() {
    String status = String("Core1 Binary Protocol Engine - Running: ") + (running ? "Yes" : "No") +
                    ", Messages RX: " + String(messagesReceived) +
                    ", Messages TX: " + String(messagesSent);

    if (binaryFramer) {
        const auto& binaryStats = binaryFramer->getStatistics();
        status += ", Binary RX: " + String(binaryStats.messagesReceived) +
                  ", Binary TX: " + String(binaryStats.messagesSent) +
                  ", CRC Errors: " + String(binaryStats.crcErrors) +
                  ", Frame Errors: " + String(binaryStats.framingErrors);
    }

    return status;
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
