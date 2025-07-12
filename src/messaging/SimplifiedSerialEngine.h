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
      ESP_LOGI("SerialEngine", "Initializing Arduino Serial at %d baud", SERIAL_BAUD_RATE);
      
      // Initialize Arduino Serial
      Serial.begin(SERIAL_BAUD_RATE);
      
      // Wait for Serial to be ready
      unsigned long startTime = millis();
      const unsigned long SERIAL_TIMEOUT_MS = 2000;  // Reduced timeout
      
      while (!Serial && (millis() - startTime) < SERIAL_TIMEOUT_MS) {
        delay(10);
      }
      
      if (!Serial) {
        ESP_LOGE("SerialEngine", "Failed to initialize Serial port after %lums", SERIAL_TIMEOUT_MS);
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
        8192,  // Stack size - increased from 4KB to 8KB for ArduinoJson operations
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

    // DEBUG: Check which core is calling send()
    int coreId = xPortGetCoreID();
    ESP_LOGI("SerialEngine", "=== SENDING MESSAGE FROM CORE %d ===", coreId);
    
    if (coreId == 1) {
      ESP_LOGW("SerialEngine", "WARNING: Send called from Core 1 (receive task) - potential stack issue!");
    }
    
    ESP_LOGI("SerialEngine", "Type: %d", msg.type);
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
    
    // Use the binary framer to encode the message
    std::vector<uint8_t> binaryFrame = framer.encodeMessage(data);
    
    if (!binaryFrame.empty()) {
      ESP_LOGI("SerialEngine", "Binary frame size: %zu bytes", binaryFrame.size());
      
      // Print first few bytes of binary frame for debugging
      String hexStr = "";
      for (int i = 0; i < min((int)binaryFrame.size(), 32); i++) {
        hexStr += String(binaryFrame[i], HEX) + " ";
      }
      ESP_LOGI("SerialEngine", "Binary frame (first 32 bytes): %s", hexStr.c_str());
      
      // Send the binary frame via Arduino Serial
      size_t written = Serial.write(binaryFrame.data(), binaryFrame.size());
      Serial.flush();
      
      if (written != binaryFrame.size()) {
        ESP_LOGW("SerialEngine", "Failed to write complete frame: %zu/%zu", written, binaryFrame.size());
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

    ESP_LOGI("SerialEngine", "=== RX TASK STARTED ON CORE %d ===", xPortGetCoreID());
    ESP_LOGI("SerialEngine", "Initial stack high water mark: %d bytes", 
             uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    
    int loopCount = 0;
    
    while (running) {
      // Check if data is available
      if (Serial.available()) {
        int len = 0;
        // Read available data
        while (Serial.available() && len < sizeof(data)) {
          data[len++] = Serial.read();
        }
        
        if (len > 0) {
          ESP_LOGI("SerialEngine", "=== RECEIVED RAW BYTES ===");
          ESP_LOGI("SerialEngine", "Length: %d bytes", len);
          
          // Print first few bytes for debugging
          String hexStr = "";
          for (int i = 0; i < min(len, 32); i++) {
            hexStr += String(data[i], HEX) + " ";
          }
          ESP_LOGI("SerialEngine", "Data (first 32 bytes): %s", hexStr.c_str());
          
          processIncomingData(data, len);
        }
      }
      
      // Periodic stack monitoring (every 100 loops)
      loopCount++;
      if (loopCount % 100 == 0) {
        ESP_LOGI("SerialEngine", "Stack high water mark: %d bytes (loop %d)", 
                 uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t), loopCount);
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
    ESP_LOGI("SerialEngine", "Processing %zu bytes through binary framer", length);
    
    // Use the binary protocol framer to decode messages
    std::vector<String> messages = framer.processIncomingBytes(data, length);

    ESP_LOGI("SerialEngine", "Binary framer returned %zu messages", messages.size());
    
    for (const String &jsonStr : messages) {
      if (!jsonStr.isEmpty()) {
        ESP_LOGI("SerialEngine", "=== RECEIVED RAW JSON ===");
        ESP_LOGI("SerialEngine", "JSON: %.300s", jsonStr.c_str());
        
        // LIGHTWEIGHT parsing to avoid stack overflow - just check if it's valid JSON
        bool isValidJson = jsonStr.startsWith("{") && jsonStr.endsWith("}") && jsonStr.length() > 10;
        
        if (isValidJson) {
          stats.messagesReceived++;
          
          // SIMPLE pattern matching to determine message type without full parsing
          String messageType = "UNKNOWN";
          if (jsonStr.indexOf("\"STATUS_MESSAGE\"") != -1) {
            messageType = "STATUS_MESSAGE";
          } else if (jsonStr.indexOf("\"GET_STATUS\"") != -1) {
            messageType = "GET_STATUS";
          } else if (jsonStr.indexOf("\"GET_ASSETS\"") != -1) {
            messageType = "GET_ASSETS";
          } else if (jsonStr.indexOf("\"ASSET_RESPONSE\"") != -1) {
            messageType = "ASSET_RESPONSE";
          } else if (jsonStr.indexOf("\"VOLUME_CHANGE\"") != -1) {
            messageType = "VOLUME_CHANGE";
          }
          
          ESP_LOGI("SerialEngine", "=== LIGHTWEIGHT MESSAGE ANALYSIS ===");
          ESP_LOGI("SerialEngine", "Detected Type: %s", messageType.c_str());
          ESP_LOGI("SerialEngine", "JSON Length: %d", jsonStr.length());
          ESP_LOGI("SerialEngine", "Valid JSON Structure: Yes");
          ESP_LOGI("SerialEngine", "=== DEBUG: NOT PARSING OR ROUTING ===");
          
          // Skip full parsing to avoid stack overflow on Core 1
          // Real parsing would happen on Core 0 when routing is enabled
          
        } else {
          stats.parseErrors++;
          ESP_LOGW("SerialEngine", "=== INVALID JSON STRUCTURE ===");
          ESP_LOGW("SerialEngine", "JSON does not look valid: %.100s", jsonStr.c_str());
        }
      }
    }
  }
};

// Global instance accessor for compatibility
inline SerialEngine &getSerialEngine() { return SerialEngine::getInstance(); }

} // namespace Messaging
