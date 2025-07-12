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
    0x7E, 0xBA, 0x01, 0x00, 0x00, 0x41, 0xB7, 0x01, 0x7B, 0x22, 0x6D, 0x65,
    0x73, 0x73, 0x61, 0x67, 0x65, 0x54, 0x79, 0x70, 0x65, 0x22, 0x3A, 0x32,
    0x2C, 0x22, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x49, 0x64, 0x22, 0x3A,
    0x22, 0x54, 0x48, 0x49, 0x4E, 0x4B, 0x49, 0x4E, 0x41, 0x54, 0x4F, 0x52,
    0x22, 0x2C, 0x22, 0x74, 0x69, 0x6D, 0x65, 0x73, 0x74, 0x61, 0x6D, 0x70,
    0x22, 0x3A, 0x31, 0x37, 0x35, 0x32, 0x32, 0x38, 0x33, 0x36, 0x39, 0x32,
    0x32, 0x34, 0x36, 0x2C, 0x22, 0x61, 0x63, 0x74, 0x69, 0x76, 0x65, 0x53,
    0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x43, 0x6F, 0x75, 0x6E, 0x74, 0x22,
    0x3A, 0x31, 0x2C, 0x22, 0x73, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x73,
    0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x70, 0x72, 0x6F, 0x63, 0x65, 0x73, 0x73,
    0x49, 0x64, 0x22, 0x3A, 0x31, 0x36, 0x32, 0x34, 0x30, 0x2C, 0x22, 0x70,
    0x72, 0x6F, 0x63, 0x65, 0x73, 0x73, 0x4E, 0x61, 0x6D, 0x65, 0x22, 0x3A,
    0x22, 0x63, 0x68, 0x72, 0x6F, 0x6D, 0x65, 0x22, 0x2C, 0x22, 0x64, 0x69,
    0x73, 0x70, 0x6C, 0x61, 0x79, 0x4E, 0x61, 0x6D, 0x65, 0x22, 0x3A, 0x22,
    0x22, 0x2C, 0x22, 0x76, 0x6F, 0x6C, 0x75, 0x6D, 0x65, 0x22, 0x3A, 0x31,
    0x2C, 0x22, 0x69, 0x73, 0x4D, 0x75, 0x74, 0x65, 0x64, 0x22, 0x3A, 0x66,
    0x61, 0x6C, 0x73, 0x65, 0x2C, 0x22, 0x73, 0x74, 0x61, 0x74, 0x65, 0x22,
    0x3A, 0x22, 0x41, 0x75, 0x64, 0x69, 0x6F, 0x53, 0x65, 0x73, 0x73, 0x69,
    0x6F, 0x6E, 0x53, 0x74, 0x61, 0x74, 0x65, 0x41, 0x63, 0x74, 0x69, 0x76,
    0x65, 0x22, 0x7D, 0x5D, 0x5D, 0x2C, 0x22, 0x64, 0x65, 0x66, 0x61, 0x75,
    0x6C, 0x74, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x22, 0x3A, 0x7B, 0x22,
    0x66, 0x72, 0x69, 0x65, 0x6E, 0x64, 0x6C, 0x79, 0x4E, 0x61, 0x6D, 0x65,
    0x22, 0x3A, 0x22, 0x53, 0x70, 0x65, 0x61, 0x6B, 0x65, 0x72, 0x73, 0x20,
    0x28, 0x53, 0x74, 0x65, 0x61, 0x6C, 0x74, 0x68, 0x20, 0x37, 0x30, 0x30,
    0x58, 0x20, 0x47, 0x65, 0x6E, 0x20, 0x33, 0x29, 0x22, 0x2C, 0x22, 0x76,
    0x6F, 0x6C, 0x75, 0x6D, 0x65, 0x22, 0x3A, 0x30, 0x2E, 0x39, 0x2C, 0x22,
    0x69, 0x73, 0x4D, 0x75, 0x74, 0x65, 0x64, 0x22, 0x3A, 0x66, 0x61, 0x6C,
    0x73, 0x65, 0x2C, 0x22, 0x64, 0x61, 0x74, 0x61, 0x46, 0x6C, 0x6F, 0x77,
    0x22, 0x3A, 0x22, 0x52, 0x65, 0x6E, 0x64, 0x65, 0x72, 0x22, 0x2C, 0x22,
    0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x52, 0x6F, 0x6C, 0x65, 0x22, 0x3A,
    0x22, 0x43, 0x6F, 0x6E, 0x73, 0x6F, 0x6C, 0x65, 0x22, 0x7D, 0x5D, 0x2C,
    0x22, 0x72, 0x65, 0x61, 0x73, 0x6F, 0x6E, 0x22, 0x3A, 0x22, 0x53, 0x65,
    0x73, 0x73, 0x69, 0x6F, 0x6E, 0x43, 0x68, 0x61, 0x6E, 0x67, 0x65, 0x22,
    0x2C, 0x22, 0x6F, 0x72, 0x69, 0x67, 0x69, 0x6E, 0x61, 0x74, 0x69, 0x6E,
    0x67, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x49, 0x64, 0x22, 0x3A,
    0x6E, 0x75, 0x6C, 0x6C, 0x2C, 0x22, 0x6F, 0x72, 0x69, 0x67, 0x69, 0x6E,
    0x61, 0x74, 0x69, 0x6E, 0x67, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x49,
    0x64, 0x22, 0x3A, 0x6E, 0x75, 0x6C, 0x6C, 0x7D, 0x5D, 0x7F};
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

    BaseType_t result = xTaskCreatePinnedToCore(rxTaskWrapper, "SerialRx",
                                                4096 * 2, // Stack size
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

    // DEBUG: Print outgoing message details
    ESP_LOGI("SerialEngine", "=== SENDING MESSAGE ===");
    ESP_LOGI("SerialEngine", "Type: %s (%d)", msg.typeToString(), msg.type);
    ESP_LOGI("SerialEngine", "Device ID: %s", msg.deviceId.c_str());
    ESP_LOGI("SerialEngine", "Request ID: %s", msg.requestId.c_str());
    String json = msg.toJson();
    ESP_LOGI("SerialEngine", "JSON: %s", json.c_str());
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

    // Use the binary framer to encode the message
    std::vector<uint8_t> binaryFrame = framer.encodeMessage(data);

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
            ESP_LOGI("SerialEngine", "Test payload (first 64 bytes): %s",
                     testHex);

            processIncomingData(testPayload, payloadSize);
          } else {
            processIncomingData(data, len);
          }
        }
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI("SerialEngine", "=== RX TASK ENDED ===");
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

        // Parse the message for debugging
        Message msg = Message::fromJson(jsonStr);
        if (msg.isValid()) {
          stats.messagesReceived++;

          // DEBUG: Print parsed message details instead of routing
          ESP_LOGI("SerialEngine", "=== PARSED MESSAGE DETAILS ===");
          ESP_LOGI("SerialEngine", "Type: %s (%d)", msg.typeToString(),
                   msg.type);
          ESP_LOGI("SerialEngine", "Device ID: %s", msg.deviceId.c_str());
          ESP_LOGI("SerialEngine", "Request ID: %s", msg.requestId.c_str());
          ESP_LOGI("SerialEngine", "Timestamp: %u", msg.timestamp);

          // Print type-specific data
          switch (msg.type) {
          case Message::AUDIO_STATUS:
            ESP_LOGI("SerialEngine", "Audio Data:");
            ESP_LOGI("SerialEngine", "  Process: %s",
                     msg.data.audio.processName);
            ESP_LOGI("SerialEngine", "  Volume: %d", msg.data.audio.volume);
            ESP_LOGI("SerialEngine", "  Muted: %s",
                     msg.data.audio.isMuted ? "Yes" : "No");
            ESP_LOGI("SerialEngine", "  Default Device: %s",
                     msg.data.audio.defaultDeviceName);
            ESP_LOGI("SerialEngine", "  Active Sessions: %d",
                     msg.data.audio.activeSessionCount);
            break;

          case Message::ASSET_REQUEST:
            ESP_LOGI("SerialEngine", "Asset Request:");
            ESP_LOGI("SerialEngine", "  Process: %s",
                     msg.data.asset.processName);
            break;

          case Message::ASSET_RESPONSE:
            ESP_LOGI("SerialEngine", "Asset Response:");
            ESP_LOGI("SerialEngine", "  Process: %s",
                     msg.data.asset.processName);
            ESP_LOGI("SerialEngine", "  Success: %s",
                     msg.data.asset.success ? "Yes" : "No");
            ESP_LOGI("SerialEngine", "  Error: %s",
                     msg.data.asset.errorMessage);
            ESP_LOGI("SerialEngine", "  Size: %dx%d", msg.data.asset.width,
                     msg.data.asset.height);
            ESP_LOGI("SerialEngine", "  Format: %s", msg.data.asset.format);
            ESP_LOGI("SerialEngine", "  Data Length: %zu",
                     strlen(msg.data.asset.assetDataBase64));
            break;

          case Message::VOLUME_CHANGE:
          case Message::SET_VOLUME:
            ESP_LOGI("SerialEngine", "Volume Data:");
            ESP_LOGI("SerialEngine", "  Process: %s",
                     msg.data.volume.processName);
            ESP_LOGI("SerialEngine", "  Volume: %d", msg.data.volume.volume);
            ESP_LOGI("SerialEngine", "  Target: %s", msg.data.volume.target);
            break;

          default:
            ESP_LOGI("SerialEngine", "No additional data for message type");
            break;
          }

          ESP_LOGI("SerialEngine", "=== DEBUG: NOT ROUTING MESSAGE ===");
          // MessageRouter::getInstance().route(msg); // DISABLED FOR DEBUGGING

        } else {
          stats.parseErrors++;
          ESP_LOGW("SerialEngine", "=== PARSE ERROR ===");
          ESP_LOGW("SerialEngine", "Failed to parse JSON: %.200s",
                   jsonStr.c_str());
        }
      }
    }
  }
};

// Global instance accessor for compatibility
inline SerialEngine &getSerialEngine() { return SerialEngine::getInstance(); }

} // namespace Messaging
