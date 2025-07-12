#pragma once

#include "Message.h"
#include "UiEventHandlers.h"
#include <Arduino.h>
#include <BinaryProtocol.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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

// Core 1 Message Queue System - now part of SerialEngine
struct Core1Message {
  enum Type { VOLUME_CHANGE, STATUS_REQUEST, MUTE_TOGGLE } type;

  struct VolumeChangeData {
    char deviceName[64]; // Empty string = current device
    int volume;
  } volumeData;

  uint32_t timestamp;
};

/**
 * SIMPLE SERIAL ENGINE
 * No transport abstraction. Just send and receive messages.
 * Includes Core 1 message queue system for inter-core communication.
 */
class SerialEngine {
private:
  static SerialEngine *instance;

  // Serial configuration
  static const int SERIAL_BAUD_RATE = 115200;
  static const size_t RX_BUFFER_SIZE = 4096;

  // Task configuration
  TaskHandle_t rxtxTaskHandle = nullptr;
  bool running = false;

  // Frame buffer for encoding - shared across all send operations
  uint8_t frameBuffer[4096];

  // Core 1 Message Queue System
  QueueHandle_t core1MessageQueue = nullptr;
  static const int CORE1_QUEUE_SIZE = 10;

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

    // Check if Serial is already initialized
    if (!Serial) {
      ESP_LOGI("SerialEngine", "Initializing Arduino Serial at %d baud",
               SERIAL_BAUD_RATE);

      // Initialize Arduino Serial
      Serial.begin(SERIAL_BAUD_RATE);

      // Wait for Serial to be ready
      unsigned long startTime = millis();
      const unsigned long SERIAL_TIMEOUT_MS = 2000; // Reduced timeout

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
        12288, // 12KB for RX/TX processing (increased from 8KB)
        this,
        5, // Priority
        &rxtxTaskHandle,
        1 // Core 1
    );

    if (rxtxResult != pdPASS) {
      ESP_LOGE("SerialEngine", "Failed to create RXTX task: %d", rxtxResult);
      return false;
    }

