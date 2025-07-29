#pragma once

#include "Message.h"
#include "MessageQueue.h"
#include "UiEventHandlers.h"
#include <Arduino.h>
#include <BinaryProtocol.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>  // Added for SemaphoreHandle_t
#include <memory>

namespace Messaging {
static uint8_t testPayload[] = {
    0x7E, 0xCA, 0x01, 0x00, 0x00, 0x4E, 0x56, 0x01, 0x7B, 0x22, 0x6D, 0x65,
    0x73, 0x73, 0x61, 0x67, 0x65, 0x54, 0x79, 0x70, 0x65, 0x22, 0x3A, 0x22,
    0x53, 0x54, 0x41, 0x54, 0x55, 0x53, 0x5F, 0x4D, 0x45, 0x53, 0x53, 0x41,
    0x47, 0x45, 0x22, 0x2C, 0x22, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x49,
    0x64, 0x22, 0x3A, 0x22, 0x54, 0x48, 0x49, 0x4E, 0x4B, 0x49, 0x4E, 0x41,
    0x54, 0x4F, 0x52, 0x22, 0x2C, 0x22, 0x74, 0x69, 0x6D, 0x65, 0x73, 0x74,
    0x61, 0x6D, 0x70, 0x22, 0x3A, 0x31, 0x37, 0x35, 0x32, 0x32, 0x39, 0x32,
    0x38, 0x36, 0x39, 0x32, 0x31, 0x39, 0x2C, 0x22, 0x61, 0x63, 0x74, 0x69,
    0x76, 0x65, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x43, 0x6F, 0x75,
    0x6E, 0x74, 0x22, 0x3A, 0x31, 0x2C, 0x22, 0x73, 0x65, 0x73, 0x73, 0x69,
    0x6F, 0x6E, 0x73, 0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x70, 0x72, 0x6F, 0x63,
    0x65, 0x73, 0x73, 0x49, 0x64, 0x22, 0x3A, 0x31, 0x36, 0x32, 0x34, 0x30,
    0x2C, 0x22, 0x70, 0x72, 0x6F, 0x63, 0x65, 0x73, 0x73, 0x4E, 0x61, 0x6D,
    0x65, 0x22, 0x3A, 0x22, 0x63, 0x68, 0x72, 0x6F, 0x6D, 0x65, 0x22, 0x2C,
    0x22, 0x64, 0x69, 0x73, 0x70, 0x6C, 0x61, 0x79, 0x4E, 0x61, 0x6D, 0x65,
    0x22, 0x3A, 0x22, 0x22, 0x2C, 0x22, 0x76, 0x6F, 0x6C, 0x75, 0x6D, 0x65,
    0x22, 0x3A, 0x31, 0x2C, 0x22, 0x69, 0x73, 0x4D, 0x75, 0x74, 0x65, 0x64,
    0x22, 0x3A, 0x66, 0x61, 0x6C, 0x73, 0x65, 0x2C, 0x22, 0x73, 0x74, 0x61,
    0x74, 0x65, 0x22, 0x3A, 0x22, 0x41, 0x75, 0x64, 0x69, 0x6F, 0x53, 0x65,
    0x73, 0x73, 0x69, 0x6F, 0x6E, 0x53, 0x74, 0x61, 0x74, 0x65, 0x41, 0x63,
    0x74, 0x69, 0x76, 0x65, 0x22, 0x7D, 0x5D, 0x5D, 0x2C, 0x22, 0x64, 0x65,
    0x66, 0x61, 0x75, 0x6C, 0x74, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x22,
    0x3A, 0x7B, 0x22, 0x66, 0x72, 0x69, 0x65, 0x6E, 0x64, 0x6C, 0x79, 0x4E,
    0x61, 0x6D, 0x65, 0x22, 0x3A, 0x22, 0x48, 0x65, 0x61, 0x64, 0x70, 0x68,
    0x6F, 0x6E, 0x65, 0x73, 0x20, 0x28, 0x57, 0x48, 0x2D, 0x31, 0x30, 0x30,
    0x30, 0x58, 0x4D, 0x35, 0x29, 0x22, 0x2C, 0x22, 0x76, 0x6F, 0x6C, 0x75,
    0x6D, 0x65, 0x22, 0x3A, 0x30, 0x2E, 0x35, 0x39, 0x38, 0x34, 0x32, 0x35,
    0x34, 0x35, 0x2C, 0x22, 0x69, 0x73, 0x4D, 0x75, 0x74, 0x65, 0x64, 0x22,
    0x3A, 0x66, 0x61, 0x6C, 0x73, 0x65, 0x2C, 0x22, 0x64, 0x61, 0x74, 0x61,
    0x46, 0x6C, 0x6F, 0x77, 0x22, 0x3A, 0x22, 0x52, 0x65, 0x6E, 0x64, 0x65,
    0x72, 0x22, 0x2C, 0x22, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x52, 0x6F,
    0x6C, 0x65, 0x22, 0x3A, 0x22, 0x43, 0x6F, 0x6E, 0x73, 0x6F, 0x6C, 0x65,
    0x22, 0x7D, 0x5D, 0x2C, 0x22, 0x72, 0x65, 0x61, 0x73, 0x6F, 0x6E, 0x22,
    0x3A, 0x22, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x43, 0x68, 0x61,
    0x6E, 0x67, 0x65, 0x22, 0x2C, 0x22, 0x6F, 0x72, 0x69, 0x67, 0x69, 0x6E,
    0x61, 0x74, 0x69, 0x6E, 0x67, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74,
    0x49, 0x64, 0x22, 0x3A, 0x6E, 0x75, 0x6C, 0x6C, 0x2C, 0x22, 0x6F, 0x72,
    0x69, 0x67, 0x69, 0x6E, 0x61, 0x74, 0x69, 0x6E, 0x67, 0x44, 0x65, 0x76,
    0x69, 0x63, 0x65, 0x49, 0x64, 0x22, 0x3A, 0x6E, 0x75, 0x6C, 0x6C, 0x7D,
    0x5D, 0x7F};

/**
 * SIMPLE SERIAL ENGINE
 * Streamlined message sending: App code calls SerialEngine::send() with Message
 * objects. Messages are converted to JSON and queued for efficient Core 1
 * transmission. The queuing stage is completely invisible to application code.
 *
 * MESSAGE SIZE LIMITS:
 * - Maximum message size: 16KB (16,384 bytes) to match server limit
 * - Asset data: Up to 16KB base64 encoded
 * - Binary protocol overhead: ~1KB for framing and escaping
 *
 * Usage Examples:
 *   // Method 1: Direct SerialEngine call
 *   auto msg = Message::createStatusRequest("device123");
 *   SerialEngine::getInstance().send(msg);
 *
 *   // Method 2: Message self-send (preferred)
 *   auto msg = Message::createVolumeChange("chrome", 75, "");
 *   msg.send();
 *
 * Core Detection: Messages from Core 0 are queued, Core 1 messages sent
 * directly.
 */
class SerialEngine {
   private:
    static SerialEngine *instance;

