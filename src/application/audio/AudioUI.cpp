#include "AudioUI.h"
#include "ui/screens/ui_screenMain.h"
#include "../../logo/LogoManager.h"
#include "UiEventHandlers.h"
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

    ESP_LOGI(TAG, "Volume slider changed to: %d - applying with reactive feedback", volume);

    // Send the volume change
    AudioManager::getInstance().setVolumeForCurrentDevice(volume);

    // Immediately update the UI to show the new volume
    // This provides instant feedback even before server confirmation
    LVGLMessageHandler::updateCurrentTabVolume(volume);

    // Also update the master device label if we're in master tab
    if (AudioManager::getInstance().getCurrentTab() == Events::UI::TabState::MASTER) {
        updateDefaultDeviceLabel();
    }
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

    ESP_LOGI(TAG, "Device dropdown changed to: %s", deviceName.c_str());

    // Handle different dropdowns based on current tab
    const auto& state = AudioManager::getInstance().getState();

    if (state.isInBalanceTab()) {
        // For balance tab, handle dual selection
        if (dropdown == ui_selectAudioDevice1) {
            String device2Name = state.selectedDevice2 ? state.selectedDevice2->processName : "";
            AudioManager::getInstance().selectBalanceDevices(deviceName, device2Name);
        } else if (dropdown == ui_selectAudioDevice2) {
            String device1Name = state.selectedDevice1 ? state.selectedDevice1->processName : "";
            AudioManager::getInstance().selectBalanceDevices(device1Name, deviceName);
        }
    } else {
        // For single/master tabs
        AudioManager::getInstance().selectDevice(deviceName);

        // Update logo if we're on the Single tab
        if (state.isInSingleTab()) {
            updateSingleTabLogo();
        }
    }
}

void AudioUI::onTabChanged(Events::UI::TabState newTab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUI not initialized");
        return;
    }

    ESP_LOGI(TAG, "Tab changed to: %s - triggering reactive updates", AudioManager::getInstance().getTabName(newTab));

    // Set the new tab first
    AudioManager::getInstance().setCurrentTab(newTab);

    // Trigger immediate smart auto-selection for the new tab
    // This happens after the tab is set so auto-selection logic can work correctly
    AudioManager& audioManager = AudioManager::getInstance();
    audioManager.performSmartAutoSelection();

    // Force a comprehensive UI refresh for the new tab context
    refreshAllUI();

    // Update logo display for debugging
    updateSingleTabLogo();
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

    if (!dropdown) {
        ESP_LOGW(TAG, "Dropdown is null");
        return "";
    }
    ESP_LOGI(TAG, "Dropdown Options: %s", lv_dropdown_get_options(dropdown));
    // Get the actual selected text from the LVGL dropdown widget
    char selectedText[64];  // Buffer to store the selected option text
    lv_dropdown_get_selected_str(dropdown, selectedText, sizeof(selectedText));

    ESP_LOGD(TAG, "Dropdown widget returned selected text: '%s'", selectedText);

    return String(selectedText);
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

    ESP_LOGD(TAG, "Refreshing all UI elements");

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

    // Update the volume slider directly (uncommented to show current volume)
    lv_obj_t* slider = getCurrentVolumeSlider();
    if (slider) {
        lv_arc_set_value(slider, currentVolume);
        ESP_LOGD(TAG, "Set %s tab slider to volume: %d",
                 AudioManager::getInstance().getTabName(state.currentTab), currentVolume);
    } else {
        ESP_LOGW(TAG, "No slider found for current tab: %s",
                 AudioManager::getInstance().getTabName(state.currentTab));
    }

    // Use message handler for thread-safe UI updates (labels, etc.)
    LVGLMessageHandler::updateCurrentTabVolume(currentVolume);

    ESP_LOGD(TAG, "Updated %s tab volume display to: %d",
             AudioManager::getInstance().getTabName(state.currentTab), currentVolume);
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
    ESP_LOGI(TAG, "Handling audio state change event: %d - triggering reactive UI updates", (int)event.type);

    switch (event.type) {
        case AudioStateChangeEvent::DEVICES_UPDATED:
            ESP_LOGD(TAG, "Devices updated - comprehensive UI refresh");
            updateDeviceSelectors();
            updateVolumeDisplay();
            updateDefaultDeviceLabel();
            // Also trigger smart auto-selection since device list changed
            AudioManager::getInstance().performSmartAutoSelection();
            break;

        case AudioStateChangeEvent::SELECTION_CHANGED:
            ESP_LOGI(TAG, "Device selection changed - updating UI");
            updateDropdownSelections();
            updateVolumeDisplay();
            updateSingleTabLogo();  // Update logo for debugging
            break;

        case AudioStateChangeEvent::VOLUME_CHANGED:
            ESP_LOGI(TAG, "Volume changed - updating display with immediate feedback");
            updateVolumeDisplay();
            break;

        case AudioStateChangeEvent::TAB_CHANGED:
            ESP_LOGD(TAG, "Tab changed - full reactive UI refresh");
            refreshAllUI();
            // Tab changes are already handled comprehensively in onTabChanged
            break;

        case AudioStateChangeEvent::MUTE_CHANGED:
            ESP_LOGI(TAG, "Mute state changed - updating UI and labels");
            updateMuteButtons();
            updateDefaultDeviceLabel();
            updateVolumeDisplay();  // Mute might affect volume display
            break;
    }

    ESP_LOGD(TAG, "Reactive state change handling complete");
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
}

