#include "SerialEngine.h"
#include "../MessageAPI.h"
#include "../protocol/MessageConfig.h"
#include "../protocol/MessageData.h"
#include "MessagingConfig.h"
#include "BinaryProtocol.h"
#include "CoreLoggingFilter.h"
#include "DebugUtils.h"
#include "../../ui/screens/ui_screenDebug.h"
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
    ESP_LOGD(TAG, "Binary protocol framer ready with compatible CRC-16-MODBUS algorithm");

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
    return true;
}

void InterruptMessagingEngine::stop() {
    if (!running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping Core 1 Binary Protocol Messaging Engine");
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

    ESP_LOGI(TAG, "Core 1 Binary Protocol Messaging Engine stopped");
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
    ESP_LOGI(TAG, "Core 1 Messaging Task started on Core %d", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t taskFrequency = pdMS_TO_TICKS(1);  // 1ms cycle time for better responsiveness

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

    ESP_LOGI(TAG, "Core 1 Messaging Task ended");
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
        // Process incoming bytes through binary protocol framer
        std::vector<String> decodedMessages = binaryFramer->processIncomingBytes(data, length);

        // Process each decoded JSON message
        for (const String& jsonMessage : decodedMessages) {
            ExternalMessage message;
            if (parseCompleteMessage(jsonMessage.c_str(), jsonMessage.length(), message)) {
                messagesReceived++;

                // Log to UI with success indicator
                LOG_TO_UI(ui_txtAreaDebugLog, ("✓ PARSED: Type=" + String(static_cast<int>(message.messageType)) +
                                               " Device=" + message.deviceId)
                                                  .c_str());

                routeExternalMessage(message);
            } else {
                bufferOverruns++;

                // Log parsing failures to UI
                LOG_TO_UI(ui_txtAreaDebugLog, ("PARSE ERROR: " + jsonMessage).c_str());
            }
        }
    }
}

void InterruptMessagingEngine::processOutgoingMessages() {
    // Process any queued outgoing messages (for retry, priority, batching)
    BinaryMessage* messagePtr;
    while (xQueueReceive(outgoingMessageQueue, &messagePtr, 0) == pdTRUE) {
        if (messagePtr && messagePtr->data && messagePtr->length > 0) {
            // Send the queued message
            bool success = sendRawData(reinterpret_cast<const char*>(messagePtr->data), messagePtr->length);

            if (success) {
                messagesSent++;
            }

            // Clean up the message
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

bool InterruptMessagingEngine::initUART() {
    ESP_LOGI(TAG, "Initializing Arduino Serial interface");

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

    ESP_LOGI(TAG, "Serial initialized successfully at %d baud", MESSAGING_SERIAL_BAUD_RATE);
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
                success = true;
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

bool InterruptMessagingEngine::sendMessageIntelligent(const String& payload) {
    if (!running || !binaryFramer) {
        return false;
    }

    size_t payloadSize = payload.length();

    // Transmission strategy based on message characteristics
    const size_t DIRECT_TRANSMISSION_THRESHOLD = 512;  // bytes

    // Check queue congestion
    UBaseType_t queueSpacesAvailable = uxQueueSpacesAvailable(outgoingMessageQueue);
    bool queueCongested = queueSpacesAvailable < (MESSAGE_QUEUE_SIZE / 4);  // Less than 25% space

    // Decision matrix:
    // Small messages + low congestion = Direct transmission (fast)
    // Large messages OR high congestion = Queue (reliable)

    bool useDirect = (payloadSize <= DIRECT_TRANSMISSION_THRESHOLD) && !queueCongested;

    if (useDirect) {
        // Attempt direct transmission for speed
        bool success = attemptDirectTransmission(payload);
        if (success) {
            messagesSent++;
            return true;
        }
        // Direct failed, fall through to queuing
    }

    // Use queued transmission for reliability
    return queueMessageForTransmission(payload);
}

bool InterruptMessagingEngine::attemptDirectTransmission(const String& payload) {
    bool success = binaryFramer->transmitMessageDirect(payload, [](uint8_t byte) -> bool {
        size_t written = Serial.write(byte);
        if (written == 1) {
            if (byte == 0x00) {
                Serial.flush();
                delay(2);
            }
            return true;
        }
        return false;
    });

    if (success) {
        Serial.flush();
    }

    return success;
}

bool InterruptMessagingEngine::queueMessageForTransmission(const String& payload) {
    // Encode the payload to binary format for queuing
    std::vector<uint8_t> binaryFrame = binaryFramer->encodeMessage(payload);
    if (binaryFrame.empty()) {
        return false;
    }

    // Create binary message for queue
    BinaryMessage* message = new BinaryMessage();
    message->length = binaryFrame.size();
    message->data = new uint8_t[message->length];
    std::copy(binaryFrame.begin(), binaryFrame.end(), message->data);

    // Queue the message (non-blocking)
    if (xQueueSend(outgoingMessageQueue, &message, 0) == pdTRUE) {
        return true;
    } else {
        // Queue full - clean up and report failure
        delete[] message->data;
        delete message;
        return false;
    }
}

// =============================================================================
// MessageCore TRANSPORT INTEGRATION
// =============================================================================

bool InterruptMessagingEngine::registerWithMessageCore() {
    ESP_LOGI(TAG, "Registering with MessageCore as Serial transport");

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

    ESP_LOGI(TAG, "Registered with MessageCore as '%s' transport successfully", Config::TRANSPORT_NAME_SERIAL);
    return true;
}

// Transport interface implementations
bool InterruptMessagingEngine::transportSend(const String& payload) {
    if (!running || !binaryFramer) {
        return false;
    }

    // Intelligent transmission strategy based on message characteristics
    return sendMessageIntelligent(payload);
}

bool InterruptMessagingEngine::transportIsConnected() {
    return running && initialized;
}

void InterruptMessagingEngine::transportUpdate() {
    // No-op: Update handled by main messaging task
}

String InterruptMessagingEngine::transportGetStatus() {
    String status = String("Core1 Engine - Running: ") + (running ? "Yes" : "No") +
                    ", RX: " + String(messagesReceived) + ", TX: " + String(messagesSent);

    if (binaryFramer) {
        const auto& stats = binaryFramer->getStatistics();
        status += ", Errors: " + String(stats.crcErrors + stats.framingErrors);
    }
    return status;
}

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
    if (messageCore) {
        messageCore->handleExternalMessage(message);
    }
}

void InterruptMessagingEngine::routeInternalMessage(const InternalMessage& message) {
    if (xQueueSend(core0NotificationQueue, &message, 0) != pdTRUE) {
        ESP_LOGD(TAG, "Failed to route internal message to Core 0 - queue full");
    }
}

}  // namespace Core1

}  // namespace Messaging
