#include "AudioStatusManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include <ui/ui.h>

static const char* TAG = "AudioStatusManager";

// Macros to reduce repetition in device actions and messaging publishing
#define AUDIO_DEVICE_ACTION_PROLOGUE(action_name)                        \
    if (!initialized) {                                                  \
        ESP_LOGW(TAG, "AudioStatusManager not initialized");             \
        return;                                                          \
    }                                                                    \
    if (selectedDevice.isEmpty()) {                                      \
        ESP_LOGW(TAG, "No device selected for " action_name " control"); \
        return;                                                          \
    }

#define AUDIO_MESSAGE_COMMAND_BASE(message_action)                       \
    if (!Messaging::MessageBus::IsConnected()) {                         \
        ESP_LOGW(TAG, "Cannot publish command: No transport connected"); \
        return;                                                          \
    }                                                                    \
    JsonDocument doc;                                                    \
    doc["messageType"] = Messaging::Protocol::MSG_TYPE_AUDIO_MIX_UPDATE; \
    doc["timestamp"] = String(Hardware::Device::getMillis());            \
    doc["messageId"] = String(Hardware::Device::getMillis());            \
    JsonArray updates = doc["updates"].to<JsonArray>();                  \
    JsonObject update = updates.add<JsonObject>();                       \
    update["processName"] = deviceName;                                  \
    update["action"] = message_action;

#define AUDIO_PUBLISH_COMMAND_FINISH(action_name)                                                                    \
    String jsonPayload;                                                                                              \
    serializeJson(doc, jsonPayload);                                                                                 \
    bool published = Messaging::MessageBus::Publish(Messaging::Protocol::TOPIC_AUDIO_REQUESTS, jsonPayload.c_str()); \
    if (published) {                                                                                                 \
        ESP_LOGI(TAG, "Published " action_name " command for %s", deviceName.c_str());                               \
    } else {                                                                                                         \
        ESP_LOGE(TAG, "Failed to publish " action_name " command");                                                  \
    }

namespace Application {
namespace Audio {

// Static member definitions
std::vector<AudioLevel> StatusManager::audioLevels;
Messaging::Handler StatusManager::audioStatusHandler;
unsigned long StatusManager::lastUpdateTime = 0;
bool StatusManager::initialized = false;
String StatusManager::selectedDevice = "";
bool StatusManager::suppressArcEvents = false;

bool StatusManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioStatusManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioStatusManager");

    // Clear existing data
    audioLevels.clear();
    lastUpdateTime = Hardware::Device::getMillis();

    // Initialize and register message handler
    initializeAudioStatusHandler();
    if (!Messaging::MessageBus::RegisterHandler(audioStatusHandler)) {
        ESP_LOGE(TAG, "Failed to register audio status message handler");
        return false;
    }
    initialized = true;
    ESP_LOGI(TAG, "AudioStatusManager initialized successfully");
    return true;
}

void StatusManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioStatusManager");

    // Unregister message handler
    Messaging::MessageBus::UnregisterHandler(audioStatusHandler.Identifier);

    // Clear data
    audioLevels.clear();
    initialized = false;
}

void StatusManager::updateAudioLevel(const String& processName, int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    unsigned long now = Hardware::Device::getMillis();

    // Find existing process or create new entry
    for (auto& level : audioLevels) {
        if (level.processName == processName) {
            level.volume = volume;
            level.lastUpdate = now;
            return;
        }
    }

    // Add new process
    AudioLevel newLevel;
    newLevel.processName = processName;
    newLevel.volume = volume;
    newLevel.lastUpdate = now;
    audioLevels.push_back(newLevel);

    lastUpdateTime = now;

    ESP_LOGI(TAG, "Updated audio level - Process: %s, Volume: %d",
             processName.c_str(), volume);
}

std::vector<AudioLevel> StatusManager::getAllAudioLevels(void) {
    return audioLevels;
}

AudioLevel* StatusManager::getAudioLevel(const String& processName) {
    for (auto& level : audioLevels) {
        if (level.processName == processName) {
            return &level;
        }
    }
    return nullptr;
}

int StatusManager::getActiveProcessCount(void) {
    return audioLevels.size();
}

int StatusManager::getTotalVolume(void) {
    int total = 0;
    for (const auto& level : audioLevels) {
        total += level.volume;
    }
    return total;
}

AudioLevel StatusManager::getHighestVolumeProcess(void) {
    AudioLevel highest;
    highest.processName = "";
    highest.volume = 0;
    highest.lastUpdate = 0;

    for (const auto& level : audioLevels) {
        if (level.volume > highest.volume) {
            highest = level;
        }
    }

    return highest;
}

void StatusManager::onAudioStatusReceived(const std::vector<AudioLevel>& levels) {
    ESP_LOGI(TAG, "Received audio status update with %d processes", levels.size());

    // Update all levels
    for (const auto& level : levels) {
        updateAudioLevel(level.processName, level.volume);
    }

    // Auto-select first device if none is selected and devices are available
    if (selectedDevice.isEmpty() && !audioLevels.empty()) {
        setSelectedDevice(audioLevels[0].processName);
        ESP_LOGI(TAG, "Auto-selected first device: %s", audioLevels[0].processName.c_str());
    }

    // Update UI dropdowns with new audio device list
    updateAudioDeviceDropdowns();

    // Sync volume arc if a device is selected
    if (!selectedDevice.isEmpty()) {
        syncVolumeArcWithSelectedDevice();
    }
}

