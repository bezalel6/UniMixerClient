#include "AudioUI.h"
#include "ui/screens/ui_screenMain.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "AudioUI";

namespace Application {
namespace Audio {

AudioUI& AudioUI::getInstance() {
    static AudioUI instance;
    return instance;
}

bool AudioUI::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioUI already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioUI");

    // Subscribe to state changes from AudioManager
    AudioManager::getInstance().subscribeToStateChanges(
        [this](const AudioStateChangeEvent& event) {
            onAudioStateChanged(event);
        });

    initialized = true;
    ESP_LOGI(TAG, "AudioUI initialized successfully");
    return true;
}

void AudioUI::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioUI");
    initialized = false;
}

// === UI EVENT HANDLERS ===

void AudioUI::onVolumeSliderChanged(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    if (shouldSuppressUIEvent()) {
        ESP_LOGD(TAG, "Suppressing volume slider event");
        return;
    }

    ESP_LOGI(TAG, "Volume slider changed to: %d", volume);
    AudioManager::getInstance().setVolumeForCurrentDevice(volume);
}

void AudioUI::onVolumeSliderDragging(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    // Update the volume label in real-time during dragging
    // This provides immediate visual feedback without committing the change
    LVGLMessageHandler::updateCurrentTabVolume(volume);
}

void AudioUI::onDeviceDropdownChanged(lv_obj_t* dropdown, const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    if (shouldSuppressUIEvent()) {
        ESP_LOGD(TAG, "Suppressing dropdown event");
        return;
    }

    ESP_LOGI(TAG, "Device dropdown changed to: %s", deviceName.c_str());

    // Handle different dropdowns based on current tab
    const auto& state = AudioManager::getInstance().getState();

    if (state.isInBalanceTab()) {
        // For balance tab, handle dual selection
        if (dropdown == ui_selectAudioDevice1) {
            AudioManager::getInstance().selectBalanceDevices(deviceName, state.selectedDevice2);
        } else if (dropdown == ui_selectAudioDevice2) {
            AudioManager::getInstance().selectBalanceDevices(state.selectedDevice1, deviceName);
        }
    } else {
        // For single/master tabs
        AudioManager::getInstance().selectDevice(deviceName);
    }
}

void AudioUI::onTabChanged(Events::UI::TabState newTab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    ESP_LOGI(TAG, "Tab changed to: %s", AudioManager::getInstance().getTabName(newTab));
    AudioManager::getInstance().setCurrentTab(newTab);
}

void AudioUI::onMuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    ESP_LOGI(TAG, "Mute button pressed");
    AudioManager::getInstance().muteCurrentDevice();
}

void AudioUI::onUnmuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    ESP_LOGI(TAG, "Unmute button pressed");
    AudioManager::getInstance().unmuteCurrentDevice();
}

// === UI QUERIES ===

String AudioUI::getDropdownSelection(lv_obj_t* dropdown) const {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return "";
    }

    const auto& state = AudioManager::getInstance().getState();

    // Determine which dropdown this is and return appropriate selection
    if (dropdown == ui_selectAudioDevice) {
        return state.selectedMainDevice;
    } else if (dropdown == ui_selectAudioDevice1) {
        return state.selectedDevice1;
    } else if (dropdown == ui_selectAudioDevice2) {
        return state.selectedDevice2;
    }

    return "";
}

lv_obj_t* AudioUI::getCurrentVolumeSlider() const {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return nullptr;
    }

    const auto& state = AudioManager::getInstance().getState();

    switch (state.currentTab) {
        case Events::UI::TabState::MASTER:
            return ui_primaryVolumeSlider;
        case Events::UI::TabState::SINGLE:
            return ui_singleVolumeSlider;
        case Events::UI::TabState::BALANCE:
            return ui_balanceVolumeSlider;
        default:
            ESP_LOGW(TAG, "Unknown tab state for volume slider");
            return nullptr;
    }
}

// === UI UPDATE TRIGGERS ===

void AudioUI::refreshAllUI() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    ESP_LOGI(TAG, "Refreshing all UI elements");
    updateDeviceSelectors();
    updateVolumeDisplay();
    updateDefaultDeviceLabel();
    updateMuteButtons();
    updateTabVisibility();
}

void AudioUI::updateVolumeDisplay() {
    if (!initialized) {
        return;
    }

    const auto& state = AudioManager::getInstance().getState();
    int currentVolume = state.getCurrentSelectedVolume();

    // Use message handler for thread-safe UI updates
    LVGLMessageHandler::updateCurrentTabVolume(currentVolume);

    ESP_LOGD(TAG, "Updated volume display to: %d", currentVolume);
}

void AudioUI::updateDeviceSelectors() {
    if (!initialized) {
        return;
    }

    const auto& devices = AudioManager::getInstance().getAllDevices();

    // Update dropdown options
    updateDropdownOptions(devices);

    ESP_LOGD(TAG, "Updated device selectors with %d devices", devices.size());
}

void AudioUI::updateDefaultDeviceLabel() {
    if (!initialized) {
        return;
    }

    const auto& state = AudioManager::getInstance().getState();

    if (state.currentStatus.hasDefaultDevice) {
        LVGLMessageHandler::updateMasterDevice(
            state.currentStatus.defaultDevice.friendlyName.c_str());
        ESP_LOGD(TAG, "Updated default device label: %s",
                 state.currentStatus.defaultDevice.friendlyName.c_str());
    }
}

void AudioUI::updateMuteButtons() {
    if (!initialized) {
        return;
    }

    // This could be expanded to update mute button states
    // For now, mute state is handled implicitly through volume display
    ESP_LOGD(TAG, "Updated mute buttons");
}