    // Serial access synchronization - CRITICAL for dual-core safety
    static SemaphoreHandle_t serialMutex;

    // Serial configuration
    static const int SERIAL_BAUD_RATE = 115200;
    static const size_t RX_BUFFER_SIZE = 16384;  // 16KB to match server message limit

    // Task configuration
    TaskHandle_t rxtxTaskHandle = nullptr;
    bool running = false;

    // Frame buffer for encoding - shared across all send operations
    uint8_t frameBuffer[16384 + 1024];  // 16KB payload + overhead for framing/escaping

    // Modern message queue using FreeRTOS MessageBuffer
    std::unique_ptr<MessageQueue> txMessageQueue;

   public:
    // Binary protocol for framing
    BinaryProtocol::BinaryProtocolFramer framer;

   private:
    // Statistics
    struct Stats {
        uint32_t messagesReceived = 0;
        uint32_t messagesSent = 0;
        uint32_t parseErrors = 0;
        uint32_t framingErrors = 0;
        uint32_t messagesQueued = 0;
        uint32_t queueOverflows = 0;
    } stats;

    SerialEngine() = default;

   public:
    static SerialEngine &getInstance() {
        if (instance == nullptr) {
            ESP_LOGI("SerialEngine", "Creating new SerialEngine instance");
            instance = new SerialEngine();
            ESP_LOGI("SerialEngine", "SerialEngine instance created at %p", instance);
        }
        return *instance;
    }

