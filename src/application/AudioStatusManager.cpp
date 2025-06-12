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

// Updated macro for new protocol format
#define AUDIO_COMMAND_BASE(command_type)                                 \
    if (!Messaging::MessageBus::IsConnected()) {                         \
        ESP_LOGW(TAG, "Cannot publish command: No transport connected"); \
        return;                                                          \
    }                                                                    \
    JsonDocument doc;                                                    \
    doc["commandType"] = command_type;                                   \
    doc["processId"] = getProcessIdForDevice(deviceName);                \
    doc["processName"] = deviceName;                                     \
    doc["requestId"] = Messaging::Protocol::generateRequestId();

#define AUDIO_PUBLISH_COMMAND_FINISH(action_name)                                      \
    String jsonPayload;                                                                \
    serializeJson(doc, jsonPayload);                                                   \
    bool published = Messaging::MessageBus::Publish("COMMAND", jsonPayload.c_str());   \
    if (published) {                                                                   \
        ESP_LOGI(TAG, "Published " action_name " command for %s", deviceName.c_str()); \
    } else {                                                                           \
        ESP_LOGE(TAG, "Failed to publish " action_name " command");                    \
    }

namespace Application {
namespace Audio {

// Static member definitions
AudioStatus StatusManager::currentAudioStatus;
Messaging::Handler StatusManager::audioStatusHandler;
Messaging::Handler StatusManager::commandResultHandler;
unsigned long StatusManager::lastUpdateTime = 0;
bool StatusManager::initialized = false;
String StatusManager::selectedDevice = "";
bool StatusManager::suppressArcEvents = false;
Events::UI::TabState StatusManager::currentTab = Events::UI::TabState::MASTER;

bool StatusManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioStatusManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioStatusManager");

    // Clear existing data
    currentAudioStatus.audioLevels.clear();
    currentAudioStatus.hasDefaultDevice = false;
    currentAudioStatus.timestamp = 0;
    lastUpdateTime = Hardware::Device::getMillis();