// UI Update functions

void StatusManager::updateAudioDeviceDropdowns(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized, cannot update UI");
        return;
    }

    // Build the options string for the dropdowns
    String optionsString = buildAudioDeviceOptionsString();

    // Update all audio device dropdowns
    Display::updateDropdownOptions(ui_selectAudioDevice, optionsString.c_str());
    Display::updateDropdownOptions(ui_selectAudioDevice1, optionsString.c_str());
    Display::updateDropdownOptions(ui_selectAudioDevice2, optionsString.c_str());

    ESP_LOGI(TAG, "Updated audio device dropdowns with %d devices", audioLevels.size());
}

String StatusManager::buildAudioDeviceOptionsString(void) {
    if (audioLevels.empty()) {
        return "None";
    }

    String options = "";
    bool first = true;

    // Add each audio process as an option
    for (const auto& level : audioLevels) {
        if (!first) {
            options += "\n";
        }

        // Format: "ProcessName (Volume%)"
        options += level.processName;
        options += " (";
        options += String(level.volume);
        options += "%)";

        first = false;
    }

    return options;
}

String StatusManager::getSelectedAudioDevice(lv_obj_t* dropdown) {
    if (dropdown == NULL) {
        ESP_LOGW(TAG, "getSelectedAudioDevice: Invalid dropdown parameter");
        return "";
    }

    // Get the selected option index
    uint16_t selectedIndex = lv_dropdown_get_selected(dropdown);

    // Get the selected text
    char selectedText[256];
    lv_dropdown_get_selected_str(dropdown, selectedText, sizeof(selectedText));

    // Extract just the process name (before the volume indicator)
    String selectedString = String(selectedText);
    int volumeStart = selectedString.indexOf(" (");
    if (volumeStart > 0) {
        return selectedString.substring(0, volumeStart);
    }

    return selectedString;
}

// Selected device state management

void StatusManager::setSelectedDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    selectedDevice = deviceName;
    ESP_LOGI(TAG, "Selected device changed to: %s", deviceName.c_str());

    // Sync volume arc with the new selection
    syncVolumeArcWithSelectedDevice();
}

String StatusManager::getSelectedDevice(void) {
    return selectedDevice;
}

void StatusManager::syncVolumeArcWithSelectedDevice(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    if (selectedDevice.isEmpty()) {
        // No device selected, set arc to 0
        suppressArcEvents = true;
        lv_arc_set_value(ui_volumeSlider, 0);
        updateVolumeArcLabel(0);
        suppressArcEvents = false;
        return;
    }

    // Find the selected device and sync its volume
    AudioLevel* level = getAudioLevel(selectedDevice);
    if (level != nullptr) {
        suppressArcEvents = true;
        lv_arc_set_value(ui_volumeSlider, level->volume);
        updateVolumeArcLabel(level->volume);
        suppressArcEvents = false;
        ESP_LOGI(TAG, "Synced volume arc to %d for device: %s",
                 level->volume, selectedDevice.c_str());
    } else {
        ESP_LOGW(TAG, "Selected device '%s' not found in audio levels", selectedDevice.c_str());
        suppressArcEvents = true;
        lv_arc_set_value(ui_volumeSlider, 0);
        updateVolumeArcLabel(0);
        suppressArcEvents = false;
    }
}

void StatusManager::updateVolumeArcLabel(int volume) {
    if (ui_volumeSliderLbl) {
        char labelText[16];
        snprintf(labelText, sizeof(labelText), "%d%%", volume);
        lv_label_set_text(ui_volumeSliderLbl, labelText);
    }
}

// Volume control

void StatusManager::setSelectedDeviceVolume(int volume) {
    AUDIO_DEVICE_ACTION_PROLOGUE("volume control")

    // Clamp volume to valid range
    volume = constrain(volume, 0, 100);

    // Update local audio level immediately for responsive UI
    updateAudioLevel(selectedDevice, volume);

    // Update the volume arc label
    updateVolumeArcLabel(volume);

    // Publish volume change command via messaging
    publishVolumeChangeCommand(selectedDevice, volume);

    ESP_LOGI(TAG, "Set volume to %d for device: %s", volume, selectedDevice.c_str());
}

void StatusManager::muteSelectedDevice(void) {
    AUDIO_DEVICE_ACTION_PROLOGUE("mute")
    publishMuteCommand(selectedDevice);
    ESP_LOGI(TAG, "Muted device: %s", selectedDevice.c_str());
}

void StatusManager::unmuteSelectedDevice(void) {
    AUDIO_DEVICE_ACTION_PROLOGUE("unmute")
    publishUnmuteCommand(selectedDevice);
    ESP_LOGI(TAG, "Unmuted device: %s", selectedDevice.c_str());
}

