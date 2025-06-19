#include "AudioManager.h"
#include "../hardware/DeviceManager.h"
#include "../messaging/MessageAPI.h"
#include "../messaging/MessageConfig.h"
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

    // Subscribe to audio status updates from the messaging system
    Messaging::MessageAPI::onAudioStatus([this](const Messaging::AudioStatusData& data) {
        // Convert AudioStatusData back to AudioStatus for compatibility
        ESP_LOGD(TAG, "Origin: %s", (!data.originatingDeviceId || data.originatingDeviceId.isEmpty()) ? "None" : data.originatingDeviceId.c_str());

        // DEBUG: Log default device volume conversion
        if (data.hasDefaultDevice) {
            ESP_LOGI(TAG, "Received default device: %s, volume: %d",
                     data.defaultDevice.friendlyName.c_str(), data.defaultDevice.volume);
        }

        AudioStatus status;
        status.setAudioLevels(data.audioLevels);
        status.defaultDevice = data.defaultDevice;
        status.hasDefaultDevice = data.hasDefaultDevice;
        status.timestamp = data.timestamp;

        // Process the audio status
        this->onAudioStatusReceived(status);
    });

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
    ESP_LOGI(TAG, "Received audio status with %d devices - triggering reactive updates", newStatus.getDeviceCount());

    // Store current device selections by name (before hash map gets replaced)
    String currentPrimaryDeviceName = state.primaryAudioDevice ? state.primaryAudioDevice->processName : "";
    String currentSingleDeviceName = state.selectedSingleDevice ? state.selectedSingleDevice->processName : "";
    String currentDevice1Name = state.selectedDevice1 ? state.selectedDevice1->processName : "";
    String currentDevice2Name = state.selectedDevice2 ? state.selectedDevice2->processName : "";

    // Check if this is significantly new data (device count changed)
    bool significantUpdate = (newStatus.getDeviceCount() != state.currentStatus.getDeviceCount());

    // Update our internal status (this replaces the hash map, invalidating pointers)
    state.currentStatus = newStatus;
    state.currentStatus.timestamp = Hardware::Device::getMillis();

    // Refresh device pointers to point to new hash map entries
    refreshDevicePointers(currentPrimaryDeviceName, currentSingleDeviceName, currentDevice1Name, currentDevice2Name);

    // Perform smart auto-selection but only if we don't have valid selections
    if (!state.hasValidSelection() || significantUpdate) {
        performSmartAutoSelection();
    }

    // If this was a significant update (new devices appeared/disappeared),
    // be extra careful about ensuring good selections
    if (significantUpdate) {
        ESP_LOGI(TAG, "Significant device update detected - ensuring valid selections");

        // Ensure we have valid selections for current tab context
        ensureValidSelections();

        // If we're in a tab that needs selections but still don't have them,
        // this might indicate all devices disappeared - handle gracefully
        if ((state.isInSingleTab() && !state.selectedSingleDevice) ||
            (state.isInBalanceTab() && (!state.selectedDevice1 || !state.selectedDevice2))) {
            ESP_LOGW(TAG, "No suitable devices available for current tab: %s", getTabName(state.currentTab));
        }
    }

    // Update timestamp
    updateTimestamp();

    // Notify listeners
    notifyStateChange(AudioStateChangeEvent::devicesUpdated());

    ESP_LOGI(TAG, "Reactive audio status processing complete");
}

// === USER ACTIONS ===

void AudioManager::selectDevice(const String& deviceName) {
    AudioLevel* device = state.findDevice(deviceName);
    if (device) {
        selectDevice(device);
    } else {
        ESP_LOGW(TAG, "Device not found: %s", deviceName.c_str());
    }
}

void AudioManager::selectDevice(AudioLevel* device) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (!device) {
        ESP_LOGW(TAG, "Cannot select null device");
        return;
    }

    AudioLevel* oldSelection = state.getCurrentSelectedDevice();

    // Update selection based on current tab
    switch (state.currentTab) {
        case Events::UI::TabState::MASTER:
            // Master tab controls primary/default device
            state.primaryAudioDevice = device;
            break;
        case Events::UI::TabState::SINGLE:
            state.selectedSingleDevice = device;
            break;

        case Events::UI::TabState::BALANCE:
            // For balance tab, update the primary device
            state.selectedDevice1 = device;
            break;

        default:
            ESP_LOGW(TAG, "Unknown tab state for device selection");
            return;
    }

    ESP_LOGI(TAG, "Selected device: %s in tab: %d", device->processName.c_str(), (int)state.currentTab);

    // Notify listeners if selection actually changed
    if (oldSelection != device) {
        notifyStateChange(AudioStateChangeEvent::selectionChanged(device->processName));
    }
}

