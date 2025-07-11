#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include <esp_log.h>

static const char *TAG = "MessagingInit";

namespace Messaging {

// Global initialization function for the brutal messaging system
bool initMessaging() {
  ESP_LOGI(TAG, "Initializing BRUTAL messaging system");

  // Initialize the serial engine - that's it!
  bool success = SerialEngine::getInstance().init();

  if (success) {
    ESP_LOGI(TAG,
             "Messaging system initialized - no abstractions, just serial");
  } else {
    ESP_LOGE(TAG, "Failed to initialize serial engine");
  }

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
