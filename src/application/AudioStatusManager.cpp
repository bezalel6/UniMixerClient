#include "AudioStatusManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include <ui/ui.h>

static const char* TAG = "AudioStatusManager";

namespace Application {
namespace Audio {

// Static member definitions
std::vector<AudioLevel> StatusManager::audioLevels;
Hardware::Mqtt::Handler StatusManager::audioStatusHandler;
unsigned long StatusManager::lastUpdateTime = 0;
bool StatusManager::initialized = false;
String StatusManager::selectedDevice = "";
bool StatusManager::suppressSliderEvents = false;

bool StatusManager::init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "AudioStatusManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioStatusManager");

    // Clear existing data
    audioLevels.clear();
    lastUpdateTime = Hardware::Device::getMillis();

    // Initialize and register MQTT handler
    initializeAudioStatusHandler();
    if (!Hardware::Mqtt::registerHandler(&audioStatusHandler)) {
        ESP_LOGE(TAG, "Failed to register audio status MQTT handler");
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "AudioStatusManager initialized successfully");
    return true;
}

void StatusManager::deinit(void) {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioStatusManager");

    // Unregister MQTT handler
    Hardware::Mqtt::unregisterHandler(audioStatusHandler.identifier);

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

    // Update UI dropdowns with new audio device list
    updateAudioDeviceDropdowns();

    // Sync volume slider if a device is selected
    if (!selectedDevice.isEmpty()) {
        syncVolumeSliderWithSelectedDevice();
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

    // Sync volume slider with the new selection
    syncVolumeSliderWithSelectedDevice();
}

String StatusManager::getSelectedDevice(void) {
    return selectedDevice;
}

void StatusManager::syncVolumeSliderWithSelectedDevice(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    if (selectedDevice.isEmpty()) {
        // No device selected, set slider to 0
        suppressSliderEvents = true;
        lv_slider_set_value(ui_volumeSlider, 0, LV_ANIM_OFF);
        updateVolumeSliderLabel(0);
        suppressSliderEvents = false;
        return;
    }

    // Find the selected device and sync its volume
    AudioLevel* level = getAudioLevel(selectedDevice);
    if (level != nullptr) {
        suppressSliderEvents = true;
        lv_slider_set_value(ui_volumeSlider, level->volume, LV_ANIM_ON);
        updateVolumeSliderLabel(level->volume);
        suppressSliderEvents = false;
        ESP_LOGI(TAG, "Synced volume slider to %d for device: %s",
                 level->volume, selectedDevice.c_str());
    } else {
        ESP_LOGW(TAG, "Selected device '%s' not found in audio levels", selectedDevice.c_str());
        suppressSliderEvents = true;
        lv_slider_set_value(ui_volumeSlider, 0, LV_ANIM_OFF);
        updateVolumeSliderLabel(0);
        suppressSliderEvents = false;
    }
}

void StatusManager::updateVolumeSliderLabel(int volume) {
    if (ui_volumeSliderLbl) {
        char labelText[16];
        snprintf(labelText, sizeof(labelText), "%d%%", volume);
        lv_label_set_text(ui_volumeSliderLbl, labelText);
    }
}

// Volume control

void StatusManager::setSelectedDeviceVolume(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    if (selectedDevice.isEmpty()) {
        ESP_LOGW(TAG, "No device selected for volume control");
        return;
    }

    // Clamp volume to valid range
    volume = constrain(volume, 0, 100);

    // Update local audio level immediately for responsive UI
    updateAudioLevel(selectedDevice, volume);

    // Update the volume slider label
    updateVolumeSliderLabel(volume);

    // Publish volume change command via MQTT
    publishVolumeChangeCommand(selectedDevice, volume);

    ESP_LOGI(TAG, "Set volume to %d for device: %s", volume, selectedDevice.c_str());
}

void StatusManager::muteSelectedDevice(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    if (selectedDevice.isEmpty()) {
        ESP_LOGW(TAG, "No device selected for mute control");
        return;
    }

    // Publish mute command via MQTT
    publishMuteCommand(selectedDevice);

    ESP_LOGI(TAG, "Muted device: %s", selectedDevice.c_str());
}

void StatusManager::unmuteSelectedDevice(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    if (selectedDevice.isEmpty()) {
        ESP_LOGW(TAG, "No device selected for unmute control");
        return;
    }

    // Publish unmute command via MQTT
    publishUnmuteCommand(selectedDevice);

    ESP_LOGI(TAG, "Unmuted device: %s", selectedDevice.c_str());
}

void StatusManager::publishVolumeChangeCommand(const String& deviceName, int volume) {
    if (!Hardware::Mqtt::isConnected()) {
        ESP_LOGW(TAG, "Cannot publish volume command: MQTT not connected");
        return;
    }

    // Create JSON command using AudioMixUpdateMessage format
    JsonDocument doc;
    doc["messageType"] = "audio.mix.update";
    doc["timestamp"] = String(Hardware::Device::getMillis());
    doc["messageId"] = String(Hardware::Device::getMillis());

    // Create updates array with a single AudioMixUpdate
    JsonArray updates = doc.createNestedArray("updates");
    JsonObject update = updates.createNestedObject();
    update["processName"] = deviceName;
    update["action"] = "SetVolume";
    update["volume"] = volume;

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the command
    bool published = Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str());

    if (published) {
        ESP_LOGI(TAG, "Published AudioMixUpdate command for %s: %d", deviceName.c_str(), volume);
    } else {
        ESP_LOGE(TAG, "Failed to publish volume command");
    }
}