// Message publishing methods using new messaging system

void StatusManager::publishVolumeChangeCommand(const String& deviceName, int volume) {
    AUDIO_MESSAGE_COMMAND_BASE(Messaging::Protocol::AUDIO_ACTION_SET_VOLUME)
    update["volume"] = volume;
    AUDIO_PUBLISH_COMMAND_FINISH("volume change")
}

void StatusManager::publishMuteCommand(const String& deviceName) {
    AUDIO_MESSAGE_COMMAND_BASE(Messaging::Protocol::AUDIO_ACTION_MUTE)
    AUDIO_PUBLISH_COMMAND_FINISH("mute")
}

void StatusManager::publishUnmuteCommand(const String& deviceName) {
    AUDIO_MESSAGE_COMMAND_BASE(Messaging::Protocol::AUDIO_ACTION_UNMUTE)
    AUDIO_PUBLISH_COMMAND_FINISH("unmute")
}

bool StatusManager::isSuppressingArcEvents(void) {
    return suppressArcEvents;
}

// Message publishing methods

void StatusManager::publishAudioStatusRequest(bool delayed) {
    if (!delayed && !Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish audio status request: No transport connected");
        return;
    }

    // Create JSON message using AudioStatusRequestMessage format
    JsonDocument doc;
    doc["messageType"] = Messaging::Protocol::MSG_TYPE_AUDIO_STATUS_REQUEST;
    doc["timestamp"] = String(Hardware::Device::getMillis());
    doc["messageId"] = String(Hardware::Device::getMillis());  // Simple ID based on millis

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the message (delayed or immediate)
    bool published;
    if (delayed) {
        published = Messaging::MessageBus::PublishDelayed(Messaging::Protocol::TOPIC_AUDIO_REQUESTS, jsonPayload.c_str());
        if (published) {
            ESP_LOGI(TAG, "Queued delayed audio status request");
        } else {
            ESP_LOGE(TAG, "Failed to queue delayed audio status request");
        }
    } else {
        published = Messaging::MessageBus::Publish(Messaging::Protocol::TOPIC_AUDIO_REQUESTS, jsonPayload.c_str());
        if (published) {
            ESP_LOGI(TAG, "Published audio status request");
        } else {
            ESP_LOGE(TAG, "Failed to publish audio status request");
        }
    }
}

// Private methods

void StatusManager::initializeAudioStatusHandler(void) {
    audioStatusHandler.Identifier = "AudioStatusManager";
    audioStatusHandler.SubscribeTopic = Messaging::Protocol::TOPIC_AUDIO_STATUS;
    audioStatusHandler.PublishTopic = Messaging::Protocol::TOPIC_AUDIO_REQUESTS;
    audioStatusHandler.Callback = audioStatusMessageHandler;
    audioStatusHandler.Active = true;
}

void StatusManager::audioStatusMessageHandler(const char* topic, const char* payload) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized, ignoring message");
        return;
    }

    std::vector<AudioLevel> levels = parseAudioStatusJson(payload);

    if (levels.empty()) {
        ESP_LOGE(TAG, "Failed to parse audio status JSON or no valid data found");
        return;
    }

    // Process the received audio levels
    onAudioStatusReceived(levels);
}

std::vector<AudioLevel> StatusManager::parseAudioStatusJson(const char* jsonPayload) {
    std::vector<AudioLevel> result;

    if (!jsonPayload) {
        ESP_LOGE(TAG, "Invalid JSON payload");
        return result;
    }

    // Create a JSON document
    JsonDocument doc;

    // Parse the JSON
    DeserializationError error = deserializeJson(doc, jsonPayload);

    if (error) {
        ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
        return result;
    }

    // Check if the root is an object
    if (!doc.is<JsonObject>()) {
        ESP_LOGE(TAG, "JSON root is not an object");
        return result;
    }

    JsonObject root = doc.as<JsonObject>();
    unsigned long now = Hardware::Device::getMillis();

    // Iterate through all key-value pairs
    for (JsonPair kv : root) {
        const char* processName = kv.key().c_str();
        JsonVariant value = kv.value();

        // Skip metadata fields
        if (strcmp(processName, "timestamp") == 0 ||
            strcmp(processName, "messageType") == 0 ||
            strcmp(processName, "messageId") == 0) {
            continue;
        }

        // Check if value is a number (volume level)
        if (value.is<int>()) {
            int volume = value.as<int>();

            // Validate volume range
            if (volume >= 0 && volume <= 100) {
                AudioLevel level;
                level.processName = String(processName);
                level.volume = volume;
                level.lastUpdate = now;

                result.push_back(level);
                ESP_LOGI(TAG, "Parsed audio level: %s = %d", processName, volume);
            } else {
                ESP_LOGW(TAG, "Invalid volume level for %s: %d", processName, volume);
            }
        } else {
            ESP_LOGW(TAG, "Non-numeric value for process %s", processName);
        }
    }

    ESP_LOGI(TAG, "Parsed %d audio levels from JSON", result.size());
    return result;
}

}  // namespace Audio
}  // namespace Application