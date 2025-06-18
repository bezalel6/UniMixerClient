#include "AudioStateManager.h"
#include "../hardware/DeviceManager.h"
#include "../messaging/TypedAudioHelpers.h"
#include "../messaging/MessageBus.h"
#include <esp_log.h>
#include <algorithm>

static const char* TAG = "AudioStateManager";

namespace Application {
namespace Audio {

AudioStateManager& AudioStateManager::getInstance() {
    static AudioStateManager instance;
    return instance;
}

bool AudioStateManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioStateManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioStateManager");

    // Clear state
    state.clear();
    callbacks.clear();

    initialized = true;
    ESP_LOGI(TAG, "AudioStateManager initialized successfully");
    return true;
}

void AudioStateManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioStateManager");

    // Clear state and callbacks
    state.clear();
    callbacks.clear();

    initialized = false;
}

void AudioStateManager::subscribeToStateChanges(StateChangeCallback callback) {
    if (callback) {
        callbacks.push_back(callback);
    }
}

void AudioStateManager::updateAudioStatus(const AudioStatus& newStatus) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Updating audio status with %d devices", newStatus.audioLevels.size());

    // Mark existing devices as stale
    // markDevicesAsStale();

    // Update status
    state.status = newStatus;
    state.status.timestamp = Hardware::Device::getMillis();

    // Update individual device data and mark as fresh
    for (const auto& levelData : newStatus.audioLevels) {
        updateDeviceFromStatus(levelData);
    }

    // Auto-select device if needed
    autoSelectDeviceIfNeeded();

    // Notify listeners
    AudioStateChangeEvent event;
    event.type = AudioStateChangeEvent::DEVICES_UPDATED;
    notifyStateChange(event);
}

void AudioStateManager::updateDeviceVolume(const String& processName, int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    AudioLevel* device = state.findDevice(processName);
    if (device) {
        device->volume = volume;
        device->lastUpdate = Hardware::Device::getMillis();
        device->stale = false;

        ESP_LOGI(TAG, "Updated device volume: %s = %d", processName.c_str(), volume);

        // Notify listeners
        AudioStateChangeEvent event;
        event.type = AudioStateChangeEvent::VOLUME_CHANGED;
        event.deviceName = processName;
        event.volume = volume;
        notifyStateChange(event);
    } else {
        // Create new device entry
        AudioLevel newLevel;
        newLevel.processName = processName;
        newLevel.volume = volume;
        newLevel.lastUpdate = Hardware::Device::getMillis();
        newLevel.stale = false;
        newLevel.isMuted = false;

        state.status.audioLevels.push_back(newLevel);

        ESP_LOGI(TAG, "Added new device: %s = %d", processName.c_str(), volume);

        // Notify listeners
        AudioStateChangeEvent event;
        event.type = AudioStateChangeEvent::DEVICES_UPDATED;
        notifyStateChange(event);
    }
}

void AudioStateManager::selectDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    String oldSelection = state.getCurrentSelectedDevice();

    // Update selection based on current tab
    switch (state.currentTab) {
        case Events::UI::TabState::MASTER:
            // Master tab doesn't have device selection - it uses default device
            ESP_LOGW(TAG, "Cannot select specific device in Master tab");
            return;

        case Events::UI::TabState::SINGLE:
            state.selectedMainDevice = deviceName;
            break;

        case Events::UI::TabState::BALANCE:
            // For balance tab, we need to handle both devices
            state.selectedDevice1 = deviceName;
            break;

        default:
            ESP_LOGW(TAG, "Unknown tab state for device selection");
            return;
    }

    ESP_LOGI(TAG, "Selected device: %s in tab: %d", deviceName.c_str(), (int)state.currentTab);

    // Notify listeners if selection actually changed
    if (oldSelection != deviceName) {
        AudioStateChangeEvent event;
        event.type = AudioStateChangeEvent::SELECTION_CHANGED;
        event.deviceName = deviceName;
        event.tab = state.currentTab;
        notifyStateChange(event);
    }
}

void AudioStateManager::selectBalanceDevices(const String& device1, const String& device2) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    if (state.currentTab != Events::UI::TabState::BALANCE) {
        ESP_LOGW(TAG, "Can only select balance devices in balance tab");
        return;
    }

    state.selectedDevice1 = device1;
    state.selectedDevice2 = device2;

    ESP_LOGI(TAG, "Selected balance devices: %s, %s", device1.c_str(), device2.c_str());

    AudioStateChangeEvent event;
    event.type = AudioStateChangeEvent::SELECTION_CHANGED;
    event.tab = state.currentTab;
    notifyStateChange(event);
}

void AudioStateManager::setTab(Events::UI::TabState tab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    Events::UI::TabState oldTab = state.currentTab;
    state.currentTab = tab;

    ESP_LOGI(TAG, "Changed tab to: %d", (int)tab);

    if (oldTab != tab) {
        AudioStateChangeEvent event;
        event.type = AudioStateChangeEvent::TAB_CHANGED;
        event.tab = tab;
        notifyStateChange(event);
    }
}

void AudioStateManager::setVolumeForCurrentDevice(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    // Clamp volume
    volume = constrain(volume, 0, 100);

    if (state.isInMasterTab()) {
        // Update default device volume
        if (state.status.hasDefaultDevice) {
            state.status.defaultDevice.volume = volume / 100.0f;
            ESP_LOGI(TAG, "Set default device volume to %d", volume);
        } else {
            ESP_LOGW(TAG, "No default device available for master volume control");
            return;
        }
    } else {
        // Update selected device volume
        String currentDevice = state.getCurrentSelectedDevice();
        if (currentDevice.isEmpty()) {
            ESP_LOGW(TAG, "No device selected for volume control");
            return;
        }

        updateDeviceVolume(currentDevice, volume);
    }

    // Publish update
    publishStatusUpdate();
}

