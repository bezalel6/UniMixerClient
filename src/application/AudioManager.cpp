#include "AudioManager.h"
#include "../hardware/DeviceManager.h"
#include "../messaging/TypedAudioHelpers.h"
#include "../messaging/MessageBus.h"
#include <esp_log.h>
#include <algorithm>

static const char* TAG = "AudioManager";

namespace Application {
namespace Audio {

AudioManager& AudioManager::getInstance() {
    static AudioManager instance;
    return instance;
}

bool AudioManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "AudioManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing AudioManager");

    // Clear state
    state.clear();
    callbacks.clear();

    initialized = true;
    ESP_LOGI(TAG, "AudioManager initialized successfully");
    return true;
}

void AudioManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing AudioManager");

    // Clear state and callbacks
    state.clear();
    callbacks.clear();

    initialized = false;
}

// === STATE ACCESS ===

AudioLevel* AudioManager::getDevice(const String& processName) {
    return state.findDevice(processName);
}

const AudioLevel* AudioManager::getDevice(const String& processName) const {
    return state.findDevice(processName);
}

// === EXTERNAL DATA INPUT ===

void AudioManager::onAudioStatusReceived(const AudioStatus& newStatus) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Received audio status with %d devices", newStatus.audioLevels.size());

    // Update our internal status
    state.currentStatus = newStatus;
    state.currentStatus.timestamp = Hardware::Device::getMillis();

    // Update individual device data
    for (const auto& device : newStatus.audioLevels) {
        updateDeviceFromStatus(device);
    }

    // Auto-select device if needed
    autoSelectDeviceIfNeeded();

    // Update timestamp
    updateTimestamp();

    // Notify listeners
    notifyStateChange(AudioStateChangeEvent::devicesUpdated());
}

// === USER ACTIONS ===

void AudioManager::selectDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
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
            // For balance tab, update the primary device
            state.selectedDevice1 = deviceName;
            break;

        default:
            ESP_LOGW(TAG, "Unknown tab state for device selection");
            return;
    }

    ESP_LOGI(TAG, "Selected device: %s in tab: %d", deviceName.c_str(), (int)state.currentTab);

    // Notify listeners if selection actually changed
    if (oldSelection != deviceName) {
        notifyStateChange(AudioStateChangeEvent::selectionChanged(deviceName));
    }
}

void AudioManager::selectBalanceDevices(const String& device1, const String& device2) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (!state.isInBalanceTab()) {
        ESP_LOGW(TAG, "Can only select balance devices in balance tab");
        return;
    }

    state.selectedDevice1 = device1;
    state.selectedDevice2 = device2;

    ESP_LOGI(TAG, "Selected balance devices: %s, %s", device1.c_str(), device2.c_str());

    notifyStateChange(AudioStateChangeEvent::selectionChanged(device1));
}

void AudioManager::setVolumeForCurrentDevice(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    String currentDevice = state.getCurrentSelectedDevice();
    setDeviceVolume(currentDevice, volume);
}

void AudioManager::setDeviceVolume(const String& deviceName, int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    // Clamp volume
    volume = constrain(volume, 0, 100);

    if (state.isInMasterTab()) {
        // Update default device volume
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.volume = volume;
            ESP_LOGI(TAG, "Set default device volume to %d", volume);
        } else {
            ESP_LOGW(TAG, "No default device available for master volume control");
            return;
        }
    } else {
        // Update selected device volume
        if (deviceName.isEmpty()) {
            ESP_LOGW(TAG, "No device specified for volume control");
            return;
        }

        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->volume = volume;
            device->lastUpdate = Hardware::Device::getMillis();
            device->stale = false;
            ESP_LOGI(TAG, "Updated device volume: %s = %d", deviceName.c_str(), volume);
        } else {
            // Create new device entry
            AudioLevel newDevice;
            newDevice.processName = deviceName;
            newDevice.friendlyName = deviceName;
            newDevice.volume = volume;
            newDevice.lastUpdate = Hardware::Device::getMillis();
            newDevice.stale = false;
            newDevice.isMuted = false;

            state.currentStatus.audioLevels.push_back(newDevice);
            ESP_LOGI(TAG, "Added new device: %s = %d", deviceName.c_str(), volume);
        }
    }

    // Update timestamp and notify
    updateTimestamp();
    notifyStateChange(AudioStateChangeEvent::volumeChanged(deviceName, volume));

    // Publish update
    publishStatusUpdate();
}

void AudioManager::muteCurrentDevice() {
    muteDevice(state.getCurrentSelectedDevice());
}

void AudioManager::unmuteCurrentDevice() {
    unmuteDevice(state.getCurrentSelectedDevice());
}