    // Initialize and register message handlers
    initializeMessageHandlers();
    if (!Messaging::MessageBus::RegisterHandler(audioStatusHandler)) {
        ESP_LOGE(TAG, "Failed to register audio status message handler");
        return false;
    }
    if (!Messaging::MessageBus::RegisterHandler(commandResultHandler)) {
        ESP_LOGE(TAG, "Failed to register command result message handler");
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

    // Unregister message handlers
    Messaging::MessageBus::UnregisterHandler(audioStatusHandler.Identifier);
    Messaging::MessageBus::UnregisterHandler(commandResultHandler.Identifier);

    // Clear data
    currentAudioStatus.audioLevels.clear();
    currentAudioStatus.hasDefaultDevice = false;
    currentAudioStatus.timestamp = 0;
    initialized = false;
}

void StatusManager::updateAudioLevel(const String& processName, int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    unsigned long now = Hardware::Device::getMillis();

    // Find existing process or create new entry
    for (auto& level : currentAudioStatus.audioLevels) {
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
    newLevel.stale = false;
    currentAudioStatus.audioLevels.push_back(newLevel);

    lastUpdateTime = now;

    ESP_LOGI(TAG, "Updated audio level - Process: %s, Volume: %d",
             processName.c_str(), volume);
}

// Helper function to map process names to IDs (simplified for demo)
int StatusManager::getProcessIdForDevice(const String& deviceName) {
    // In a real implementation, maintain a mapping of process names to IDs
    // For now, use a simple hash of the process name
    unsigned long hash = 0;
    for (int i = 0; i < deviceName.length(); i++) {
        hash = hash * 31 + deviceName.charAt(i);
    }
    return (int)(hash % 65536);  // Keep it reasonable
}

// Helper method to update all dropdown options with the same content
void StatusManager::updateAllDropdownOptions(void) {
    // Update all dropdowns
    if (ui_selectAudioDevice1) {
        lv_dropdown_clear_options(ui_selectAudioDevice1);
        if (currentAudioStatus.audioLevels.empty()) {
            lv_dropdown_add_option(ui_selectAudioDevice1, "No devices", LV_DROPDOWN_POS_LAST);
        } else {
            for (const auto& level : currentAudioStatus.audioLevels) {
                String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
                lv_dropdown_add_option(ui_selectAudioDevice1, displayName.c_str(), LV_DROPDOWN_POS_LAST);
            }
        }
    }

    if (ui_selectAudioDevice2) {
        lv_dropdown_clear_options(ui_selectAudioDevice2);
        if (currentAudioStatus.audioLevels.empty()) {
            lv_dropdown_add_option(ui_selectAudioDevice2, "No devices", LV_DROPDOWN_POS_LAST);
        } else {
            for (const auto& level : currentAudioStatus.audioLevels) {
                String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
                lv_dropdown_add_option(ui_selectAudioDevice2, displayName.c_str(), LV_DROPDOWN_POS_LAST);
            }
        }
    }
}

// Helper method to update a single dropdown selection
void StatusManager::updateSingleDropdownSelection(lv_obj_t* dropdown) {
    if (!dropdown) return;

    // Find the index of selectedDevice in the dropdown by comparing with our audio levels
    uint16_t optionCount = lv_dropdown_get_option_cnt(dropdown);
    for (uint16_t i = 0; i < optionCount && i < currentAudioStatus.audioLevels.size(); i++) {
        // Compare with our known audio levels
        String levelName = currentAudioStatus.audioLevels[i].processName;

        if (levelName == selectedDevice) {
            lv_dropdown_set_selected(dropdown, i);
            break;
        }
    }
}

// Build options string for dropdowns (legacy compatibility method)
String StatusManager::buildAudioDeviceOptionsString(void) {
    if (currentAudioStatus.audioLevels.empty()) {
        return "None";
    }

    String options = "";
    bool first = true;

    for (const auto& level : currentAudioStatus.audioLevels) {
        if (!first) {
            options += "\n";
        }

        // Format: "ProcessName (Volume%)" or "ProcessName (Volume%) (Stale)"
        options += level.processName;
        options += " (";
        options += String(level.volume);
        options += "%)";

        if (level.stale) {
            options += " (Stale)";
        }

        first = false;
    }

    return options;
}

// Get selected audio device from dropdown (for event handlers)
String StatusManager::getSelectedAudioDevice(lv_obj_t* dropdown) {
    if (dropdown == NULL) {
        ESP_LOGW(TAG, "getSelectedAudioDevice: Invalid dropdown parameter");
        return "";
    }

    // Get the selected text
    char selectedText[256];
    lv_dropdown_get_selected_str(dropdown, selectedText, sizeof(selectedText));

    // Extract just the process name (before the volume indicator)
    String selectedString = String(selectedText);

    // Remove stale prefix if present
    if (selectedString.startsWith("(!) ")) {
        selectedString = selectedString.substring(4);
    }

    // Remove volume indicator if present
    int volumeStart = selectedString.indexOf(" (");
    if (volumeStart > 0) {
        return selectedString.substring(0, volumeStart);
    }

    return selectedString;
}

std::vector<AudioLevel> StatusManager::getAllAudioLevels(void) {
    return currentAudioStatus.audioLevels;
}

AudioLevel* StatusManager::getAudioLevel(const String& processName) {
    for (auto& level : currentAudioStatus.audioLevels) {
        if (level.processName == processName) {
            return &level;
        }
    }
    return nullptr;
}

AudioStatus StatusManager::getCurrentAudioStatus(void) {
    return currentAudioStatus;
}

void StatusManager::onAudioStatusReceived(const AudioStatus& status) {
    ESP_LOGI(TAG, "Received audio status update with %d processes and %s default device",
             status.audioLevels.size(), status.hasDefaultDevice ? "a" : "no");

    // Update the current audio status with the received data
    currentAudioStatus = status;
    currentAudioStatus.timestamp = Hardware::Device::getMillis();

    // First, mark all existing devices as stale
    for (auto& level : currentAudioStatus.audioLevels) {
        if (!level.stale) {
            ESP_LOGI(TAG, "Marking device as stale: %s", level.processName.c_str());
        }
        level.stale = true;
    }

    // Update levels from the received data and mark them as fresh
    for (const auto& level : status.audioLevels) {
        updateAudioLevel(level.processName, level.volume);
        // Find the updated device and mark it as fresh
        for (auto& existingLevel : currentAudioStatus.audioLevels) {
            if (existingLevel.processName == level.processName) {
                existingLevel.stale = false;
                break;
            }
        }
    }

    // Auto-select first non-stale device if none is selected and devices are available
    if (selectedDevice.isEmpty() && !currentAudioStatus.audioLevels.empty()) {
        // Find first non-stale device, or fall back to first device if all are stale
        String deviceToSelect = "";
        for (const auto& level : currentAudioStatus.audioLevels) {
            if (!level.stale) {
                deviceToSelect = level.processName;
                break;
            }
        }
        if (deviceToSelect.isEmpty() && !currentAudioStatus.audioLevels.empty()) {
            deviceToSelect = currentAudioStatus.audioLevels[0].processName;
        }
        if (!deviceToSelect.isEmpty()) {
            setSelectedDevice(deviceToSelect);
        }
    }

    // Update the UI
    onAudioLevelsChangedUI();
}

void StatusManager::onAudioLevelsChangedUI(void) {
    // Update device dropdown if available (using the actual UI element names)
    if (ui_selectAudioDevice && !suppressArcEvents) {
        // Clear existing options
        lv_dropdown_clear_options(ui_selectAudioDevice);

        // If in MASTER tab, show default device in dropdown
        if (currentTab == Events::UI::TabState::MASTER) {
            // For MASTER tab, still show all available devices
            if (currentAudioStatus.audioLevels.empty()) {
                lv_dropdown_add_option(ui_selectAudioDevice, "No devices", LV_DROPDOWN_POS_LAST);
            } else {
                for (const auto& level : currentAudioStatus.audioLevels) {
                    // Mark stale devices with a prefix
                    String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
                    lv_dropdown_add_option(ui_selectAudioDevice, displayName.c_str(), LV_DROPDOWN_POS_LAST);
                }
            }
        } else {
            // For other tabs, add devices from audio levels
            if (currentAudioStatus.audioLevels.empty()) {
                lv_dropdown_add_option(ui_selectAudioDevice, "No devices", LV_DROPDOWN_POS_LAST);
            } else {
                for (const auto& level : currentAudioStatus.audioLevels) {
                    // Mark stale devices with a prefix
                    String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
                    lv_dropdown_add_option(ui_selectAudioDevice, displayName.c_str(), LV_DROPDOWN_POS_LAST);
                }
            }
        }

        if (currentAudioStatus.hasDefaultDevice && ui_lblPrimaryAudioDeviceValue) {
            lv_label_set_text(ui_lblPrimaryAudioDeviceValue, currentAudioStatus.defaultDevice.friendlyName.c_str());
        }
        // Update other dropdowns too
        updateAllDropdownOptions();

        // Update selection to match selectedDevice
        updateDropdownSelection();
    }

    // Update volume arc based on current tab
    updateVolumeArcFromSelectedDevice();
}

void StatusManager::updateDropdownSelection(void) {
    if (!ui_selectAudioDevice || selectedDevice.isEmpty()) {
        return;
    }

    suppressArcEvents = true;

    // Update primary dropdown
    updateSingleDropdownSelection(ui_selectAudioDevice);

    // Update other dropdowns if they exist
    if (ui_selectAudioDevice1) {
        updateSingleDropdownSelection(ui_selectAudioDevice1);
    }
    if (ui_selectAudioDevice2) {
        updateSingleDropdownSelection(ui_selectAudioDevice2);
    }

    suppressArcEvents = false;
}

String StatusManager::getSelectedDevice(void) {
    return selectedDevice;
}

void StatusManager::setSelectedDevice(const String& deviceName) {
    selectedDevice = deviceName;
    ESP_LOGI(TAG, "Selected device: %s", deviceName.c_str());

    // Update UI elements
    updateDropdownSelection();
    updateVolumeArcFromSelectedDevice();
}

void StatusManager::updateVolumeArcFromSelectedDevice(void) {
    if (!ui_volumeSlider) {
        return;
    }

    suppressArcEvents = true;

    // If in MASTER tab, show default device volume
    if (currentTab == Events::UI::TabState::MASTER) {
        if (currentAudioStatus.hasDefaultDevice) {
            int volume = (int)(currentAudioStatus.defaultDevice.volume * 100.0f);
            lv_arc_set_value(ui_volumeSlider, volume);
            updateVolumeArcLabel(volume);
        } else {
            lv_arc_set_value(ui_volumeSlider, 0);
            updateVolumeArcLabel(0);
        }
    } else {
        // For other tabs, show selected device volume
        if (selectedDevice.isEmpty()) {
            lv_arc_set_value(ui_volumeSlider, 0);
            updateVolumeArcLabel(0);
        } else {
            // Find audio level for selected device
            AudioLevel* level = getAudioLevel(selectedDevice);
            if (level) {
                lv_arc_set_value(ui_volumeSlider, level->volume);
                updateVolumeArcLabel(level->volume);
            } else {
                lv_arc_set_value(ui_volumeSlider, 0);
                updateVolumeArcLabel(0);
            }
        }
    }

    suppressArcEvents = false;
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

    // Update the volume arc label
    updateVolumeArcLabel(volume);

    // If in MASTER tab, control the default device
    if (currentTab == Events::UI::TabState::MASTER) {
        if (currentAudioStatus.hasDefaultDevice) {
            // Update local default device volume for responsive UI
            currentAudioStatus.defaultDevice.volume = volume / 100.0f;
            ESP_LOGI(TAG, "Set default device volume to %d", volume);
        } else {
            ESP_LOGW(TAG, "No default device available for master volume control");
            return;
        }
    } else {
        // Update local audio level immediately for responsive UI
        updateAudioLevel(selectedDevice, volume);
        ESP_LOGI(TAG, "Set volume to %d for device: %s", volume, selectedDevice.c_str());
    }

    // Publish the updated status to server
    publishStatusUpdate();
}

void StatusManager::muteSelectedDevice(void) {
    AUDIO_DEVICE_ACTION_PROLOGUE("mute")

    // If in MASTER tab, control the default device
    if (currentTab == Events::UI::TabState::MASTER) {
        if (currentAudioStatus.hasDefaultDevice) {
            currentAudioStatus.defaultDevice.isMuted = true;
            ESP_LOGI(TAG, "Muted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master mute control");
            return;
        }
    } else {
        // Find and update the selected device's mute state
        AudioLevel* level = getAudioLevel(selectedDevice);
        if (level) {
            level->isMuted = true;
        }
        ESP_LOGI(TAG, "Muted device: %s", selectedDevice.c_str());
    }

    // Publish the updated status to server
    publishStatusUpdate();
}

void StatusManager::unmuteSelectedDevice(void) {
    AUDIO_DEVICE_ACTION_PROLOGUE("unmute")

    // If in MASTER tab, control the default device
    if (currentTab == Events::UI::TabState::MASTER) {
        if (currentAudioStatus.hasDefaultDevice) {
            currentAudioStatus.defaultDevice.isMuted = false;
            ESP_LOGI(TAG, "Unmuted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master unmute control");
            return;
        }
    } else {
        // Find and update the selected device's mute state
        AudioLevel* level = getAudioLevel(selectedDevice);
        if (level) {
            level->isMuted = false;
        }
        ESP_LOGI(TAG, "Unmuted device: %s", selectedDevice.c_str());
    }

    // Publish the updated status to server
    publishStatusUpdate();
}

void StatusManager::publishStatusUpdate(void) {
    if (!Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish status update: No transport connected");
        return;
    }

    JsonDocument doc;
    doc["messageType"] = Messaging::Protocol::MESSAGE_STATUS_UPDATE;
    doc["requestId"] = Messaging::Protocol::generateRequestId();
    doc["timestamp"] = Hardware::Device::getMillis();

    // Add sessions array
    JsonArray sessions = doc["sessions"].to<JsonArray>();
    for (const auto& level : currentAudioStatus.audioLevels) {
        JsonObject session = sessions.add<JsonObject>();
        session["processName"] = level.processName;
        session["volume"] = level.volume / 100.0f;  // Convert to 0.0-1.0 range
        session["isMuted"] = level.isMuted;
        session["state"] = "Active";
    }

    // Add default device if available
    if (currentAudioStatus.hasDefaultDevice) {
        JsonObject defaultDevice = doc["defaultDevice"].to<JsonObject>();
        defaultDevice["friendlyName"] = currentAudioStatus.defaultDevice.friendlyName;
        defaultDevice["volume"] = currentAudioStatus.defaultDevice.volume;
        defaultDevice["isMuted"] = currentAudioStatus.defaultDevice.isMuted;
        defaultDevice["dataFlow"] = currentAudioStatus.defaultDevice.state;
        defaultDevice["deviceRole"] = "Console";
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);
    bool published = Messaging::MessageBus::Publish("STATUS_UPDATE", jsonPayload.c_str());
    if (published) {
        ESP_LOGI(TAG, "Published status update with %d sessions", currentAudioStatus.audioLevels.size());
    } else {
        ESP_LOGE(TAG, "Failed to publish status update");
    }
}

bool StatusManager::isSuppressingArcEvents(void) {
    return suppressArcEvents;
}

// Tab state management methods
Events::UI::TabState StatusManager::getCurrentTab(void) {
    return currentTab;
}

void StatusManager::setCurrentTab(Events::UI::TabState tab) {
    currentTab = tab;
    // Update UI elements for the new tab
    onAudioLevelsChangedUI();
}

const char* StatusManager::getTabName(Events::UI::TabState tab) {
    switch (tab) {
        case Events::UI::TabState::MASTER:
            return "Master";
        case Events::UI::TabState::SINGLE:
            return "Single";
        case Events::UI::TabState::BALANCE:
            return "Balance";
        default:
            return "Unknown";
    }
}

// Message publishing methods

void StatusManager::publishAudioStatusRequest(bool delayed) {
    if (!delayed && !Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish audio status request: No transport connected");
        return;
    }

    // Create JSON request for getting status
    JsonDocument doc;
    doc["messageType"] = Messaging::Protocol::MESSAGE_GET_STATUS;
    doc["requestId"] = Messaging::Protocol::generateRequestId();

    // Serialize to string
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Publish the request (delayed or immediate)
    bool published;
    if (delayed) {
        published = Messaging::MessageBus::PublishDelayed("STATUS_REQUEST", jsonPayload.c_str());
        if (published) {
            ESP_LOGI(TAG, "Queued delayed audio status request");
        } else {
            ESP_LOGE(TAG, "Failed to queue delayed audio status request");
        }
    } else {
        published = Messaging::MessageBus::Publish("STATUS_REQUEST", jsonPayload.c_str());
        if (published) {
            ESP_LOGI(TAG, "Published audio status request");
        } else {
            ESP_LOGE(TAG, "Failed to publish audio status request");
        }
    }
}

// Private methods

void StatusManager::initializeMessageHandlers(void) {
    // Status message handler
    audioStatusHandler.Identifier = "AudioStatusHandler";
    audioStatusHandler.SubscribeTopic = "STATUS";
    audioStatusHandler.PublishTopic = "";
    audioStatusHandler.Callback = audioStatusMessageHandler;
    audioStatusHandler.Active = true;

    // Command result handler
    commandResultHandler.Identifier = "CommandResultHandler";
    commandResultHandler.SubscribeTopic = "RESULT";
    commandResultHandler.PublishTopic = "";
    commandResultHandler.Callback = commandResultMessageHandler;
    commandResultHandler.Active = true;
}

void StatusManager::audioStatusMessageHandler(const char* messageType, const char* payload) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized, ignoring message");
        return;
    }

