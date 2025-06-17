// Refactored AudioStatusManager - now uses clean state-driven architecture
// Old implementation backed up and replaced with new modular design

#include "AudioStatusManager.h"
#include "ui/screens/ui_screenMain.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "AudioStatusManager";

namespace Application {
namespace Audio {

bool StatusManager::initialized = false;

bool StatusManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioStatusManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioStatusManager");

    // Initialize the core components
    if (!AudioStateManager::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioStateManager");
        return false;
    }

    if (!AudioUIController::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioUIController");
        AudioStateManager::getInstance().deinit();
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

    // Deinitialize components
    AudioUIController::getInstance().deinit();
    AudioStateManager::getInstance().deinit();

    initialized = false;
}

void StatusManager::onAudioStatusReceived(const AudioStatus& status) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Received audio status update with %d processes", status.audioLevels.size());
    AudioStateManager::getInstance().updateAudioStatus(status);
}

void StatusManager::setSelectedDeviceVolume(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioUIController::getInstance().onVolumeSliderChanged(volume);
}

void StatusManager::muteSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioUIController::getInstance().onMuteButtonPressed();
}

void StatusManager::unmuteSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioUIController::getInstance().onUnmuteButtonPressed();
}

void StatusManager::setDropdownSelection(lv_obj_t* dropdown, const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioUIController::getInstance().onDeviceDropdownChanged(dropdown, deviceName);
}

String StatusManager::getDropdownSelection(lv_obj_t* dropdown) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
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

String StatusManager::getSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return "";
    }

    return AudioStateManager::getInstance().getCurrentDevice();
}

AudioLevel* StatusManager::getAudioLevel(const String& processName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return nullptr;
    }

    return AudioStateManager::getInstance().getDevice(processName);
}

std::vector<AudioLevel> StatusManager::getAllAudioLevels() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return {};
    }

    return AudioStateManager::getInstance().getAllDevices();
}

AudioStatus StatusManager::getCurrentAudioStatus() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return {};
    }

    return AudioStateManager::getInstance().getState().status;
}

Events::UI::TabState StatusManager::getCurrentTab() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return Events::UI::TabState::MASTER;
    }

    return AudioStateManager::getInstance().getState().currentTab;
}

void StatusManager::setCurrentTab(Events::UI::TabState tab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioUIController::getInstance().onTabChanged(tab);
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

bool StatusManager::isSuppressingArcEvents() {
    if (!initialized) {
        return false;
    }

    return AudioUIController::getInstance().shouldSuppressArcEvents();
}

bool StatusManager::isSuppressingDropdownEvents() {
    if (!initialized) {
        return false;
    }

    return AudioUIController::getInstance().shouldSuppressDropdownEvents();
}

void StatusManager::publishStatusUpdate() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioStateManager::getInstance().publishStatusUpdate();
}

void StatusManager::publishAudioStatusRequest(bool delayed) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    AudioStateManager::getInstance().publishStatusRequest(delayed);
}

void StatusManager::updateVolumeArcFromSelectedDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    // This is now handled automatically by the UI controller when state changes
    // But we can trigger an update if needed
    const auto& state = AudioStateManager::getInstance().getState();
    int volume = state.getCurrentSelectedVolume();
    updateVolumeArcLabel(volume);
}

void StatusManager::updateVolumeArcLabel(int volume) {
    // Volume label updates are now handled by the LVGLMessageHandler
    // This method is kept for backward compatibility but doesn't need to do anything
    // since the UI controller automatically updates the display
}

lv_obj_t* StatusManager::getCurrentVolumeSlider() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
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

void StatusManager::onAudioLevelsChangedUI() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStatusManager not initialized");
        return;
    }

    // This method is called periodically by TaskManager to refresh UI
    // In the new architecture, this should trigger a comprehensive UI update
    // The UI controller automatically updates when state changes, but this provides
    // a way to force a refresh if needed
    updateVolumeArcFromSelectedDevice();
}

}  // namespace Audio
}  // namespace Application