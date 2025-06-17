#include "AudioUIController.h"
#include "DebugUtils.h"
#include "ui/screens/ui_screenMain.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "AudioUIController";

namespace Application {
namespace Audio {

AudioUIController& AudioUIController::getInstance() {
    static AudioUIController instance;
    return instance;
}

bool AudioUIController::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioUIController already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioUIController");

    // Initialize device selector manager
    deviceSelectorManager = std::make_unique<UI::Components::DeviceSelectorManager>();
    if (!deviceSelectorManager) {
        ESP_LOGE(TAG, "Failed to create device selector manager");
        return false;
    }

    // Setup device selector callbacks
    setupDeviceSelectorCallbacks();

    // Subscribe to state changes
    AudioStateManager::getInstance().subscribeToStateChanges(
        [this](const AudioStateChangeEvent& event) {
            onAudioStateChanged(event);
        });

    initialized = true;
    ESP_LOGI(TAG, "AudioUIController initialized successfully");
    return true;
}

void AudioUIController::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioUIController");

    // Clear device selector manager
    if (deviceSelectorManager) {
        deviceSelectorManager.reset();
    }

    initialized = false;
}

void AudioUIController::onVolumeSliderChanged(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUIController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Volume slider changed to: %d", volume);
    AudioStateManager::getInstance().setVolumeForCurrentDevice(volume);
}

void AudioUIController::onDeviceDropdownChanged(lv_obj_t* dropdown, const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUIController not initialized");
        return;
    }

    if (shouldSuppressDropdownEvents()) {
        ESP_LOGD(TAG, "Suppressing dropdown event");
        return;
    }

    ESP_LOGI(TAG, "Device dropdown changed to: %s", deviceName.c_str());

    // Handle different dropdowns based on current tab
    const auto& state = AudioStateManager::getInstance().getState();

    if (state.isInBalanceTab()) {
        // For balance tab, handle dual selection
        if (dropdown == ui_selectAudioDevice1) {
            AudioStateManager::getInstance().selectBalanceDevices(deviceName, state.selectedDevice2);
        } else if (dropdown == ui_selectAudioDevice2) {
            AudioStateManager::getInstance().selectBalanceDevices(state.selectedDevice1, deviceName);
        }
    } else {
        // For single/master tabs
        AudioStateManager::getInstance().selectDevice(deviceName);
    }
}

void AudioUIController::onTabChanged(Events::UI::TabState newTab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUIController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Tab changed to: %s", getCurrentTabName().c_str());
    AudioStateManager::getInstance().setTab(newTab);
}

void AudioUIController::onMuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUIController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Mute button pressed");
    AudioStateManager::getInstance().muteCurrentDevice();
}

void AudioUIController::onUnmuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioUIController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Unmute button pressed");
    AudioStateManager::getInstance().unmuteCurrentDevice();
}

bool AudioUIController::shouldSuppressArcEvents() const {
    return AudioStateManager::getInstance().isSuppressingArcEvents();
}

bool AudioUIController::shouldSuppressDropdownEvents() const {
    return AudioStateManager::getInstance().isSuppressingDropdownEvents();
}

// Private methods

void AudioUIController::onAudioStateChanged(const AudioStateChangeEvent& event) {
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
            updateAllUI();
            break;

        case AudioStateChangeEvent::MUTE_CHANGED:
            updateMuteButtons();
            updateDefaultDeviceLabel();
            break;
    }
}

void AudioUIController::updateVolumeDisplay() {
    const auto& state = AudioStateManager::getInstance().getState();
    int currentVolume = state.getCurrentSelectedVolume();

    // Use message handler for thread-safe UI updates
    LVGLMessageHandler::updateVolumeLevel(currentVolume);

    ESP_LOGD(TAG, "Updated volume display to: %d", currentVolume);
}

void AudioUIController::updateDeviceSelectors() {
    if (!deviceSelectorManager) {
        return;
    }

    const auto& devices = AudioStateManager::getInstance().getAllDevices();

    // Update device selector manager
    deviceSelectorManager->updateAvailableDevices(devices);

    // Update dropdown options
    updateDropdownOptions(devices);

    ESP_LOGD(TAG, "Updated device selectors with %d devices", devices.size());
}