    ESP_LOGI("SerialEngine", "RXTX task started successfully on Core 1");
    return true;
  }

  // Core 1 Message Queue Management
  bool initCore1MessageQueue() {
    if (core1MessageQueue == nullptr) {
      core1MessageQueue = xQueueCreate(CORE1_QUEUE_SIZE, sizeof(Core1Message));
      if (core1MessageQueue == nullptr) {
        ESP_LOGE("SerialEngine", "Failed to create Core 1 message queue");
        return false;
      }
      ESP_LOGI("SerialEngine", "Core 1 message queue initialized");
    }
    return true;
  }

  bool enqueueVolumeChangeForCore1(int volume, const char *deviceName = "") {
    if (!initCore1MessageQueue()) {
      return false;
    }

    Core1Message msg;
    msg.type = Core1Message::VOLUME_CHANGE;
    msg.timestamp = millis();
    msg.volumeData.volume = volume;
    strncpy(msg.volumeData.deviceName, deviceName,
            sizeof(msg.volumeData.deviceName) - 1);
    msg.volumeData.deviceName[sizeof(msg.volumeData.deviceName) - 1] = '\0';

    BaseType_t result = xQueueSend(core1MessageQueue, &msg, 0); // Non-blocking
    if (result == pdTRUE) {
      ESP_LOGI("SerialEngine",
               "Enqueued volume change for Core 1: device='%s', volume=%d",
               deviceName, volume);
      return true;
    } else {
      ESP_LOGW("SerialEngine", "Failed to enqueue volume change - queue full");
      return false;
    }
  }

  bool processCore1MessageQueue() {
    if (core1MessageQueue == nullptr) {
      return false;
    }

    bool processedMessages = false;
    Core1Message msg;

    while (xQueueReceive(core1MessageQueue, &msg, 0) == pdTRUE) {
      processedMessages = true;
      ESP_LOGI("SerialEngine", "Core 1 processing message type %d (Core %d)",
               msg.type, xPortGetCoreID());

      switch (msg.type) {
      case Core1Message::VOLUME_CHANGE: {
        // Create and send volume change message via proper messaging system
        auto volumeMsg = Messaging::Message::createVolumeChange(
            msg.volumeData.deviceName, msg.volumeData.volume, "");
        Messaging::sendMessage(volumeMsg);

        ESP_LOGI("SerialEngine",
                 "Core 1 sent volume change: device='%s', volume=%d",
                 msg.volumeData.deviceName, msg.volumeData.volume);
        break;
      }
      default:
        ESP_LOGW("SerialEngine", "Unknown Core 1 message type: %d", msg.type);
        break;
      }
    }

    return processedMessages;
  }

  void cleanupCore1MessageQueue() {
    if (core1MessageQueue) {
      vQueueDelete(core1MessageQueue);
      core1MessageQueue = nullptr;
      ESP_LOGI("SerialEngine", "Core 1 message queue cleaned up");
    }
  }

  // Send a message (now delegated to TX task for proper Core 1 processing)
  void send(const Message &msg) {
    if (!running) {
      ESP_LOGW("SerialEngine", "Serial engine not running");
      return;
    }

    // DEBUG: Check which core is calling send()
    int coreId = xPortGetCoreID();
    ESP_LOGI("SerialEngine",
             "=== DELEGATING MESSAGE FROM CORE %d TO TX TASK ===", coreId);

    ESP_LOGI("SerialEngine", "Type: %s", msg.type.c_str());
    ESP_LOGI("SerialEngine", "Device ID: %.50s", msg.deviceId.c_str());
    ESP_LOGI("SerialEngine", "Request ID: %.50s", msg.requestId.c_str());
    ESP_LOGI("SerialEngine", "Timestamp: %u", msg.timestamp);

    // Convert to JSON here (Core 0 context) and delegate the actual sending to
    // TX task
    String json = msg.toJson();

    ESP_LOGI("SerialEngine", "JSON Length: %d", json.length());
    ESP_LOGI("SerialEngine", "Delegating to TX task for transmission");

    sendRaw(json);
    stats.messagesSent++;
  }

  // Send raw string (for compatibility)
  void sendRaw(const String &data) {
    if (!running)
      return;

    ESP_LOGI("SerialEngine", "=== SENDING RAW DATA ===");
    ESP_LOGI("SerialEngine", "Raw data: %s", data.c_str());

    // Use the binary framer to encode the message - using class member buffer
    size_t frameLength = 0;
    std::vector<uint8_t> binaryFrame;

    if (framer.encodeMessage(data, frameBuffer, sizeof(frameBuffer),
                             frameLength)) {
      binaryFrame.assign(frameBuffer, frameBuffer + frameLength);
    }

    if (!binaryFrame.empty()) {
      ESP_LOGI("SerialEngine", "Binary frame size: %zu bytes",
               binaryFrame.size());

      // Print first few bytes of binary frame for debugging
      String hexStr = "";
      for (int i = 0; i < min((int)binaryFrame.size(), 32); i++) {
        hexStr += String(binaryFrame[i], HEX) + " ";
      }
      ESP_LOGI("SerialEngine", "Binary frame (first 32 bytes): %s",
               hexStr.c_str());

      // Send the binary frame via Arduino Serial
      size_t written = Serial.write(binaryFrame.data(), binaryFrame.size());
      Serial.flush();

      if (written != binaryFrame.size()) {
        ESP_LOGW("SerialEngine", "Failed to write complete frame: %zu/%zu",
                 written, binaryFrame.size());
      } else {
        ESP_LOGI("SerialEngine", "Successfully sent %zu bytes", written);
      }
    } else {
      ESP_LOGW("SerialEngine", "Failed to frame message");
    }
  }

  // Get statistics
  const Stats &getStats() const { return stats; }

  // Stop the engine
  void stop() {
    running = false;

    if (rxtxTaskHandle) {
      vTaskDelete(rxtxTaskHandle);
      rxtxTaskHandle = nullptr;
    }

    // Cleanup Core 1 message queue
    cleanupCore1MessageQueue();

    ESP_LOGI("SerialEngine", "RXTX task stopped");
  }

private:
  // Task wrapper
  static void rxtxTaskWrapper(void *param) {
    static_cast<SerialEngine *>(param)->rxtxTask();
  }

  // Consolidated RXTX task - handles both receiving and transmitting
  void rxtxTask() {
    uint8_t data[256];

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
            String testJson = "{\"messageType\":\"STATUS_MESSAGE\","
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

      // Handle outgoing messages (TX)
      // Check for Core 1 messages with a reasonable timeout
      bool hasMessages = processCore1MessageQueue();

      if (!hasMessages) {
        // No messages processed, short delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms sleep when no work
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

} // namespace Messaging