    // Initialize and start the serial engine
    bool init() {
        ESP_LOGI("SerialEngine", "Initializing SimplifiedSerialEngine");

        // Initialize serial access mutex FIRST for dual-core safety
        if (!serialMutex) {
            serialMutex = xSemaphoreCreateMutex();
            if (!serialMutex) {
                ESP_LOGE("SerialEngine", "CRITICAL: Failed to create serial mutex");
                return false;
            }
            ESP_LOGI("SerialEngine", "Serial access mutex created for dual-core safety");
        }

        // Check if Serial is already initialized
        if (!Serial) {
            ESP_LOGI("SerialEngine", "Initializing Arduino Serial at %d baud",
                     SERIAL_BAUD_RATE);

            // Initialize Arduino Serial
            Serial.begin(SERIAL_BAUD_RATE);

            // Wait for Serial to be ready
            unsigned long startTime = millis();
            const unsigned long SERIAL_TIMEOUT_MS = 2000;  // Reduced timeout

            while (!Serial && (millis() - startTime) < SERIAL_TIMEOUT_MS) {
                delay(10);
            }

            if (!Serial) {
                ESP_LOGE("SerialEngine", "Failed to initialize Serial port after %lums",
                         SERIAL_TIMEOUT_MS);
                return false;
            }

            ESP_LOGI("SerialEngine", "Arduino Serial initialized successfully");

            // Give Serial time to stabilize
            delay(100);
        } else {
            ESP_LOGI("SerialEngine", "Serial already initialized");
        }

        // Clear any existing data
        while (Serial.available()) {
            Serial.read();
        }

        // Initialize the TX message queue
        if (!initTxMessageQueue()) {
            ESP_LOGE("SerialEngine", "Failed to initialize TX message queue");
            return false;
        }

        // Mark as running but delay task creation
        running = true;
        ESP_LOGI("SerialEngine",
                 "Serial engine initialized (task creation delayed)");

        // Delay task creation to avoid initialization conflicts
        // The task will be created on first use or can be started manually
        return true;
    }

    // Start the consolidated RXTX task (call this after system is fully
    // initialized)
    bool startReceiveTask() {
        if (rxtxTaskHandle != nullptr) {
            ESP_LOGW("SerialEngine", "RXTX task already started");
            return true;
        }

        ESP_LOGI("SerialEngine", "Starting consolidated RXTX task on Core 1");

        // Start consolidated RXTX Task
        BaseType_t rxtxResult = xTaskCreatePinnedToCore(
            rxtxTaskWrapper, "SerialRxTx",
            32768,  // 32KB stack for handling 16KB messages
            this,
            5,  // Priority
            &rxtxTaskHandle,
            1  // Core 1
        );

        if (rxtxResult != pdPASS) {
            ESP_LOGE("SerialEngine", "Failed to create RXTX task: %d", rxtxResult);
            return false;
        }

        ESP_LOGI("SerialEngine", "RXTX task started successfully on Core 1");
        return true;
    }

    // STREAMLINED MESSAGE SENDING - The only app-wide entry point
    void send(const Message &msg) {
        if (!running) {
            ESP_LOGW("SerialEngine", "Serial engine not running");
            return;
        }

        if (!msg.isValid()) {
            ESP_LOGW("SerialEngine", "Attempted to send invalid message");
            return;
        }

        // Convert message to JSON immediately
        String json = msg.toJson();

        int coreId = xPortGetCoreID();
        ESP_LOGI("SerialEngine", "Sending message from Core %d: type=%s, length=%d",
                 coreId, msg.type.c_str(), json.length());

        if (coreId == 1) {
            // Already on Core 1 - send directly for efficiency
            sendJsonDirect(json);
        } else {
            // Core 0 - queue for Core 1 processing
            enqueueJsonForTx(json);
        }

        stats.messagesSent++;
    }

    // Send raw string (for compatibility and testing)
    void sendRaw(const String &data) {
        if (!running)
            return;

        int coreId = xPortGetCoreID();
        ESP_LOGI("SerialEngine", "Sending raw data from Core %d: length=%d", coreId,
                 data.length());

        if (coreId == 1) {
            sendJsonDirect(data);
        } else {
            enqueueJsonForTx(data);
        }
    }

    // Get statistics
    const Stats &getStats() const { return stats; }

    // Get serial mutex for external synchronization (e.g., CoreLoggingFilter)
    static SemaphoreHandle_t getSerialMutex() { return serialMutex; }

    // Stop the engine
    void stop() {
        running = false;

        if (rxtxTaskHandle) {
            vTaskDelete(rxtxTaskHandle);
            rxtxTaskHandle = nullptr;
        }

        // Cleanup TX message queue
        if (txMessageQueue) {
            txMessageQueue.reset();  // Smart pointer automatically cleans up
            ESP_LOGI("SerialEngine", "TX message queue cleaned up");
        }

        ESP_LOGI("SerialEngine", "RXTX task stopped");
    }

   private:
    // Initialize TX message queue for inter-core communication
    bool initTxMessageQueue() {
        if (!txMessageQueue) {
            txMessageQueue = std::make_unique<MessageQueue>();
            if (!txMessageQueue->isValid()) {
                ESP_LOGE("SerialEngine", "Failed to create TX message queue");
                txMessageQueue.reset();
                return false;
            }
            ESP_LOGI("SerialEngine", "TX message queue initialized with efficient memory usage");
        }
        return true;
    }

