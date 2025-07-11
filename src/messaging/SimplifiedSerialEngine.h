#pragma once

#include "Message.h"
#include <Arduino.h>
#include <BinaryProtocol.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_log.h>

namespace Messaging {

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
      // Initialize Arduino Serial
      Serial.begin(SERIAL_BAUD_RATE);
      
      // Wait for Serial to be ready
      unsigned long startTime = millis();
      const unsigned long SERIAL_TIMEOUT_MS = 2000;  // Reduced timeout
      
      while (!Serial && (millis() - startTime) < SERIAL_TIMEOUT_MS) {
        delay(10);
      }
      
      if (!Serial) {
        ESP_LOGE("SerialEngine", "Failed to initialize Serial port");
        return false;
      }
      
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
    ESP_LOGI("SerialEngine", "Serial engine initialized (task creation delayed)");
    
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
    
    BaseType_t result = xTaskCreatePinnedToCore(
        rxTaskWrapper, 
        "SerialRx", 
        4096,  // Stack size
        this,
        5,     // Priority
        &rxTaskHandle,
        1      // Core 1
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

    String json = msg.toJson();
    sendRaw(json);
    stats.messagesSent++;
  }

  // Send raw string (for compatibility)
  void sendRaw(const String &data) {
    if (!running)
      return;

    // Use the binary framer to encode the message
    std::vector<uint8_t> binaryFrame = framer.encodeMessage(data);
    
    if (!binaryFrame.empty()) {
      // Send the binary frame via Arduino Serial
      size_t written = Serial.write(binaryFrame.data(), binaryFrame.size());
      Serial.flush();
      
      if (written != binaryFrame.size()) {
        ESP_LOGW("SerialEngine", "Failed to write complete frame");
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

    while (running) {
      // Check if data is available
      if (Serial.available()) {
        int len = 0;
        // Read available data
        while (Serial.available() && len < sizeof(data)) {
          data[len++] = Serial.read();
        }
        
        if (len > 0) {
          processIncomingData(data, len);
        }
      }
      
      // Small delay to prevent tight loop
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  // Process incoming data
  void processIncomingData(const uint8_t *data, size_t length) {
    // Use the binary protocol framer to decode messages
    std::vector<String> messages = framer.processIncomingBytes(data, length);

    for (const String &jsonStr : messages) {
      if (!jsonStr.isEmpty()) {
        // Parse and route the message
        Message msg = Message::fromJson(jsonStr);
        if (msg.isValid()) {
          stats.messagesReceived++;
          MessageRouter::getInstance().route(msg);
        } else {
          stats.parseErrors++;
          ESP_LOGW("SerialEngine", "Failed to parse message: %.100s",
                   jsonStr.c_str());
        }
      }
    }
  }
};

// Global instance accessor for compatibility
inline SerialEngine &getSerialEngine() { return SerialEngine::getInstance(); }

} // namespace Messaging