void AudioStateManager::muteCurrentDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    if (state.isInMasterTab()) {
        if (state.status.hasDefaultDevice) {
            state.status.defaultDevice.isMuted = true;
            ESP_LOGI(TAG, "Muted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master mute control");
            return;
        }
    } else {
        String currentDevice = state.getCurrentSelectedDevice();
        if (currentDevice.isEmpty()) {
            ESP_LOGW(TAG, "No device selected for mute control");
            return;
        }

        AudioLevel* device = state.findDevice(currentDevice);
        if (device) {
            device->isMuted = true;
            ESP_LOGI(TAG, "Muted device: %s", currentDevice.c_str());
        }
    }

    // Notify and publish
    AudioStateChangeEvent event;
    event.type = AudioStateChangeEvent::MUTE_CHANGED;
    event.deviceName = state.getCurrentSelectedDevice();
    notifyStateChange(event);

    publishStatusUpdate();
}

void AudioStateManager::unmuteCurrentDevice() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioStateManager not initialized");
        return;
    }

    if (state.isInMasterTab()) {
        if (state.status.hasDefaultDevice) {
            state.status.defaultDevice.isMuted = false;
            ESP_LOGI(TAG, "Unmuted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master unmute control");
            return;
        }
    } else {
        String currentDevice = state.getCurrentSelectedDevice();
        if (currentDevice.isEmpty()) {
            ESP_LOGW(TAG, "No device selected for unmute control");
            return;
        }

        AudioLevel* device = state.findDevice(currentDevice);
        if (device) {
            device->isMuted = false;
            ESP_LOGI(TAG, "Unmuted device: %s", currentDevice.c_str());
        }
    }

    // Notify and publish
    AudioStateChangeEvent event;
    event.type = AudioStateChangeEvent::MUTE_CHANGED;
    event.deviceName = state.getCurrentSelectedDevice();
    notifyStateChange(event);

    publishStatusUpdate();
}

std::vector<AudioLevel> AudioStateManager::getAllDevices() const {
    return state.status.audioLevels;
}

AudioLevel* AudioStateManager::getDevice(const String& processName) {
    return state.findDevice(processName);
}

String AudioStateManager::getCurrentDevice() const {
    return state.getCurrentSelectedDevice();
}

int AudioStateManager::getCurrentVolume() const {
    return state.getCurrentSelectedVolume();
}

bool AudioStateManager::isCurrentDeviceMuted() const {
    return state.isCurrentDeviceMuted();
}

void AudioStateManager::publishStatusUpdate() {
    if (!Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish status update: No transport connected");
        return;
    }

    bool published = Messaging::AudioHelpers::PublishStatusUpdate(state.status);
    if (published) {
        ESP_LOGI(TAG, "Published status update with %d sessions", state.status.audioLevels.size());
    } else {
        ESP_LOGE(TAG, "Failed to publish status update");
    }
}

void AudioStateManager::publishStatusRequest(bool delayed) {
    if (!delayed && !Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish status request: No transport connected");
        return;
    }

    bool published;
    if (delayed) {
        published = Messaging::AudioHelpers::PublishStatusRequestDelayed();
    } else {
        published = Messaging::AudioHelpers::PublishStatusRequest();
    }

    if (published) {
        ESP_LOGI(TAG, "Published %sstatus request", delayed ? "delayed " : "");
    } else {
        ESP_LOGE(TAG, "Failed to publish %sstatus request", delayed ? "delayed " : "");
    }
}

// Private methods

void AudioStateManager::notifyStateChange(const AudioStateChangeEvent& event) {
    for (auto& callback : callbacks) {
        callback(event);
    }
}

void AudioStateManager::autoSelectDeviceIfNeeded() {
    // Only auto-select for tabs that need device selection
    if (state.isInMasterTab()) {
        return;  // Master tab uses default device
    }

    if (state.hasValidSelection()) {
        return;  // Already has valid selection
    }

    String deviceToSelect = findBestDeviceToSelect();
    if (!deviceToSelect.isEmpty()) {
        selectDevice(deviceToSelect);
        ESP_LOGI(TAG, "Auto-selected device: %s", deviceToSelect.c_str());
    }
}

void AudioStateManager::markDevicesAsStale() {
    for (auto& level : state.status.audioLevels) {
        if (!level.stale) {
            ESP_LOGI(TAG, "Marking device as stale: %s", level.processName.c_str());
        }
        level.stale = true;
    }
}

void AudioStateManager::updateDeviceFromStatus(const AudioLevel& levelData) {
    AudioLevel* existing = state.findDevice(levelData.processName);
    if (existing) {
        existing->volume = levelData.volume;
        existing->isMuted = levelData.isMuted;
        existing->lastUpdate = Hardware::Device::getMillis();
        existing->stale = false;
    } else {
        // Add new device
        AudioLevel newLevel = levelData;
        newLevel.lastUpdate = Hardware::Device::getMillis();
        newLevel.stale = false;
        state.status.audioLevels.push_back(newLevel);
    }
}

String AudioStateManager::findBestDeviceToSelect() const {
    if (state.status.audioLevels.empty()) {
        return "";
    }

    // Look for a non-stale device first
    for (const auto& level : state.status.audioLevels) {
        if (!level.stale) {
            return level.processName;
        }
    }

    // If all devices are stale, just pick the first one
    return state.status.audioLevels[0].processName;
}

}  // namespace Audio
}  // namespace Application