    // Enqueue JSON string for Core 1 transmission (called from Core 0)
    void enqueueJsonForTx(const String &json) {
        if (!txMessageQueue) {
            ESP_LOGW("SerialEngine", "TX queue not initialized");
            return;
        }

        // Convert Arduino String to std::string for the queue
        std::string jsonStr(json.c_str());
        
        // Try to send with non-blocking (0ms timeout)
        if (txMessageQueue->send(jsonStr, 0)) {
            stats.messagesQueued++;
            ESP_LOGD("SerialEngine", "Queued JSON message for Core 1 TX: %d bytes",
                     json.length());
        } else {
            stats.queueOverflows++;
            ESP_LOGW("SerialEngine", "TX queue full - message dropped. Free space: %zu bytes",
                     txMessageQueue->getFreeSpace());
        }
    }

    // Send JSON directly (called from Core 1 or for immediate transmission)
    void sendJsonDirect(const String &json) {
        if (json.isEmpty()) {
            return;
        }

        ESP_LOGD("SerialEngine", "Direct JSON transmission: %d bytes",
                 json.length());

        // Use the binary framer to encode the message - using class member buffer
        size_t frameLength = 0;
        std::vector<uint8_t> binaryFrame;

        if (framer.encodeMessage(json, frameBuffer, sizeof(frameBuffer),
                                 frameLength)) {
            binaryFrame.assign(frameBuffer, frameBuffer + frameLength);
        }

        if (!binaryFrame.empty()) {
            ESP_LOGD("SerialEngine", "Binary frame size: %zu bytes",
                     binaryFrame.size());

            // CRITICAL: Protect Serial access with mutex to prevent race conditions
            // between ESP_LOG (vprintf) and SerialEngine (Serial.write)
            if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Send the binary frame via Arduino Serial
                size_t written = Serial.write(binaryFrame.data(), binaryFrame.size());
                Serial.flush();

                xSemaphoreGive(serialMutex);

                if (written != binaryFrame.size()) {
                    ESP_LOGW("SerialEngine", "Failed to write complete frame: %zu/%zu",
                             written, binaryFrame.size());
                } else {
                    ESP_LOGD("SerialEngine", "Successfully sent %zu bytes", written);
                }
            } else {
                ESP_LOGW("SerialEngine", "Failed to acquire serial mutex for transmission");
            }
        } else {
            ESP_LOGW("SerialEngine", "Failed to frame message");
        }
    }

    // Process queued TX messages (called by Core 1 RXTX task)
    bool processTxMessageQueue() {
        if (!txMessageQueue) {
            return false;
        }

        bool processedMessages = false;

        // Process all available messages with non-blocking receive
        while (auto message = txMessageQueue->receive(16384, 0)) {
            processedMessages = true;
            
            // Convert std::string back to Arduino String for sendJsonDirect
            String json(message->c_str());
            ESP_LOGD("SerialEngine", "Processing queued message: %d bytes",
                     json.length());

            sendJsonDirect(json);
        }

        return processedMessages;
    }

    // Task wrapper
    static void rxtxTaskWrapper(void *param) {
        static_cast<SerialEngine *>(param)->rxtxTask();
    }