void AudioManager::selectBalanceDevices(const String& device1Name, const String& device2Name) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (!state.isInBalanceTab()) {
        ESP_LOGW(TAG, "Can only select balance devices in balance tab");
        return;
    }

    AudioLevel* device1 = state.findDevice(device1Name);
    AudioLevel* device2 = state.findDevice(device2Name);

    if (!device1 || !device2) {
        ESP_LOGW(TAG, "One or both balance devices not found: %s, %s", device1Name.c_str(), device2Name.c_str());
        return;
    }

    state.selectedDevice1 = device1;
    state.selectedDevice2 = device2;

    ESP_LOGI(TAG, "Selected balance devices: %s, %s", device1Name.c_str(), device2Name.c_str());

    notifyStateChange(AudioStateChangeEvent::selectionChanged(device1Name));
}

void AudioManager::setVolumeForCurrentDevice(int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    if (state.isInMasterTab()) {
        // Master tab controls the default device directly
        ESP_LOGI(TAG, "Master tab: Setting default device volume to %d", volume);
        if (state.currentStatus.hasDefaultDevice) {
            setDeviceVolume("", volume);  // Empty string triggers default device logic
        } else {
            ESP_LOGW(TAG, "No default device available for master volume control");
        }
    } else {
        // Single/Balance tabs use selected devices
        AudioLevel* currentDevice = state.getCurrentSelectedDevice();
        if (currentDevice) {
            ESP_LOGI(TAG, "%s tab: Setting session device '%s' volume to %d",
                     getTabName(state.currentTab), currentDevice->processName.c_str(), volume);
            setDeviceVolume(currentDevice->processName, volume);
        } else {
            ESP_LOGW(TAG, "No device selected for volume control in current tab: %s", getTabName(state.currentTab));
        }
    }
}

void AudioManager::setDeviceVolume(const String& deviceName, int volume) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    // Clamp volume
    volume = constrain(volume, 0, 100);

    // Determine target based on deviceName parameter, NOT current tab
    if (deviceName.isEmpty()) {
        // Empty deviceName = update default device
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.volume = volume;
            ESP_LOGI(TAG, "Set default device volume to %d", volume);
        } else {
            ESP_LOGW(TAG, "No default device available for volume control");
            return;
        }
    } else {
        // Non-empty deviceName = update specific session device
        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->volume = volume;
            device->lastUpdate = Hardware::Device::getMillis();
            device->stale = false;
            ESP_LOGI(TAG, "Updated session device volume: %s = %d", deviceName.c_str(), volume);
        } else {
            // Create new device entry
            AudioLevel newDevice;
            newDevice.processName = deviceName;
            newDevice.friendlyName = deviceName;
            newDevice.volume = volume;
            newDevice.lastUpdate = Hardware::Device::getMillis();
            newDevice.stale = false;
            newDevice.isMuted = false;

            state.currentStatus.addOrUpdateDevice(newDevice);
            ESP_LOGI(TAG, "Added new session device: %s = %d", deviceName.c_str(), volume);
        }
    }

    // Update timestamp and notify
    updateTimestamp();
    notifyStateChange(AudioStateChangeEvent::volumeChanged(deviceName, volume));

    // Publish update
    publishStatusUpdate();
}

void AudioManager::muteCurrentDevice() {
    AudioLevel* currentDevice = state.getCurrentSelectedDevice();
    if (currentDevice) {
        muteDevice(currentDevice->processName);
    } else {
        ESP_LOGW(TAG, "No device selected for mute control");
    }
}

void AudioManager::unmuteCurrentDevice() {
    AudioLevel* currentDevice = state.getCurrentSelectedDevice();
    if (currentDevice) {
        unmuteDevice(currentDevice->processName);
    } else {
        ESP_LOGW(TAG, "No device selected for unmute control");
    }
}

