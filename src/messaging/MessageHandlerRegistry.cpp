#include "MessageHandlerRegistry.h"
#include "../application/AudioStatusManager.h"
#include <esp_log.h>
#include "DebugUtils.h"
#include <ui/ui.h>
static const char* TAG = "MessageHandlerRegistry";

namespace Messaging {

// Static member definition
std::vector<String> MessageHandlerRegistry::registeredHandlers;

bool MessageHandlerRegistry::RegisterAllHandlers() {
    ESP_LOGI(TAG, "Registering all message handlers...");

    bool success = true;

    // Register audio status handlers
    if (!RegisterAudioStatusHandlers()) {
        ESP_LOGE(TAG, "Failed to register audio status handlers");
        success = false;
    }

    // Future handlers can be added here:
    // if (!RegisterOtaHandlers()) { success = false; }
    // if (!RegisterNetworkHandlers()) { success = false; }

    ESP_LOGI(TAG, "Message handler registration %s (%d handlers registered)",
             success ? "successful" : "failed", registeredHandlers.size());

    return success;
}

void MessageHandlerRegistry::UnregisterAllHandlers() {
    ESP_LOGI(TAG, "Unregistering all message handlers...");

    for (const String& identifier : registeredHandlers) {
        MessageBus::UnregisterHandler(identifier);
        ESP_LOGI(TAG, "Unregistered handler: %s", identifier.c_str());
    }

    registeredHandlers.clear();
    ESP_LOGI(TAG, "All message handlers unregistered");
}

bool MessageHandlerRegistry::RegisterAudioStatusHandlers() {
    ESP_LOGI(TAG, "Registering audio status handlers");

    // Register typed audio status response handler
    String handlerIdentifier = "TypedAudioStatusHandler";
    bool success = MessageBus::RegisterAudioStatusHandler(handlerIdentifier, HandleAudioStatusResponse);

    if (success) {
        registeredHandlers.push_back(handlerIdentifier);
        ESP_LOGI(TAG, "Successfully registered audio status handler: %s", handlerIdentifier.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to register audio status handler: %s", handlerIdentifier.c_str());
    }

    return success;
}

// Message handler implementations
void MessageHandlerRegistry::HandleAudioStatusResponse(const Messages::AudioStatusResponse& message) {
    ESP_LOGI(TAG, "Received typed audio status response with %d sessions and %s default device",
             message.sessions.size(), message.hasDefaultDevice ? "a" : "no");

    if (message.reason == Protocol::REASON_UPDATE_RESPONSE && message.originatingDeviceId == String(Protocol::MY_DEVICE_ID)) {
        return;
    }
    // Convert typed message to AudioStatus structure expected by AudioStatusManager

    Application::Audio::AudioStatus status;
    status.timestamp = message.timestamp;
    status.audioLevels = message.sessions;
    status.defaultDevice = message.defaultDevice;
    status.hasDefaultDevice = message.hasDefaultDevice;

    // Forward to AudioStatusManager using existing interface
    Application::Audio::StatusManager::onAudioStatusReceived(status);
}

}  // namespace Messaging