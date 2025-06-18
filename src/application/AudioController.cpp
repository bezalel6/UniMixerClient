#include "AudioController.h"
#include "DebugUtils.h"
#include "ui/screens/ui_screenMain.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "AudioController";

namespace Application {
namespace Audio {

AudioController& AudioController::getInstance() {
    static AudioController instance;
    return instance;
}

bool AudioController::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioController already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioController");

    // Initialize the core components
    if (!AudioStateManager::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioStateManager");
        return false;
    }

    // Initialize device selector manager
    deviceSelectorManager = std::make_unique<UI::Components::DeviceSelectorManager>();
    if (!deviceSelectorManager) {
        ESP_LOGE(TAG, "Failed to create device selector manager");
        AudioStateManager::getInstance().deinit();
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
    ESP_LOGI(TAG, "AudioController initialized successfully");
    return true;
}

void AudioController::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioController");

    // Clear device selector manager
    if (deviceSelectorManager) {
        deviceSelectorManager.reset();
    }

    // Deinitialize components
    AudioStateManager::getInstance().deinit();

    initialized = false;
}

// === External Interface Methods (from AudioStatusManager) ===

void AudioController::onAudioStatusReceived(const AudioStatus& status) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Received audio status update with %d processes", status.audioLevels.size());
    AudioStateManager::getInstance().updateAudioStatus(status);
}

void AudioController::publishStatusUpdate() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    AudioStateManager::getInstance().publishStatusUpdate();
}

void AudioController::publishAudioStatusRequest(bool delayed) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    AudioStateManager::getInstance().publishStatusRequest(delayed);
}

String AudioController::getSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return "";
    }

    return AudioStateManager::getInstance().getCurrentDevice();
}

AudioLevel* AudioController::getAudioLevel(const String& processName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return nullptr;
    }

    return AudioStateManager::getInstance().getDevice(processName);
}

std::vector<AudioLevel> AudioController::getAllAudioLevels() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return {};
    }

    return AudioStateManager::getInstance().getAllDevices();
}

AudioStatus AudioController::getCurrentAudioStatus() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return {};
    }

    return AudioStateManager::getInstance().getState().status;
}

Events::UI::TabState AudioController::getCurrentTab() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return Events::UI::TabState::MASTER;
    }

    return AudioStateManager::getInstance().getState().currentTab;
}

void AudioController::setCurrentTab(Events::UI::TabState tab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    AudioStateManager::getInstance().setTab(tab);
}

const char* AudioController::getTabName(Events::UI::TabState tab) {
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

bool AudioController::isSuppressingArcEvents() const {
    if (!initialized) {
        return false;
    }

    return AudioStateManager::getInstance().isSuppressingArcEvents();
}

bool AudioController::isSuppressingDropdownEvents() const {
    if (!initialized) {
        return false;
    }

    return AudioStateManager::getInstance().isSuppressingDropdownEvents();
}

String AudioController::getDropdownSelection(lv_obj_t* dropdown) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return "";
    }

    const auto& state = AudioStateManager::getInstance().getState();

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

void AudioController::updateVolumeArcFromSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    // Label updates are now handled automatically by the visual event handler
    // This method is kept for backward compatibility but no longer needs to update labels
    // The UI controller automatically updates the display when state changes
}

void AudioController::updateVolumeArcLabel(int volume) {
    // DEPRECATED: Volume label updates are now handled automatically by the visual event handler
    // This method is kept for backward compatibility but no longer performs any operations
    // Labels are updated in real-time during arc dragging by volumeArcVisualHandler
}

lv_obj_t* AudioController::getCurrentVolumeSlider() const {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return nullptr;
    }

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

void AudioController::onAudioLevelsChangedUI() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    // This method is called periodically by TaskManager to refresh UI
    // In the new architecture, this should trigger a comprehensive UI update
    // The UI controller automatically updates when state changes, but this provides
    // a way to force a refresh if needed
    updateVolumeArcFromSelectedDevice();
}

// === UI Event Handlers ===

void AudioController::onVolumeSliderChanged(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Volume slider changed to: %d", volume);
    AudioStateManager::getInstance().setVolumeForCurrentDevice(volume);
}

void AudioController::onDeviceDropdownChanged(lv_obj_t* dropdown, const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    if (isSuppressingDropdownEvents()) {
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

void AudioController::onTabChanged(Events::UI::TabState newTab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Tab changed to: %s", getCurrentTabName().c_str());
    AudioStateManager::getInstance().setTab(newTab);
}

void AudioController::onMuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Mute button pressed");
    AudioStateManager::getInstance().muteCurrentDevice();
}

void AudioController::onUnmuteButtonPressed() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioController not initialized");
        return;
    }

    ESP_LOGI(TAG, "Unmute button pressed");
    AudioStateManager::getInstance().unmuteCurrentDevice();
}

// === Private Methods ===

void AudioController::onAudioStateChanged(const AudioStateChangeEvent& event) {
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

void AudioController::updateVolumeDisplay() {
    const auto& state = AudioStateManager::getInstance().getState();
    int currentVolume = state.getCurrentSelectedVolume();

    // Use message handler for thread-safe UI updates
    LVGLMessageHandler::updateVolumeLevel(currentVolume);

    ESP_LOGD(TAG, "Updated volume display to: %d", currentVolume);
}

void AudioController::updateDeviceSelectors() {
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

void AudioController::updateDefaultDeviceLabel() {
    const auto& state = AudioStateManager::getInstance().getState();

    if (state.status.hasDefaultDevice) {
        LVGLMessageHandler::updateDefaultDevice(
            state.status.defaultDevice.friendlyName.c_str());
        ESP_LOGD(TAG, "Updated default device label: %s",
                 state.status.defaultDevice.friendlyName.c_str());
    }
}

void AudioController::updateMuteButtons() {
    // This could be expanded to update mute button states
    // For now, mute state is handled implicitly through volume display
    ESP_LOGD(TAG, "Updated mute buttons");
}

void AudioController::updateAllUI() {
    updateDeviceSelectors();
    updateVolumeDisplay();
    updateDefaultDeviceLabel();
    updateMuteButtons();
    ESP_LOGD(TAG, "Updated all UI elements");
}

void AudioController::setupDeviceSelectorCallbacks() {
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
            if (!isSuppressingDropdownEvents() && ui_selectAudioDevice) {
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

            if (!isSuppressingDropdownEvents()) {
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

void AudioController::updateDropdownOptions(const std::vector<AudioLevel>& devices) {
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

void AudioController::updateDropdownSelections() {
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

String AudioController::getCurrentTabName() const {
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