THIS SHOULD BE A LINTER ERROR#include "InterruptMessagingEngine.h"
#include "MessageAPI.h"
#include "MessageConfig.h"
#include "MessageData.h"
#include "../include/MessagingConfig.h"
#include "../include/BinaryProtocol.h"
#include "../include/CoreLoggingFilter.h"
#include "../include/DebugUtils.h"
#include "../ui/screens/ui_screenDebug.h"
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

    ESP_LOGI(TAG, "Initializing Core 1 Binary Protocol Messaging Engine");

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
    ESP_LOGI(TAG, "Binary protocol framer ready with compatible CRC-16-MODBUS algorithm");

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
    ESP_LOGI(TAG, "Core 1 Binary Protocol Messaging Engine initialized successfully");
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

    ESP_LOGI(TAG, "Starting Core 1 Binary Protocol Messaging Engine task");

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

    ESP_LOGI(TAG, "Core 1 Binary Protocol Messaging Engine started successfully");

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
    const TickType_t taskFrequency = pdMS_TO_TICKS(1);  // 1ms cycle time for better responsiveness

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

    // Use Arduino Serial for consistency with transmission method
    uint8_t data[256];  // Increased from 64 to 256 bytes
    int length = 0;

    // Read available data from Arduino Serial
    while (Serial.available() && length < sizeof(data)) {
        data[length] = Serial.read();
        length++;
    }

    if (length > 0) {
        ESP_LOGD(TAG, "Received %d bytes from UART", length);

        // Process incoming bytes through binary protocol framer
        std::vector<String> decodedMessages = binaryFramer->processIncomingBytes(data, length);

        ESP_LOGD(TAG, "Binary framer decoded %zu messages", decodedMessages.size());

        // Process each decoded JSON message
        for (const String& jsonMessage : decodedMessages) {
            ESP_LOGD(TAG, "Decoded JSON: %s", jsonMessage.c_str());

            // Log every received message payload to UI
            LOG_TO_UI(ui_txtAreaDebugLog, ("RX: " + jsonMessage).c_str());

            ExternalMessage message;
            if (parseCompleteMessage(jsonMessage.c_str(), jsonMessage.length(), message)) {
                messagesReceived++;
                ESP_LOGD(TAG, "Message parsed successfully: Type=%d Device=%s", 
                         static_cast<int>(message.messageType), message.deviceId.c_str());

                // Log to UI with success indicator
                LOG_TO_UI(ui_txtAreaDebugLog, ("✓ PARSED: Type=" + String(static_cast<int>(message.messageType)) +
                                               " Device=" + message.deviceId)
                                                  .c_str());

                routeExternalMessage(message);
                ESP_LOGD(TAG, "Message parsed and routed successfully");
            } else {
                bufferOverruns++;
                ESP_LOGD(TAG, "Parse failed for JSON message");

                // Log parsing failures to UI
                LOG_TO_UI(ui_txtAreaDebugLog, ("PARSE ERROR: " + jsonMessage).c_str());
            }
        }
    }
}

