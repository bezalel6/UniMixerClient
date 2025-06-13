#include "../hardware/MqttManager.h"
#include "MessageBus.h"
#include <esp_log.h>
#include <vector>

static const char *TAG = "MqttTransport";

namespace Messaging::Transports {

// Handler storage for bridging modern to legacy handlers
static std::vector<std::pair<String, Hardware::Mqtt::Handler *>> handlerBridge;

// Convert modern ConnectionStatus to legacy and vice versa
static ConnectionStatus
ConvertMqttStatus(Hardware::Mqtt::ConnectionStatus mqttStatus) {
  switch (mqttStatus) {
  case Hardware::Mqtt::MQTT_STATUS_DISCONNECTED:
    return ConnectionStatus::Disconnected;
  case Hardware::Mqtt::MQTT_STATUS_CONNECTING:
    return ConnectionStatus::Connecting;
  case Hardware::Mqtt::MQTT_STATUS_CONNECTED:
    return ConnectionStatus::Connected;
  case Hardware::Mqtt::MQTT_STATUS_FAILED:
    return ConnectionStatus::Failed;
  case Hardware::Mqtt::MQTT_STATUS_ERROR:
    return ConnectionStatus::Error;
  default:
    return ConnectionStatus::Error;
  }
}

// Bridge function to convert legacy MQTT callback to modern callback
static void BridgeCallback(const char *topic, const char *payload) {
  // Find the modern handler associated with this topic
  for (const auto &pair : handlerBridge) {
    // Check if this handler matches the topic
    if (pair.second && strcmp(pair.second->subscribeTopic, topic) == 0) {
      ESP_LOGI(
          TAG,
          "Received MQTT message for handler '%s' - Topic: %s, Payload: %s",
          pair.first.c_str(), topic, payload);
      // We'll need to store the modern callback somewhere accessible
      // For now, we'll just log - this will be enhanced when handlers are
      // registered
      break;
    }
  }
}

// Modern MQTT transport implementation using lambdas
static Transport MqttTransport = {
    .Publish = [](const char *topic, const char *payload) -> bool {
      return Hardware::Mqtt::publish(topic, payload);
    },

    .PublishDelayed = [](const char *topic, const char *payload) -> bool {
      return Hardware::Mqtt::publishDelayed(topic, payload);
    },

    .IsConnected = []() -> bool { return Hardware::Mqtt::isConnected(); },

    .RegisterHandler = [](const Handler &handler) -> bool {
      ESP_LOGI(TAG, "Registering MQTT handler: %s", handler.Identifier.c_str());

      // Create legacy MQTT handler
      auto *legacyHandler = new Hardware::Mqtt::Handler();
      strncpy(legacyHandler->identifier, handler.Identifier.c_str(), 63);
      legacyHandler->identifier[63] = '\0';

      strncpy(legacyHandler->subscribeTopic, handler.SubscribeTopic.c_str(),
              127);
      legacyHandler->subscribeTopic[127] = '\0';

      strncpy(legacyHandler->publishTopic, handler.PublishTopic.c_str(), 127);
      legacyHandler->publishTopic[127] = '\0';

      // Store modern callback for later use
      legacyHandler->callback = [](const char *topic, const char *payload) {
        // Find the corresponding modern handler and call its callback
        for (const auto &pair : handlerBridge) {
          if (pair.first == String(topic) ||
              (pair.second &&
               strcmp(pair.second->subscribeTopic, topic) == 0)) {
            // For now, just log - we'll enhance this
            ESP_LOGI(TAG, "MQTT callback for topic: %s", topic);
            break;
          }
        }
      };

      legacyHandler->active = handler.Active;

      // Store the bridge mapping
      handlerBridge.emplace_back(handler.Identifier, legacyHandler);

      // Register with legacy MQTT manager
      bool result = Hardware::Mqtt::registerHandler(legacyHandler);

      if (!result) {
        // Clean up on failure
        handlerBridge.pop_back();
        delete legacyHandler;
      }

      return result;
    },

    .UnregisterHandler = [](const String &identifier) -> bool {
      ESP_LOGI(TAG, "Unregistering MQTT handler: %s", identifier.c_str());

      // Find and remove from bridge
      for (auto it = handlerBridge.begin(); it != handlerBridge.end(); ++it) {
        if (it->first == identifier) {
          Hardware::Mqtt::Handler *legacyHandler = it->second;

          // Unregister from legacy MQTT manager
          bool result =
              Hardware::Mqtt::unregisterHandler(legacyHandler->identifier);

          // Clean up
          delete legacyHandler;
          handlerBridge.erase(it);

          return result;
        }
      }

      return false;
    },

    .Update = []() -> void { Hardware::Mqtt::update(); },

    .GetStatus = []() -> ConnectionStatus {
      return ConvertMqttStatus(Hardware::Mqtt::getStatus());
    },

    .GetStatusString = []() -> const char * {
      return Hardware::Mqtt::getStatusString();
    },

    .Init = []() -> void {
      ESP_LOGI(TAG, "Initializing MQTT transport wrapper");
      // Initialize MQTT manager now that it's not done by NetworkManager
      Hardware::Mqtt::init();
      // Ensure our bridge is clean
      handlerBridge.clear();
    },

    .Deinit = []() -> void {
      ESP_LOGI(TAG, "Deinitializing MQTT transport wrapper");

      // Clean up all bridged handlers
      for (auto &pair : handlerBridge) {
        if (pair.second) {
          Hardware::Mqtt::unregisterHandler(pair.second->identifier);
          delete pair.second;
        }
      }
      handlerBridge.clear();

      // Deinitialize the MQTT manager now that we manage it
      Hardware::Mqtt::deinit();
    }};

Transport *GetMqttTransport() { return &MqttTransport; }

} // namespace Messaging::Transports