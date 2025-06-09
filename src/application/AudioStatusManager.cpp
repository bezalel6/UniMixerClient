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