void AudioUIController::updateDefaultDeviceLabel() {
    const auto& state = AudioStateManager::getInstance().getState();

    if (state.status.hasDefaultDevice) {
        LVGLMessageHandler::updateDefaultDevice(
            state.status.defaultDevice.friendlyName.c_str());
        ESP_LOGD(TAG, "Updated default device label: %s",
                 state.status.defaultDevice.friendlyName.c_str());
    }
}

void AudioUIController::updateMuteButtons() {
    // This could be expanded to update mute button states
    // For now, mute state is handled implicitly through volume display
    ESP_LOGD(TAG, "Updated mute buttons");
}

void AudioUIController::updateAllUI() {
    updateDeviceSelectors();
    updateVolumeDisplay();
    updateDefaultDeviceLabel();
    updateMuteButtons();
    ESP_LOGD(TAG, "Updated all UI elements");
}

void AudioUIController::setupDeviceSelectorCallbacks() {
    if (!deviceSelectorManager) {
        return;
    }

    // Main device selection callback
    deviceSelectorManager->setMainSelectionCallback(
        [this](const UI::Components::DeviceSelection& selection) {
            ESP_LOGI(TAG, "Main selection changed to: %s", selection.getValue().c_str());
            LOG_TO_UI(ui_txtAreaDebugLog,
                      String("DeviceSelector: Main selection changed to '") +
                          selection.getValue() + String("'"));

            // Update main dropdown if not suppressing events
            if (!shouldSuppressDropdownEvents() && ui_selectAudioDevice) {
                AudioStateManager::getInstance().setSuppressDropdownEvents(true);
                // Find and set dropdown index based on selection
                updateDropdownSelections();
                AudioStateManager::getInstance().setSuppressDropdownEvents(false);
            }
        });

    // Balance selection callback
    deviceSelectorManager->setBalanceSelectionCallback(
        [this](const UI::Components::BalanceSelection& selection) {
            ESP_LOGI(TAG, "Balance selection changed: %s, %s",
                     selection.device1.getValue().c_str(),
                     selection.device2.getValue().c_str());
            LOG_TO_UI(ui_txtAreaDebugLog, String("DeviceSelector: Balance selection changed"));

            if (!shouldSuppressDropdownEvents()) {
                AudioStateManager::getInstance().setSuppressDropdownEvents(true);
                updateDropdownSelections();
                AudioStateManager::getInstance().setSuppressDropdownEvents(false);
            }
        });

    // Device list callback
    deviceSelectorManager->setDeviceListCallback(
        [this](const std::vector<AudioLevel>& devices) {
            ESP_LOGI(TAG, "Device list updated with %d devices", devices.size());
            LOG_TO_UI(ui_txtAreaDebugLog,
                      String("DeviceSelector: Device list updated with ") +
                          String(devices.size()) + String(" devices"));

            // Log device details
            for (size_t i = 0; i < devices.size(); i++) {
                const auto& device = devices[i];
                String deviceInfo = String("  [") + String(i) + String("] ") +
                                    device.processName + String(" (") +
                                    String(device.volume) + String("%)");
                if (device.isMuted) {
                    deviceInfo += String(" [MUTED]");
                }
                if (device.stale) {
                    deviceInfo += String(" [STALE]");
                }
                LOG_TO_UI(ui_txtAreaDebugLog, deviceInfo);
            }

            updateDropdownOptions(devices);
        });
}

void AudioUIController::updateDropdownOptions(const std::vector<AudioLevel>& devices) {
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
    AudioStateManager::getInstance().setSuppressDropdownEvents(true);

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

    AudioStateManager::getInstance().setSuppressDropdownEvents(false);
}

void AudioUIController::updateDropdownSelections() {
    const auto& state = AudioStateManager::getInstance().getState();
    const auto& devices = state.status.audioLevels;

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

lv_obj_t* AudioUIController::getCurrentVolumeSlider() const {
    const auto& state = AudioStateManager::getInstance().getState();

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

String AudioUIController::getCurrentTabName() const {
    const auto& state = AudioStateManager::getInstance().getState();

    switch (state.currentTab) {
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

}  // namespace Audio
}  // namespace Application