    AudioStatus status = parseAudioStatusJson(payload);

    if (status.audioLevels.empty() && !status.hasDefaultDevice) {
        ESP_LOGE(TAG, "Failed to parse audio status JSON or no valid data found");
        // LOG_TO_UI(ui_txtAreaDebugLog, String("AUDIO PARSE FAIL: No data found"));
        // LOG_TO_UI(ui_txtAreaDebugLog, String("Payload: ") + String(payload));
        return;
    }

    // Process the received audio status
    onAudioStatusReceived(status);
}

void StatusManager::commandResultMessageHandler(const char* messageType, const char* payload) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized, ignoring result");
        return;
    }

    ESP_LOGI(TAG, "Received command result: %s", payload);

    // Parse result to check if command was successful
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse command result JSON: %s", error.c_str());
        return;
    }

    bool success = doc["success"] | false;
    String message = doc["message"] | "No message";
    String requestId = doc["requestId"] | "";

    if (success) {
        ESP_LOGI(TAG, "Command successful [%s]: %s", requestId.c_str(), message.c_str());
    } else {
        ESP_LOGW(TAG, "Command failed [%s]: %s", requestId.c_str(), message.c_str());
    }
}

AudioStatus StatusManager::parseAudioStatusJson(const char* jsonPayload) {
    AudioStatus result;

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
    result.timestamp = now;

    // Parse default device if present
    JsonObject defaultDevice = root["defaultDevice"];
    if (!defaultDevice.isNull()) {
        String friendlyName = defaultDevice["friendlyName"] | "";
        float volume = defaultDevice["volume"] | 0.0f;
        bool isMuted = defaultDevice["isMuted"] | false;
        String dataFlow = defaultDevice["dataFlow"] | "";
        String deviceRole = defaultDevice["deviceRole"] | "";

        if (friendlyName.length() > 0) {
            result.defaultDevice.friendlyName = friendlyName;
            result.defaultDevice.volume = volume;
            result.defaultDevice.isMuted = isMuted;
            result.defaultDevice.state = dataFlow + "/" + deviceRole;
            result.hasDefaultDevice = true;

            ESP_LOGI(TAG, "Parsed default device: %s = %.1f%% %s [%s]",
                     friendlyName.c_str(), volume * 100.0f, isMuted ? "(muted)" : "",
                     result.defaultDevice.state.c_str());
        }
    }

    // Parse the new server status format
    JsonArray sessions = root["sessions"];
    if (sessions.isNull()) {
        ESP_LOGW(TAG, "No sessions array in status message");
        return result;
    }

    for (JsonObject session : sessions) {
        String processName = session["processName"] | "";
        String displayName = session["displayName"] | "";
        float volume = session["volume"] | 0.0f;
        bool isMuted = session["isMuted"] | false;
        String state = session["state"] | "";

        if (processName.length() > 0) {
            AudioLevel level;
            level.processName = processName;
            level.volume = (int)(volume * 100.0f);  // Convert from 0.0-1.0 to 0-100
            level.isMuted = isMuted;
            level.lastUpdate = now;
            level.stale = false;

            result.audioLevels.push_back(level);
            ESP_LOGI(TAG, "Parsed audio session: %s = %d%% %s",
                     processName.c_str(), level.volume, isMuted ? "(muted)" : "");
        }
    }

    ESP_LOGI(TAG, "Parsed %d audio sessions from status message", result.audioLevels.size());
    return result;
}

}  // namespace Audio
}  // namespace Application