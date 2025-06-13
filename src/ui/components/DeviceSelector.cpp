#include "DeviceSelector.h"
#include "../../application/AudioStatusManager.h"
#include <esp_log.h>

static const char* TAG = "DeviceSelector";

namespace UI {
namespace Components {

// Use the AudioLevel from Application::Audio namespace
using AudioLevel = Application::Audio::AudioLevel;

DeviceSelector::DeviceSelector(lv_obj_t* dropdown) : dropdown(dropdown), selectedDevice("") {
    if (!dropdown) {
        ESP_LOGE(TAG, "DeviceSelector: Invalid dropdown parameter");
    }
}

void DeviceSelector::setSelection(const String& deviceName) {
    selectedDevice = deviceName;
    ESP_LOGI(TAG, "Device selection set to: %s", deviceName.c_str());
}

String DeviceSelector::getSelection() const {
    return selectedDevice;
}

void DeviceSelector::clearSelection() {
    selectedDevice = "";
    ESP_LOGI(TAG, "Device selection cleared");
}

void DeviceSelector::refresh(const std::vector<AudioLevel>& audioLevels) {
    if (!dropdown) return;

    updateOptions(audioLevels);
    updateSelection();
}

void DeviceSelector::updateOptions(const std::vector<AudioLevel>& audioLevels) {
    updateDropdownOptions(audioLevels);
}

void DeviceSelector::updateSelection() {
    updateDropdownSelection();
}

bool DeviceSelector::isAvailableFor(const String& deviceName) const {
    // Base implementation - all devices are available
    return !deviceName.isEmpty();
}

void DeviceSelector::updateDropdownOptions(const std::vector<AudioLevel>& audioLevels) {
    if (!dropdown) return;

    // Simple change detection to prevent excessive updates
    static std::vector<AudioLevel> lastAudioLevels;
    bool hasChanged = false;

    if (lastAudioLevels.size() != audioLevels.size()) {
        hasChanged = true;
    } else {
        for (size_t i = 0; i < audioLevels.size(); i++) {
            if (i >= lastAudioLevels.size() ||
                lastAudioLevels[i].processName != audioLevels[i].processName ||
                lastAudioLevels[i].volume != audioLevels[i].volume ||
                lastAudioLevels[i].stale != audioLevels[i].stale) {
                hasChanged = true;
                break;
            }
        }
    }

    if (!hasChanged) return;

    // Update the last known state
    lastAudioLevels = audioLevels;

    lv_dropdown_clear_options(dropdown);

    if (audioLevels.empty()) {
        lv_dropdown_add_option(dropdown, "No devices", LV_DROPDOWN_POS_LAST);
        return;
    }

    bool hasAvailableOptions = false;
    for (const auto& level : audioLevels) {
        if (isAvailableFor(level.processName)) {
            String displayName = formatDisplayName(level);
            lv_dropdown_add_option(dropdown, displayName.c_str(), LV_DROPDOWN_POS_LAST);
            hasAvailableOptions = true;
        }
    }

    if (!hasAvailableOptions) {
        lv_dropdown_add_option(dropdown, "No devices", LV_DROPDOWN_POS_LAST);
    }
}

void DeviceSelector::updateDropdownSelection() {
    if (!dropdown || selectedDevice.isEmpty()) return;

    // Store current selection to avoid unnecessary updates
    uint16_t currentSelection = lv_dropdown_get_selected(dropdown);

    // Find the correct index by checking which option matches our target device
    uint16_t optionCount = lv_dropdown_get_option_cnt(dropdown);
    uint16_t targetIndex = 0xFFFF;  // Invalid index

    // Get all options as a single string and parse them
    const char* options = lv_dropdown_get_options(dropdown);
    if (!options) return;

    String optionsString = String(options);
    int startPos = 0;
    int endPos = 0;
    uint16_t optionIndex = 0;

    // Parse options string (format: "option1\noption2\noption3")
    while (startPos < optionsString.length() && optionIndex < optionCount) {
        endPos = optionsString.indexOf('\n', startPos);
        if (endPos == -1) {
            endPos = optionsString.length();
        }

        String optionString = optionsString.substring(startPos, endPos);

        // Remove stale prefix if present
        if (optionString.startsWith("(!) ")) {
            optionString = optionString.substring(4);
        }

        // Remove volume indicator if present
        int volumeStart = optionString.indexOf(" (");
        if (volumeStart > 0) {
            optionString = optionString.substring(0, volumeStart);
        }

        if (optionString == selectedDevice) {
            targetIndex = optionIndex;
            break;
        }

        startPos = endPos + 1;
        optionIndex++;
    }

    // Only update if the selection actually needs to change
    if (targetIndex != 0xFFFF && targetIndex != currentSelection) {
        lv_dropdown_set_selected(dropdown, targetIndex);
    } else if (targetIndex == 0xFFFF && currentSelection != 0) {
        // No match found, reset to first option
        lv_dropdown_set_selected(dropdown, 0);
    }
}

String DeviceSelector::formatDisplayName(const AudioLevel& level) const {
    String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
    return displayName;
}

}  // namespace Components
}  // namespace UI