void InterruptMessagingEngine::processOutgoingMessages() {
    // NOTE: We now use direct transmission in transportSend() instead of queued frames
    // This method is kept for compatibility but should not receive any messages
    // since we're using the SerialBridge-style direct transmission approach

    BinaryMessage* messagePtr;
    while (xQueueReceive(outgoingMessageQueue, &messagePtr, 0) == pdTRUE) {
        ESP_LOGW(TAG, "Unexpected queued message received - cleaning up");
        if (messagePtr) {
            if (messagePtr->data) {
                delete[] messagePtr->data;
            }
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
    ESP_LOGW(TAG, "Initializing Arduino Serial interface (like working SerialBridge)");

    // Use Arduino Serial initialization for consistency with Serial.write()
    Serial.begin(MESSAGING_SERIAL_BAUD_RATE);

    // Wait for Serial to be ready
    unsigned long startTime = millis();
    const unsigned long SERIAL_TIMEOUT_MS = 5000;  // 5 seconds max wait

    while (!Serial && (millis() - startTime) < SERIAL_TIMEOUT_MS) {
        delay(10);  // Small delay while waiting for Serial
    }

    if (!Serial) {
        ESP_LOGE(TAG, "Arduino Serial failed to initialize within timeout");
        return false;
    }

    // Give Serial a moment to fully initialize
    delay(100);

    // Clear any existing data in buffers
    while (Serial.available()) {
        Serial.read();
    }

    ESP_LOGW(TAG, "Arduino Serial initialized successfully at %d baud", MESSAGING_SERIAL_BAUD_RATE);
    ESP_LOGW(TAG, "Serial ready: %s", Serial ? "YES" : "NO");

    // Verify Serial is working by checking if we can write/flush
    size_t testWrite = Serial.write(0x00);  // Test writing a null byte
    Serial.flush();

    if (testWrite == 1) {
        ESP_LOGW(TAG, "Serial null byte test successful - ready for binary protocol");
    } else {
        ESP_LOGW(TAG, "Serial null byte test failed - may have issues with binary data");
    }

    return true;
}

bool InterruptMessagingEngine::sendRawData(const char* data, size_t length) {
    if (!data || length == 0) {
        return false;
    }



    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool success = false;



        try {
            size_t written = Serial.write(reinterpret_cast<const uint8_t*>(data), length);
            Serial.flush();  // Ensure all bytes are transmitted

            if (written == length) {
                ESP_LOGD(TAG, "Serial transmission successful: %zu bytes", written);
                success = true;
            } else {
                ESP_LOGD(TAG, "Serial transmission failed: %zu out of %zu bytes", written, length);
            }

            // ALWAYS try the exact SerialBridge method regardless of bulk success
            // This helps us debug if the issue is bulk vs individual writes
            ESP_LOGW(TAG, "=== TRYING EXACT SERIALBRIDGE METHOD ===");
            ESP_LOGW(TAG, "Individual Serial.write() calls like working SerialBridge");

            size_t totalIndividualWritten = 0;
            bool individualSuccess = true;

            for (size_t i = 0; i < length; i++) {
                uint8_t byte = static_cast<uint8_t>(data[i]);
                size_t byteResult = Serial.write(byte);

                if (byteResult == 1) {
                    totalIndividualWritten++;

                    // Debug critical header bytes and null bytes
                    if (i < 16 || byte == 0x00) {
                        ESP_LOGW(TAG, "  Individual TX[%zu]: 0x%02X %s result=%zu",
                                 i, byte, (byte == 0x00) ? "NULL!" : "", byteResult);
                    }

                    // Extra care for null bytes (like working SerialBridge)
                    if (byte == 0x00) {
                        Serial.flush();  // Immediate flush after null bytes
                        delay(2);        // Slightly longer delay for null bytes
                    }
                } else {
                    ESP_LOGW(TAG, "Individual write FAILED at byte %zu: 0x%02X", i, byte);
                    individualSuccess = false;
                    break;
                }
            }

            Serial.flush();  // Final flush
            ESP_LOGW(TAG, "Individual writes result: %zu out of %zu bytes written",
                     totalIndividualWritten, length);

            // Use individual method result if it's better
            if (individualSuccess && totalIndividualWritten == length) {
                ESP_LOGW(TAG, "Individual write method successful!");
                success = true;
            } else if (!success) {
                ESP_LOGW(TAG, "Both bulk and individual methods failed");
                success = false;
            }
        } catch (...) {
            ESP_LOGE(TAG, "Exception during Arduino Serial transmission");
            success = false;
        }

        // Clear any received data that might be echo/garbage
        while (Serial.available()) {
            Serial.read();
        }

        xSemaphoreGive(uartMutex);
        return success;
    }

    ESP_LOGE(TAG, "Failed to acquire UART mutex");
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

    ESP_LOGD(TAG, "Sending JSON message using direct transmission: %d bytes", payload.length());

    // Use direct transmission method (like working SerialBridge)
    bool success = binaryFramer->transmitMessageDirect(payload, [](uint8_t byte) -> bool {
        // Write byte using Arduino Serial and check success
        size_t written = Serial.write(byte);
        if (written == 1) {
            // Extra care for null bytes
            if (byte == 0x00) {
                Serial.flush();  // Immediate flush after null bytes
                delay(2);        // Extra delay for null bytes
            }
            return true;
        }
        return false;
    });

    if (!success) {
        ESP_LOGE(TAG, "Direct transmission failed");
        return false;
    }

    // Final flush to ensure all data is transmitted
    Serial.flush();

    ESP_LOGD(TAG, "Direct transmission completed successfully");
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