void AudioManager::muteDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (state.isInMasterTab()) {
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.isMuted = true;
            ESP_LOGI(TAG, "Muted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master mute control");
            return;
        }
    } else {
        if (deviceName.isEmpty()) {
            ESP_LOGW(TAG, "No device specified for mute control");
            return;
        }

        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->isMuted = true;
            ESP_LOGI(TAG, "Muted device: %s", deviceName.c_str());
        }
    }

    // Update timestamp and notify
    updateTimestamp();
    notifyStateChange(AudioStateChangeEvent::muteChanged(deviceName));
    publishStatusUpdate();
}

void AudioManager::unmuteDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (state.isInMasterTab()) {
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.isMuted = false;
            ESP_LOGI(TAG, "Unmuted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for master unmute control");
            return;
        }
    } else {
        if (deviceName.isEmpty()) {
            ESP_LOGW(TAG, "No device specified for unmute control");
            return;
        }

        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->isMuted = false;
            ESP_LOGI(TAG, "Unmuted device: %s", deviceName.c_str());
        }
    }

    // Update timestamp and notify
    updateTimestamp();
    notifyStateChange(AudioStateChangeEvent::muteChanged(deviceName));
    publishStatusUpdate();
}

void AudioManager::setCurrentTab(Events::UI::TabState tab) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    Events::UI::TabState oldTab = state.currentTab;
    state.currentTab = tab;

    ESP_LOGI(TAG, "Changed tab to: %d", (int)tab);

    if (oldTab != tab) {
        updateTimestamp();
        notifyStateChange(AudioStateChangeEvent::tabChanged(tab));
    }
}

// === EVENT SUBSCRIPTION ===

void AudioManager::subscribeToStateChanges(StateChangeCallback callback) {
    if (callback) {
        callbacks.push_back(callback);
    }
}

// === EXTERNAL COMMUNICATION ===

void AudioManager::publishStatusUpdate() {
    if (!Messaging::MessageBus::IsConnected()) {
        ESP_LOGW(TAG, "Cannot publish status update: No transport connected");
        return;
    }

    bool published = Messaging::AudioHelpers::PublishStatusUpdate(state.currentStatus);
    if (published) {
        ESP_LOGI(TAG, "Published status update with %d sessions", state.currentStatus.audioLevels.size());
    } else {
        ESP_LOGE(TAG, "Failed to publish status update");
    }
}

void AudioManager::publishStatusRequest(bool delayed) {
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

// === UTILITY ===

const char* AudioManager::getTabName(Events::UI::TabState tab) const {
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

// === PRIVATE METHODS ===

void AudioManager::notifyStateChange(const AudioStateChangeEvent& event) {
    for (auto& callback : callbacks) {
        callback(event);
    }
}

void AudioManager::autoSelectDeviceIfNeeded() {
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

void AudioManager::markDevicesAsStale() {
    for (auto& device : state.currentStatus.audioLevels) {
        if (!device.stale) {
            ESP_LOGI(TAG, "Marking device as stale: %s", device.processName.c_str());
        }
        device.stale = true;
    }
}

void AudioManager::updateDeviceFromStatus(const AudioLevel& deviceData) {
    AudioLevel* existing = getDevice(deviceData.processName);
    if (existing) {
        existing->volume = deviceData.volume;
        existing->isMuted = deviceData.isMuted;
        existing->friendlyName = deviceData.friendlyName;
        existing->state = deviceData.state;
        existing->lastUpdate = Hardware::Device::getMillis();
        existing->stale = false;
    } else {
        // Add new device
        AudioLevel newDevice = deviceData;
        newDevice.lastUpdate = Hardware::Device::getMillis();
        newDevice.stale = false;
        state.currentStatus.audioLevels.push_back(newDevice);
    }
}

String AudioManager::findBestDeviceToSelect() const {
    if (state.currentStatus.audioLevels.empty()) {
        return "";
    }

    // Look for a non-stale device first
    for (const auto& device : state.currentStatus.audioLevels) {
        if (!device.stale) {
            return device.processName;
        }
    }

    // If all devices are stale, just pick the first one
    return state.currentStatus.audioLevels[0].processName;
}

void AudioManager::updateTimestamp() {
    state.lastUpdateTime = millis();
}

void AudioManager::ensureValidSelections() {
    // Validate current selections and fix if needed
    if (!isDeviceAvailable(state.selectedMainDevice)) {
        state.selectedMainDevice = findBestDeviceToSelect();
    }

    if (!isDeviceAvailable(state.selectedDevice1)) {
        state.selectedDevice1 = findBestDeviceToSelect();
    }

    if (!isDeviceAvailable(state.selectedDevice2)) {
        state.selectedDevice2 = findBestDeviceToSelect();
    }
}

bool AudioManager::isDeviceAvailable(const String& deviceName) const {
    return getDevice(deviceName) != nullptr;
}

}  // namespace Audio
}  // namespace Application