void AudioManager::muteDevice(const String& deviceName) {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    // Determine target based on deviceName parameter, NOT current tab
    if (deviceName.isEmpty()) {
        // Empty deviceName = mute default device
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.isMuted = true;
            ESP_LOGI(TAG, "Muted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for mute control");
            return;
        }
    } else {
        // Non-empty deviceName = mute specific session device
        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->isMuted = true;
            ESP_LOGI(TAG, "Muted session device: %s", deviceName.c_str());
        } else {
            ESP_LOGW(TAG, "Session device not found for mute: %s", deviceName.c_str());
            return;
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

    // Determine target based on deviceName parameter, NOT current tab
    if (deviceName.isEmpty()) {
        // Empty deviceName = unmute default device
        if (state.currentStatus.hasDefaultDevice) {
            state.currentStatus.defaultDevice.isMuted = false;
            ESP_LOGI(TAG, "Unmuted default device");
        } else {
            ESP_LOGW(TAG, "No default device available for unmute control");
            return;
        }
    } else {
        // Non-empty deviceName = unmute specific session device
        AudioLevel* device = getDevice(deviceName);
        if (device) {
            device->isMuted = false;
            ESP_LOGI(TAG, "Unmuted session device: %s", deviceName.c_str());
        } else {
            ESP_LOGW(TAG, "Session device not found for unmute: %s", deviceName.c_str());
            return;
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
    if (!Messaging::MessageAPI::isHealthy()) {
        ESP_LOGW(TAG, "Cannot publish status update: Messaging system not healthy");
        return;
    }

    // Convert AudioStatus to AudioStatusData and publish
    Messaging::AudioStatusData statusData;
    statusData.audioLevels = state.currentStatus.getAudioLevels();
    statusData.defaultDevice = state.currentStatus.defaultDevice;
    statusData.hasDefaultDevice = state.currentStatus.hasDefaultDevice;
    statusData.timestamp = state.currentStatus.timestamp;
    statusData.reason = Messaging::Config::REASON_UPDATE_RESPONSE;
    statusData.originatingDeviceId = Messaging::Config::DEVICE_ID;

    String statusJson = Messaging::Json::createStatusResponse(statusData);
    bool published = Messaging::MessageAPI::publish(Messaging::Config::TOPIC_AUDIO_STATUS_RESPONSE, statusJson);

    if (published) {
        ESP_LOGI(TAG, "Published status update with %d sessions", state.currentStatus.getDeviceCount());
    } else {
        ESP_LOGE(TAG, "Failed to publish status update");
    }
}

void AudioManager::publishStatusRequest(bool delayed) {
    if (!delayed && !Messaging::MessageAPI::isHealthy()) {
        ESP_LOGW(TAG, "Cannot publish status request: Messaging system not healthy");
        return;
    }

    // For now, treat delayed requests the same as immediate requests
    // The MessageAPI handles connection management automatically
    bool published = Messaging::MessageAPI::requestAudioStatus();

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

// === SMART BEHAVIOR ===

void AudioManager::performSmartAutoSelection() {
    if (!initialized) {
        ESP_LOGW(TAG, "AudioManager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Performing smart auto-selection for tab: %s", getTabName(state.currentTab));

    // Always try auto-selection when explicitly requested
    autoSelectDeviceIfNeeded();

    // For Single tab: if we have devices but no selection, pick the best one
    if (state.currentTab == Events::UI::TabState::SINGLE && !state.selectedSingleDevice && state.hasDevices()) {
        String deviceToSelect = findBestDeviceToSelect();
        if (!deviceToSelect.isEmpty()) {
            ESP_LOGI(TAG, "Smart auto-selection: choosing %s for Single tab", deviceToSelect.c_str());
            selectDevice(deviceToSelect);
        }
    }

    // For Balance tab: ensure both devices are selected if possible
    if (state.currentTab == Events::UI::TabState::BALANCE && state.hasDevices()) {
        bool needsNotification = false;

        if (!state.selectedDevice1) {
            String deviceToSelect = findBestDeviceToSelect();
            if (!deviceToSelect.isEmpty()) {
                state.selectedDevice1 = state.findDevice(deviceToSelect);
                if (state.selectedDevice1) {
                    ESP_LOGI(TAG, "Smart auto-selection: choosing %s for Balance device1", deviceToSelect.c_str());
                    needsNotification = true;
                }
            }
        }

        if (!state.selectedDevice2) {
            String deviceToSelect = findBestDeviceToSelect();
            // Try to select a different device if possible
            if (!deviceToSelect.isEmpty()) {
                if (state.selectedDevice1 && deviceToSelect != state.selectedDevice1->processName) {
                    // Found a different device
                    state.selectedDevice2 = state.findDevice(deviceToSelect);
                    if (state.selectedDevice2) {
                        ESP_LOGI(TAG, "Smart auto-selection: choosing %s for Balance device2", deviceToSelect.c_str());
                        needsNotification = true;
                    }
                } else {
                    // Use the same device for both (better than no selection)
                    state.selectedDevice2 = state.selectedDevice1;
                    if (state.selectedDevice2) {
                        ESP_LOGI(TAG, "Smart auto-selection: using same device %s for both Balance devices", deviceToSelect.c_str());
                        needsNotification = true;
                    }
                }
            }
        }

        if (needsNotification) {
            String device1Name = state.selectedDevice1 ? state.selectedDevice1->processName : "";
            notifyStateChange(AudioStateChangeEvent::selectionChanged(device1Name));
        }
    }

    ESP_LOGI(TAG, "Smart auto-selection complete for %s tab", getTabName(state.currentTab));
}

// === PRIVATE METHODS ===

void AudioManager::notifyStateChange(const AudioStateChangeEvent& event) {
    for (auto& callback : callbacks) {
        callback(event);
    }
}

void AudioManager::autoSelectDeviceIfNeeded() {
    ESP_LOGD(TAG, "Checking if auto-selection is needed");

    // Check if we need to auto-select for Single tab
    if (state.currentTab == Events::UI::TabState::SINGLE && !state.selectedSingleDevice) {
        String deviceToSelect = findBestDeviceToSelect();
        if (!deviceToSelect.isEmpty()) {
            state.selectedSingleDevice = state.findDevice(deviceToSelect);
            if (state.selectedSingleDevice) {
                ESP_LOGI(TAG, "Auto-selected single device: %s", deviceToSelect.c_str());
                notifyStateChange(AudioStateChangeEvent::selectionChanged(deviceToSelect));
            }
        }
    }

    // Check if we need to auto-select for Balance tab
    if (state.currentTab == Events::UI::TabState::BALANCE) {
        bool needsSelection = false;

        if (!state.selectedDevice1) {
            String deviceToSelect = findBestDeviceToSelect();
            if (!deviceToSelect.isEmpty()) {
                state.selectedDevice1 = state.findDevice(deviceToSelect);
                if (state.selectedDevice1) {
                    ESP_LOGI(TAG, "Auto-selected balance device1: %s", deviceToSelect.c_str());
                    needsSelection = true;
                }
            }
        }

        if (!state.selectedDevice2) {
            String deviceToSelect = findBestDeviceToSelect();
            // Try to select a different device than device1
            if (!deviceToSelect.isEmpty() && state.selectedDevice1 && deviceToSelect != state.selectedDevice1->processName) {
                state.selectedDevice2 = state.findDevice(deviceToSelect);
                if (state.selectedDevice2) {
                    ESP_LOGI(TAG, "Auto-selected balance device2: %s", deviceToSelect.c_str());
                    needsSelection = true;
                }
            } else if (!deviceToSelect.isEmpty()) {
                // If only one device available, use it for both
                state.selectedDevice2 = state.findDevice(deviceToSelect);
                if (state.selectedDevice2) {
                    ESP_LOGI(TAG, "Auto-selected balance device2 (same as device1): %s", deviceToSelect.c_str());
                    needsSelection = true;
                }
            }
        }

        if (needsSelection) {
            String device1Name = state.selectedDevice1 ? state.selectedDevice1->processName : "";
            notifyStateChange(AudioStateChangeEvent::selectionChanged(device1Name));
        }
    }
}

void AudioManager::markDevicesAsStale() {
    for (auto& pair : state.currentStatus) {
        if (!pair.second.stale) {
            ESP_LOGI(TAG, "Marking device as stale: %s", pair.second.processName.c_str());
        }
        pair.second.stale = true;
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
        state.currentStatus.addOrUpdateDevice(newDevice);

        // Refresh pointers if the new device should be selected
        refreshDevicePointersIfNeeded(deviceData.processName);
    }
}

void AudioManager::refreshDevicePointersIfNeeded(const String& deviceName) {
    // Check if any of our selection pointers need to point to this new device
    // This handles the case where a device is added and should be selected

    bool needsRefresh = false;

    // This is mainly for cases where we're expecting a device but it wasn't in the hash map yet
    if (!state.selectedSingleDevice && state.currentTab == Events::UI::TabState::SINGLE) {
        // Could auto-select this new device
        autoSelectDeviceIfNeeded();
    }

    if ((!state.selectedDevice1 || !state.selectedDevice2) && state.currentTab == Events::UI::TabState::BALANCE) {
        // Could auto-select this new device for balance
        autoSelectDeviceIfNeeded();
    }
}

String AudioManager::findBestDeviceToSelect() const {
    if (state.currentStatus.isEmpty()) {
        return "";
    }

    // Look for a non-stale device first
    for (const auto& pair : state.currentStatus) {
        if (!pair.second.stale) {
            return pair.second.processName;
        }
    }

    // If all devices are stale, just pick the first one
    return state.currentStatus.begin()->second.processName;
}

void AudioManager::updateTimestamp() {
    state.lastUpdateTime = millis();
}

void AudioManager::ensureValidSelections() {
    // Use the AudioAppState validation method
    state.validateDeviceSelections();

    // If selections are now null, auto-select new devices
    if (!state.selectedSingleDevice) {
        String deviceName = findBestDeviceToSelect();
        if (!deviceName.isEmpty()) {
            state.selectedSingleDevice = state.findDevice(deviceName);
        }
    }

    if (!state.selectedDevice1) {
        String deviceName = findBestDeviceToSelect();
        if (!deviceName.isEmpty()) {
            state.selectedDevice1 = state.findDevice(deviceName);
        }
    }

    if (!state.selectedDevice2) {
        String deviceName = findBestDeviceToSelect();
        if (!deviceName.isEmpty()) {
            state.selectedDevice2 = state.findDevice(deviceName);
        }
    }
}

void AudioManager::refreshDevicePointers(const String& primaryDeviceName, const String& singleDeviceName, const String& device1Name, const String& device2Name) {
    ESP_LOGI(TAG, "Refreshing device pointers after hash map update");

    // Refresh primary device pointer
    if (!primaryDeviceName.isEmpty()) {
        state.primaryAudioDevice = state.findDevice(primaryDeviceName);
        if (state.primaryAudioDevice) {
            ESP_LOGD(TAG, "Refreshed primary device pointer: %s", primaryDeviceName.c_str());
        } else {
            ESP_LOGW(TAG, "Failed to refresh primary device pointer: %s (device not found)", primaryDeviceName.c_str());
        }
    } else {
        state.primaryAudioDevice = nullptr;
    }

    // Refresh single device pointer
    if (!singleDeviceName.isEmpty()) {
        state.selectedSingleDevice = state.findDevice(singleDeviceName);
        if (state.selectedSingleDevice) {
            ESP_LOGD(TAG, "Refreshed single device pointer: %s", singleDeviceName.c_str());
        } else {
            ESP_LOGW(TAG, "Failed to refresh single device pointer: %s (device not found)", singleDeviceName.c_str());
        }
    } else {
        state.selectedSingleDevice = nullptr;
    }

    // Refresh device1 pointer (balance tab)
    if (!device1Name.isEmpty()) {
        state.selectedDevice1 = state.findDevice(device1Name);
        if (state.selectedDevice1) {
            ESP_LOGD(TAG, "Refreshed device1 pointer: %s", device1Name.c_str());
        } else {
            ESP_LOGW(TAG, "Failed to refresh device1 pointer: %s (device not found)", device1Name.c_str());
        }
    } else {
        state.selectedDevice1 = nullptr;
    }

    // Refresh device2 pointer (balance tab)
    if (!device2Name.isEmpty()) {
        state.selectedDevice2 = state.findDevice(device2Name);
        if (state.selectedDevice2) {
            ESP_LOGD(TAG, "Refreshed device2 pointer: %s", device2Name.c_str());
        } else {
            ESP_LOGW(TAG, "Failed to refresh device2 pointer: %s (device not found)", device2Name.c_str());
        }
    } else {
        state.selectedDevice2 = nullptr;
    }
}

}  // namespace Audio
}  // namespace Application