    // Consolidated RXTX task - handles both receiving and transmitting
    void rxtxTask() {
        uint8_t data[4096];  // Increased to read larger chunks

        ESP_LOGI("SerialEngine",
                 "=== RXTX TASK STARTED ON CORE %d ===", xPortGetCoreID());

        // Monitor stack usage for debugging
        UBaseType_t initialStackSize = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("SerialEngine", "Initial stack high water mark: %d bytes",
                 initialStackSize * sizeof(StackType_t));

        uint32_t stackCheckCounter = 0;
        while (running) {
            // Handle incoming messages (RX)
            if (Serial.available()) {
                int len = 0;
                while (Serial.available() && len < sizeof(data)) {
                    data[len++] = Serial.read();
                }

                if (len > 0) {
                    ESP_LOGI("SerialEngine", "Received %d bytes", len);

                    // Optional: Print hex (max 32 bytes)
                    char hexBuf[3 * 32 + 1] = {};
                    for (int i = 0; i < len && i < 32; i++) {
                        sprintf(&hexBuf[i * 3], "%02X ", data[i]);
                    }
                    ESP_LOGI("SerialEngine", "First 32 bytes: %s", hexBuf);

                    // Check for "test" command in printable ASCII
                    bool isTest = false;
                    for (int i = 0; i <= len - 4; i++) {
                        if (memcmp(&data[i], "test", 4) == 0) {
                            isTest = true;
                            break;
                        }
                    }

                    if (isTest) {
                        ESP_LOGI("SerialEngine",
                                 "DEBUG CMD 'test' DETECTED, injecting test payload...");

                        const size_t payloadSize = sizeof(testPayload);

                        char testHex[3 * 64 + 1] = {};
                        for (size_t i = 0; i < payloadSize && i < 64; i++) {
                            sprintf(&testHex[i * 3], "%02X ", testPayload[i]);
                        }

                        processIncomingData(testPayload, payloadSize);
                    } else {
                        processIncomingData(data, len);
                    }

                    // TEST: Check if "msgtest" command is received for message type
                    // mapping test
                    bool isMsgTest = false;
                    for (int i = 0; i <= len - 7; i++) {
                        if (memcmp(&data[i], "msgtest", 7) == 0) {
                            isMsgTest = true;
                            break;
                        }
                    }

                    if (isMsgTest) {
                        ESP_LOGI("SerialEngine", "=== MESSAGE TYPE MAPPING TEST ===");
                        String testJson =
                            "{\"messageType\":\"STATUS_MESSAGE\","
                            "\"deviceId\":\"TEST\",\"timestamp\":123456}";
                        ESP_LOGI("SerialEngine", "Test JSON: %s", testJson.c_str());

                        auto testMsg = Messaging::Message::fromJson(testJson);
                        ESP_LOGI("SerialEngine", "Parsed type: %s", testMsg.type.c_str());
                        ESP_LOGI("SerialEngine", "Expected: %s",
                                 Messaging::Message::TYPE_AUDIO_STATUS);
                        ESP_LOGI("SerialEngine", "Match: %s",
                                 (testMsg.type == Messaging::Message::TYPE_AUDIO_STATUS)
                                     ? "YES"
                                     : "NO");
                    }
                }
            }

            // Handle outgoing messages (TX) - process queued JSON messages
            bool hasMessages = processTxMessageQueue();

            if (!hasMessages) {
                // No messages processed, short delay to prevent tight loop
                vTaskDelay(pdMS_TO_TICKS(10));  // 10ms sleep when no work
            }
            // If messages were processed, immediately check for more (no delay)

            // Periodic stack monitoring (every 1000 iterations)
            if (++stackCheckCounter >= 1000) {
                stackCheckCounter = 0;
                UBaseType_t currentStackSize = uxTaskGetStackHighWaterMark(NULL);
                ESP_LOGI("SerialEngine", "Stack high water mark: %d bytes (was %d)",
                         currentStackSize * sizeof(StackType_t),
                         initialStackSize * sizeof(StackType_t));
            }
        }

        ESP_LOGI("SerialEngine", "=== RXTX TASK ENDED ===");
        ESP_LOGI("SerialEngine", "RXTX Task final stack high water mark: %d bytes",
                 uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    }

    // Process incoming data - optimized to reduce stack usage
    void processIncomingData(const uint8_t *data, size_t length) {
        ESP_LOGI("SerialEngine", "Processing %zu bytes through binary framer",
                 length);

        // Use the binary protocol framer to decode messages
        std::vector<String> messages = framer.processIncomingBytes(data, length);

        ESP_LOGI("SerialEngine", "Binary framer returned %zu messages",
                 messages.size());

        for (const String &jsonStr : messages) {
            if (!jsonStr.isEmpty()) {
                // LIGHTWEIGHT parsing to avoid stack overflow - just check if it's
                // valid JSON
                bool isValidJson = jsonStr.startsWith("{") && jsonStr.endsWith("}") &&
                                   jsonStr.length() > 10;

                ESP_LOGI("SerialEngine", "Received JSON message, length: %d, valid: %s",
                         jsonStr.length(), isValidJson ? "true" : "false");

                if (isValidJson) {
                    stats.messagesReceived++;

                    // Parse and route the message - avoid creating additional string
                    // copies
                    auto parsed = Messaging::Message::fromJson(jsonStr);
                    ESP_LOGI("SerialEngine", "Parsed message type: %s, device: %.20s",
                             parsed.type.c_str(), parsed.deviceId.c_str());

                    // Route valid messages to handlers
                    Messaging::MessageRouter::getInstance().route(parsed);
                } else {
                    stats.parseErrors++;
                    ESP_LOGW("SerialEngine",
                             "Invalid JSON structure, first 50 chars: %.50s",
                             jsonStr.c_str());
                }
            }
        }
    }
};

// Global instance accessor for compatibility
inline SerialEngine &getSerialEngine() { return SerialEngine::getInstance(); }

}  // namespace Messaging