void AudioUI::updateDropdownSelections() {
    const auto& state = AudioManager::getInstance().getState();
    const auto devices = state.currentStatus.getAudioLevels();

    // Helper lambda to find device index in options
    auto findDeviceIndex = [devices](const String& deviceName) -> int {
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].processName == deviceName) {
                return i;
            }
        }
        return 0;  // Default to first option
    };

    // Update main dropdown based on current tab
    if (ui_selectAudioDevice) {
        if (state.isInMasterTab() && state.primaryAudioDevice) {
            int index = findDeviceIndex(state.primaryAudioDevice->processName);
            lv_dropdown_set_selected(ui_selectAudioDevice, index);
        } else if (state.isInSingleTab() && state.selectedSingleDevice) {
            int index = findDeviceIndex(state.selectedSingleDevice->processName);
            lv_dropdown_set_selected(ui_selectAudioDevice, index);
        }
    }

    // Update balance dropdowns
    if (ui_selectAudioDevice1 && state.selectedDevice1) {
        int index = findDeviceIndex(state.selectedDevice1->processName);
        lv_dropdown_set_selected(ui_selectAudioDevice1, index);
    }

    if (ui_selectAudioDevice2 && state.selectedDevice2) {
        int index = findDeviceIndex(state.selectedDevice2->processName);
        lv_dropdown_set_selected(ui_selectAudioDevice2, index);
    }
}

void AudioUI::updateVolumeSlider() {
    const auto& state = AudioManager::getInstance().getState();
    lv_obj_t* slider = getCurrentVolumeSlider();

    if (slider) {
        int currentVolume = state.getCurrentSelectedVolume();
        lv_slider_set_value(slider, currentVolume, LV_ANIM_OFF);
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

void AudioUI::updateSingleTabLogo() {
    if (!initialized || !ui_img) {
        return;
    }

    const auto& state = AudioManager::getInstance().getState();

    // Only update if we're on the Single tab
    if (!state.isInSingleTab()) {
        lv_obj_add_flag(ui_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Get currently selected device on Single tab
    if (!state.selectedSingleDevice) {
        ESP_LOGD(TAG, "No device selected on Single tab - hiding logo");
        lv_obj_add_flag(ui_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    String processName = state.selectedSingleDevice->processName;
    ESP_LOGI(TAG, "Updating Single tab logo for process: %s", processName.c_str());

    // Get logo path from LogoManager
    String logoPath = Logo::LogoManager::getInstance().getLogoPath(processName.c_str());

    if (!logoPath.isEmpty()) {
        ESP_LOGI(TAG, "Found logo for %s at: %s", processName.c_str(), logoPath.c_str());

        // Set the logo image source
        lv_img_set_src(ui_img, logoPath.c_str());

        // Show the image
        lv_obj_remove_flag(ui_img, LV_OBJ_FLAG_HIDDEN);

        ESP_LOGI(TAG, "Logo displayed for %s", processName.c_str());
    } else {
        ESP_LOGD(TAG, "No logo found for %s - hiding image", processName.c_str());
        lv_obj_add_flag(ui_img, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace Audio
}  // namespace Application