// === PRIVATE METHODS ===

void AudioUI::onAudioStateChanged(const AudioStateChangeEvent& event) {
    ESP_LOGD(TAG, "Handling audio state change event: %d", (int)event.type);

    switch (event.type) {
        case AudioStateChangeEvent::DEVICES_UPDATED:
            updateDeviceSelectors();
            updateVolumeDisplay();
            updateDefaultDeviceLabel();
            break;

        case AudioStateChangeEvent::SELECTION_CHANGED:
            updateDropdownSelections();
            updateVolumeDisplay();
            break;

        case AudioStateChangeEvent::VOLUME_CHANGED:
            updateVolumeDisplay();
            break;

        case AudioStateChangeEvent::TAB_CHANGED:
            refreshAllUI();
            break;

        case AudioStateChangeEvent::MUTE_CHANGED:
            updateMuteButtons();
            updateDefaultDeviceLabel();
            break;
    }
}

void AudioUI::updateDropdownOptions(const std::vector<AudioLevel>& devices) {
    // Build options string for dropdowns (LVGL format: "Option1\nOption2\nOption3")
    String optionsString = "";
    if (devices.empty()) {
        optionsString = "-";  // Default option when no devices
    } else {
        for (size_t i = 0; i < devices.size(); i++) {
            if (i > 0) {
                optionsString += "\n";
            }
            optionsString += devices[i].processName;
        }
    }

    // Update all dropdown widgets with new options
    AudioManager::getInstance().setSuppressDropdownEvents(true);

    if (ui_selectAudioDevice) {
        lv_dropdown_set_options(ui_selectAudioDevice, optionsString.c_str());
    }
    if (ui_selectAudioDevice1) {
        lv_dropdown_set_options(ui_selectAudioDevice1, optionsString.c_str());
    }
    if (ui_selectAudioDevice2) {
        lv_dropdown_set_options(ui_selectAudioDevice2, optionsString.c_str());
    }

    // Restore selections after updating options
    updateDropdownSelections();

    AudioManager::getInstance().setSuppressDropdownEvents(false);
}

void AudioUI::updateDropdownSelections() {
    const auto& state = AudioManager::getInstance().getState();
    const auto& devices = state.currentStatus.audioLevels;

    // Helper lambda to find device index in options
    auto findDeviceIndex = [&devices](const String& deviceName) -> int {
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].processName == deviceName) {
                return i;
            }
        }
        return 0;  // Default to first option
    };

    // Update main dropdown
    if (ui_selectAudioDevice && !state.selectedMainDevice.isEmpty()) {
        int index = findDeviceIndex(state.selectedMainDevice);
        lv_dropdown_set_selected(ui_selectAudioDevice, index);
    }

    // Update balance dropdowns
    if (ui_selectAudioDevice1 && !state.selectedDevice1.isEmpty()) {
        int index = findDeviceIndex(state.selectedDevice1);
        lv_dropdown_set_selected(ui_selectAudioDevice1, index);
    }

    if (ui_selectAudioDevice2 && !state.selectedDevice2.isEmpty()) {
        int index = findDeviceIndex(state.selectedDevice2);
        lv_dropdown_set_selected(ui_selectAudioDevice2, index);
    }
}

void AudioUI::updateVolumeSlider() {
    const auto& state = AudioManager::getInstance().getState();
    lv_obj_t* slider = getCurrentVolumeSlider();

    if (slider) {
        int currentVolume = state.getCurrentSelectedVolume();
        AudioManager::getInstance().setSuppressArcEvents(true);
        lv_slider_set_value(slider, currentVolume, LV_ANIM_OFF);
        AudioManager::getInstance().setSuppressArcEvents(false);
    }
}

void AudioUI::updateTabVisibility() {
    // This could be expanded to show/hide different UI elements based on the current tab
    // For now, the tab-specific UI is handled by the LVGL tab view itself
    ESP_LOGD(TAG, "Updated tab visibility");
}

lv_obj_t* AudioUI::getDropdownForTab(Events::UI::TabState tab) const {
    switch (tab) {
        case Events::UI::TabState::MASTER:
            return ui_selectAudioDevice;
        case Events::UI::TabState::SINGLE:
            return ui_selectAudioDevice;
        case Events::UI::TabState::BALANCE:
            return ui_selectAudioDevice1;  // Primary for balance
        default:
            return nullptr;
    }
}

void AudioUI::setDropdownSelection(lv_obj_t* dropdown, const String& deviceName) {
    if (!dropdown) {
        return;
    }

    const auto& devices = AudioManager::getInstance().getAllDevices();

    // Find device index
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].processName == deviceName) {
            lv_dropdown_set_selected(dropdown, i);
            break;
        }
    }
}

int AudioUI::findDeviceIndexInDropdown(lv_obj_t* dropdown, const String& deviceName) const {
    if (!dropdown) {
        return 0;
    }

    const auto& devices = AudioManager::getInstance().getAllDevices();

    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].processName == deviceName) {
            return i;
        }
    }

    return 0;  // Default to first option
}

String AudioUI::getCurrentTabName() const {
    const auto& state = AudioManager::getInstance().getState();
    return AudioManager::getInstance().getTabName(state.currentTab);
}

bool AudioUI::shouldSuppressUIEvent() const {
    return AudioManager::getInstance().isSuppressingArcEvents() ||
           AudioManager::getInstance().isSuppressingDropdownEvents();
}

}  // namespace Audio
}  // namespace Application