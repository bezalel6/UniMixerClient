#pragma once

#include "Message.h"
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
/**
 * SIMPLE SERIAL ENGINE
 * No transport abstraction. Just send and receive messages.
 */
class SerialEngine {
private:
  static SerialEngine *instance;

  // Serial configuration
  static const int SERIAL_BAUD_RATE = 115200;
  static const size_t RX_BUFFER_SIZE = 4096;

  // Task configuration
  TaskHandle_t rxTaskHandle = nullptr;
  bool running = false;

  // Frame buffer for encoding - shared across all send operations
  uint8_t frameBuffer[4096];

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

  // Start the receive task (call this after system is fully initialized)
  bool startReceiveTask() {
    if (rxTaskHandle != nullptr) {
      ESP_LOGW("SerialEngine", "Receive task already started");
      return true;
    }

    ESP_LOGI("SerialEngine", "Starting receive task on Core 1");

    BaseType_t result =
        xTaskCreatePinnedToCore(rxTaskWrapper, "SerialRx",
                                8192 * 2, // Stack size - increased from 4KB to
                                          // 8KB for ArduinoJson operations
                                this,
                                5, // Priority
                                &rxTaskHandle,
                                1 // Core 1
        );

    if (result != pdPASS) {
      ESP_LOGE("SerialEngine", "Failed to create receive task: %d", result);
      return false;
    }

    ESP_LOGI("SerialEngine", "Receive task started successfully");
    return true;
  }

  // Send a message
  void send(const Message &msg) {
    if (!running) {
      ESP_LOGW("SerialEngine", "Serial engine not running");
      return;
    }

    // DEBUG: Check which core is calling send()
    int coreId = xPortGetCoreID();
    ESP_LOGI("SerialEngine", "=== SENDING MESSAGE FROM CORE %d ===", coreId);

    if (coreId == 1) {
      ESP_LOGW("SerialEngine", "WARNING: Send called from Core 1 (receive "
                               "task) - potential stack issue!");
    }

    ESP_LOGI("SerialEngine", "Type: %s", msg.type.c_str());
    ESP_LOGI("SerialEngine", "Device ID: %.50s", msg.deviceId.c_str());
    ESP_LOGI("SerialEngine", "Request ID: %.50s", msg.requestId.c_str());
    ESP_LOGI("SerialEngine", "Timestamp: %u", msg.timestamp);
    ESP_LOGI("SerialEngine", "Converting to JSON...");

    String json = msg.toJson();

    ESP_LOGI("SerialEngine", "JSON Length: %d", json.length());
    ESP_LOGI("SerialEngine", "JSON: %.200s", json.c_str());
    ESP_LOGI("SerialEngine", "========================");

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
    if (rxTaskHandle) {
      vTaskDelete(rxTaskHandle);
      rxTaskHandle = nullptr;
    }
  }

private:
  // Task wrapper
  static void rxTaskWrapper(void *param) {
    static_cast<SerialEngine *>(param)->rxTask();
  }

  // Receive task
  void rxTask() {
    uint8_t data[256];

    ESP_LOGI("SerialEngine",
             "=== RX TASK STARTED ON CORE %d ===", xPortGetCoreID());

    while (running) {
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

      // Small delay to prevent tight loop
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI("SerialEngine", "=== RX TASK ENDED ===");
    ESP_LOGI("SerialEngine", "Final stack high water mark: %d bytes",
             uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
  }

  // Process incoming data
  void processIncomingData(const uint8_t *data, size_t length) {
    ESP_LOGI("SerialEngine", "=== PROCESSING INCOMING DATA ===");
    ESP_LOGI("SerialEngine", "Processing %zu bytes through binary framer",
             length);

    // Use the binary protocol framer to decode messages
    std::vector<String> messages = framer.processIncomingBytes(data, length);

    ESP_LOGI("SerialEngine", "Binary framer returned %zu messages",
             messages.size());

    for (const String &jsonStr : messages) {
      if (!jsonStr.isEmpty()) {
        ESP_LOGI("SerialEngine", "=== RECEIVED RAW JSON ===");
        ESP_LOGI("SerialEngine", "JSON: %s", jsonStr.c_str());

        // LIGHTWEIGHT parsing to avoid stack overflow - just check if it's
        // valid JSON
        bool isValidJson = jsonStr.startsWith("{") && jsonStr.endsWith("}") &&
                           jsonStr.length() > 10;

        ESP_LOGI("SerialEngine", "=== JSON VALIDATION DEBUG ===");
        ESP_LOGI("SerialEngine", "JSON length: %d", jsonStr.length());
        ESP_LOGI("SerialEngine", "Starts with {: %s",
                 jsonStr.startsWith("{") ? "true" : "false");
        ESP_LOGI("SerialEngine", "Ends with }: %s",
                 jsonStr.endsWith("}") ? "true" : "false");
        ESP_LOGI("SerialEngine", "First 50 chars: %.50s", jsonStr.c_str());
        ESP_LOGI("SerialEngine", "Last 50 chars: %s",
                 jsonStr.substring(jsonStr.length() - 50).c_str());

        if (isValidJson) {
          stats.messagesReceived++;
          auto parsed = Messaging::Message::fromJson(jsonStr);
          ESP_LOGI("SerialEngine", "=== PARSED MESSAGE DEBUG ===");
          ESP_LOGI("SerialEngine", "Message type: %s", parsed.type.c_str());
          ESP_LOGI("SerialEngine", "Device ID: %s", parsed.deviceId.c_str());
          ESP_LOGI("SerialEngine", "Full message: %s",
                   parsed.toString().c_str());

          // Route valid messages to handlers
          Messaging::MessageRouter::getInstance().route(parsed);
        } else {
          stats.parseErrors++;
          ESP_LOGW("SerialEngine", "=== INVALID JSON STRUCTURE ===");
          ESP_LOGW("SerialEngine", "JSON does not look valid: %.100s",
                   jsonStr.c_str());
        }
      }
    }
  }
};

// Global instance accessor for compatibility
inline SerialEngine &getSerialEngine() { return SerialEngine::getInstance(); }

} // namespace Messaging