void StatusManager::publishMuteCommand(const String& deviceName) {
    if (!Hardware::Mqtt::isConnected()) {
        ESP_LOGW(TAG, "Cannot publish mute command: MQTT not connected");
        return;
    }

    // Create JSON command using AudioMixUpdateMessage format
    JsonDocument doc;
    doc["messageType"] = "audio.mix.update";
    doc["timestamp"] = String(Hardware::Device::getMillis());
    doc["messageId"] = String(Hardware::Device::getMillis());

    // Create updates array with a single AudioMixUpdate
    JsonArray updates = doc.createNestedArray("updates");
    JsonObject update = updates.createNestedObject();
    update["processName"] = deviceName;
    update["action"] = "Mute";

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the command
    bool published = Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str());

    if (published) {
        ESP_LOGI(TAG, "Published mute command for %s", deviceName.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to publish mute command");
    }
}

void StatusManager::publishUnmuteCommand(const String& deviceName) {
    if (!Hardware::Mqtt::isConnected()) {
        ESP_LOGW(TAG, "Cannot publish unmute command: MQTT not connected");
        return;
    }

    // Create JSON command using AudioMixUpdateMessage format
    JsonDocument doc;
    doc["messageType"] = "audio.mix.update";
    doc["timestamp"] = String(Hardware::Device::getMillis());
    doc["messageId"] = String(Hardware::Device::getMillis());

    // Create updates array with a single AudioMixUpdate
    JsonArray updates = doc.createNestedArray("updates");
    JsonObject update = updates.createNestedObject();
    update["processName"] = deviceName;
    update["action"] = "Unmute";

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the command
    bool published = Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str());

    if (published) {
        ESP_LOGI(TAG, "Published unmute command for %s", deviceName.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to publish unmute command");
    }
}

bool StatusManager::isSuppressingSliderEvents(void) {
    return suppressSliderEvents;
}

// MQTT publishing methods

void StatusManager::publishAudioStatusRequest(void) {
    if (!Hardware::Mqtt::isConnected()) {
        ESP_LOGW(TAG, "Cannot publish audio status request: MQTT not connected");
        return;
    }

    // Create JSON message using AudioStatusRequestMessage format
    JsonDocument doc;
    doc["messageType"] = "audio.status.request";
    doc["timestamp"] = String(Hardware::Device::getMillis());
    doc["messageId"] = String(Hardware::Device::getMillis());  // Simple ID based on millis

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the message
    bool published = Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str());

    if (published) {
        ESP_LOGI(TAG, "Published audio status request");
    } else {
        ESP_LOGE(TAG, "Failed to publish audio status request");
    }
}

// Private methods

void StatusManager::initializeAudioStatusHandler(void) {
    strcpy(audioStatusHandler.identifier, "AudioStatusManager");
    strcpy(audioStatusHandler.subscribeTopic, "homeassistant/unimix/audio_status");
    strcpy(audioStatusHandler.publishTopic, "homeassistant/unimix/audio/requests");
    audioStatusHandler.callback = audioStatusMessageHandler;
    audioStatusHandler.active = true;
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
    for (JsonPair pair : root) {
        const char* key = pair.key().c_str();

        // Check if value is a number
        if (pair.value().is<int>()) {
            int value = pair.value().as<int>();

            AudioLevel level;
            level.processName = String(key);
            level.volume = value;
            level.lastUpdate = now;
            result.push_back(level);

            ESP_LOGD(TAG, "Parsed audio level - Process: %s, Volume: %d",
                     key, value);
        } else {
            ESP_LOGW(TAG, "Skipping non-numeric value for key: %s", key);
        }
    }

    return result;
}

}  // namespace Audio
}  // namespace Application