#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include <Arduino.h>
#include <esp_log.h>

static const char *TAG = "MessagingInit";

namespace Messaging {

// Global initialization function for the brutal messaging system
bool initMessaging() {
  ESP_LOGI(TAG, "=== BRUTAL MESSAGING INIT START ===");
  ESP_LOGI(TAG, "Current free heap: %d", ESP.getFreeHeap());
  ESP_LOGI(TAG, "Current stack watermark: %d", uxTaskGetStackHighWaterMark(NULL));
  
  // Initialize the serial engine - that's it!
  ESP_LOGI(TAG, "Creating SerialEngine instance...");
  SerialEngine& engine = SerialEngine::getInstance();
  
  ESP_LOGI(TAG, "Calling SerialEngine init...");
  bool success = engine.init();

  if (success) {
    ESP_LOGI(TAG,
             "Messaging system initialized - no abstractions, just serial");
    ESP_LOGI(TAG, "Free heap after init: %d", ESP.getFreeHeap());
    
    // Start the receive task after a small delay to ensure system stability
    ESP_LOGI(TAG, "Starting receive task after delay...");
    delay(100);
    
    if (!engine.startReceiveTask()) {
      ESP_LOGE(TAG, "Failed to start receive task");
      success = false;
    }
  } else {
    ESP_LOGE(TAG, "Failed to initialize serial engine");
  }
  
  ESP_LOGI(TAG, "=== BRUTAL MESSAGING INIT END ===");
  return success;
}

// Shutdown function
void shutdownMessaging() {
  ESP_LOGI(TAG, "Shutting down messaging system");
  SerialEngine::getInstance().stop();
}

// Get status
String getMessagingStatus() {
  const auto &stats = SerialEngine::getInstance().getStats();

  String status = "BRUTAL Messaging Status:\n";
  status += "- Messages received: " + String(stats.messagesReceived) + "\n";
  status += "- Messages sent: " + String(stats.messagesSent) + "\n";
  status += "- Parse errors: " + String(stats.parseErrors) + "\n";
  status += "- Framing errors: " + String(stats.framingErrors) + "\n";
  status += "- Active handlers: " +
            String(MessageRouter::getInstance().getHandlerCount()) + "\n";

  return status;
}

} // namespace Messaging
