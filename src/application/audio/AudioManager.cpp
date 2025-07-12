#include "AudioManager.h"
#include "../../hardware/DeviceManager.h"
#include "../../logo/LogoManager.h"
#include "../../logo/BrutalLogoManager.h"
#include "../../messaging/Message.h"
#include "ManagerMacros.h"
#include "ui/ui.h"
#include <algorithm>
#include <esp_log.h>

static const char *TAG = "AudioManager";

namespace Application {
namespace Audio {

AudioManager &AudioManager::getInstance() {
  static AudioManager instance;
  return instance;
}

bool AudioManager::init() {
  INIT_GUARD("AudioManager", initialized, TAG);

  ESP_LOGI(TAG, "Initializing AudioManager");

  // Clear state
  state.clear();
  callbacks.clear();

  // Subscribe to audio status updates using BRUTAL messaging
  Messaging::subscribe(
      Messaging::Message::TYPE_AUDIO_STATUS,
      [this](const Messaging::Message &msg) {
        ESP_LOGI(TAG, "Received audio status update");

        // Access the new audio data structure
        const auto &audio = msg.data.audio;

        ESP_LOGI(TAG, "Origin: %s, Sessions: %d, Reason: %s",
                 msg.deviceId.c_str(), audio.sessionCount, audio.reason);

        // Create AudioStatus from message data
        AudioStatus status;
        status.timestamp = msg.timestamp;

        // Process sessions array
        for (int i = 0; i < audio.sessionCount && i < 16; i++) {
          const auto &session = audio.sessions[i];

          AudioLevel level;
          level.processName = session.processName;
          level.friendlyName = strlen(session.displayName) > 0
                                   ? session.displayName
                                   : session.processName;
          level.volume = static_cast<int>(session.volume *
                                          100); // Convert from 0-1 to 0-100
          level.isMuted = session.isMuted;
          level.state = session.state;
          level.lastUpdate = msg.timestamp;

          status.addOrUpdateDevice(level);

          ESP_LOGD(TAG, "Added session: %s (ID: %d), volume: %d, muted: %s",
                   level.processName.c_str(), session.processId, level.volume,
                   level.isMuted ? "yes" : "no");
        }

        // Set default device info
        if (audio.hasDefaultDevice) {
          status.defaultDevice.friendlyName = audio.defaultDevice.friendlyName;
          status.defaultDevice.volume = static_cast<int>(
              audio.defaultDevice.volume * 100); // Convert from 0-1 to 0-100
          status.defaultDevice.isMuted = audio.defaultDevice.isMuted;
          status.defaultDevice.state = String(audio.defaultDevice.dataFlow) +
                                       "/" +
                                       String(audio.defaultDevice.deviceRole);
          status.defaultDevice.lastUpdate = msg.timestamp;
          status.hasDefaultDevice = true;

          ESP_LOGI(TAG, "Default device: %s, volume: %d, muted: %s",
                   status.defaultDevice.friendlyName.c_str(),
                   status.defaultDevice.volume,
                   status.defaultDevice.isMuted ? "yes" : "no");
        }

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

AudioLevel *AudioManager::getDevice(const String &processName) {
  return state.findDevice(processName);
}

const AudioLevel *AudioManager::getDevice(const String &processName) const {
  return state.findDevice(processName);
}

// === EXTERNAL DATA INPUT ===

void AudioManager::onAudioStatusReceived(const AudioStatus &newStatus) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  ESP_LOGI(
      TAG,
      "Received audio status with %d devices - triggering reactive updates",
      newStatus.getDeviceCount());

  // Store current device selections by name (before hash map gets replaced)
  String currentPrimaryDeviceName =
      state.primaryAudioDevice ? state.primaryAudioDevice->processName : "";
  String currentSingleDeviceName =
      state.selectedSingleDevice ? state.selectedSingleDevice->processName : "";
  String currentDevice1Name =
      state.selectedDevice1 ? state.selectedDevice1->processName : "";
  String currentDevice2Name =
      state.selectedDevice2 ? state.selectedDevice2->processName : "";

  // Check if this is significantly new data (device count changed)
  bool significantUpdate =
      (newStatus.getDeviceCount() != state.currentStatus.getDeviceCount());

  // Update our internal status (this replaces the hash map, invalidating
  // pointers)
  state.currentStatus = newStatus;
  state.currentStatus.timestamp = Hardware::Device::getMillis();

  // Refresh device pointers to point to new hash map entries
  refreshDevicePointers(currentPrimaryDeviceName, currentSingleDeviceName,
                        currentDevice1Name, currentDevice2Name);

  // Perform smart auto-selection but only if we don't have valid selections
  if (!state.hasValidSelection() || significantUpdate) {
    performSmartAutoSelection();
  }

  // If this was a significant update (new devices appeared/disappeared),
  // be extra careful about ensuring good selections
  if (significantUpdate) {
    ESP_LOGI(TAG,
             "Significant device update detected - ensuring valid selections");

    // Ensure we have valid selections for current tab context
    ensureValidSelections();

    // If we're in a tab that needs selections but still don't have them,
    // this might indicate all devices disappeared - handle gracefully
    if ((state.isInSingleTab() && !state.selectedSingleDevice) ||
        (state.isInBalanceTab() &&
         (!state.selectedDevice1 || !state.selectedDevice2))) {
      ESP_LOGW(TAG, "No suitable devices available for current tab: %s",
               getTabName(state.currentTab));
    }
  }

  // Update timestamp
  updateTimestamp();

  // Notify listeners
  notifyStateChange(AudioStateChangeEvent::devicesUpdated());

  ESP_LOGI(TAG, "Reactive audio status processing complete");
}

// === USER ACTIONS ===

void AudioManager::selectDevice(const String &deviceName) {
  AudioLevel *device = state.findDevice(deviceName);
  if (device) {
    selectDevice(device);
  } else {
    ESP_LOGW(TAG, "Device not found: %s", deviceName.c_str());
  }
}

void AudioManager::selectDevice(AudioLevel *device) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);
  VALIDATE_PARAM_VOID(device, TAG, "device");

  AudioLevel *oldSelection = state.getCurrentSelectedDevice();

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

  ESP_LOGI(TAG, "Selected device: %s in tab: %d", device->processName.c_str(),
           (int)state.currentTab);

  // Notify listeners if selection actually changed
  NOTIFY_STATE_CHANGE_IF_DIFFERENT(oldSelection, device, selectionChanged);
}

void AudioManager::selectBalanceDevices(const String &device1Name,
                                        const String &device2Name) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  if (!state.isInBalanceTab()) {
    ESP_LOGW(TAG, "Can only select balance devices in balance tab");
    return;
  }

  UPDATE_BALANCE_SELECTION(device1Name, device2Name);
  NOTIFY_STATE_CHANGE_IF_DIFFERENT(nullptr, state.selectedDevice1,
                                   selectionChanged);
}

// === NEW BALANCE VOLUME METHODS ===

void AudioManager::setBalanceVolume(int volume, float balance_ratio) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);
  VALIDATE_BALANCE_DEVICES_VOID(state.selectedDevice1, state.selectedDevice2);

  BALANCE_VOLUME_DISTRIBUTE(volume, state.selectedDevice1,
                            state.selectedDevice2, balance_ratio);

  // Update timestamps and notify
  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::volumeChanged("balance", volume));
  publishStatusUpdate();
}

void AudioManager::setBalanceDeviceVolumes(int device1Volume,
                                           int device2Volume) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);
  VALIDATE_BALANCE_DEVICES_VOID(state.selectedDevice1, state.selectedDevice2);

  state.selectedDevice1->volume = constrain(device1Volume, 0, 100);
  state.selectedDevice2->volume = constrain(device2Volume, 0, 100);

  ESP_LOGI(TAG, "Set balance device volumes: %s=%d, %s=%d",
           state.selectedDevice1->processName.c_str(),
           state.selectedDevice1->volume,
           state.selectedDevice2->processName.c_str(),
           state.selectedDevice2->volume);

  updateTimestamp();
  notifyStateChange(
      AudioStateChangeEvent::volumeChanged("balance", device1Volume));
  publishStatusUpdate();
}

void AudioManager::muteBalanceDevices() {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);
  VALIDATE_BALANCE_DEVICES_VOID(state.selectedDevice1, state.selectedDevice2);

  state.selectedDevice1->isMuted = true;
  state.selectedDevice2->isMuted = true;

  ESP_LOGI(TAG, "Muted balance devices: %s, %s",
           state.selectedDevice1->processName.c_str(),
           state.selectedDevice2->processName.c_str());

  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::muteChanged("balance"));
  publishStatusUpdate();
}

void AudioManager::unmuteBalanceDevices() {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);
  VALIDATE_BALANCE_DEVICES_VOID(state.selectedDevice1, state.selectedDevice2);

  state.selectedDevice1->isMuted = false;
  state.selectedDevice2->isMuted = false;

  ESP_LOGI(TAG, "Unmuted balance devices: %s, %s",
           state.selectedDevice1->processName.c_str(),
           state.selectedDevice2->processName.c_str());

  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::muteChanged("balance"));
  publishStatusUpdate();
}

void AudioManager::setVolumeForCurrentDevice(int volume) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  if (state.isInMasterTab()) {
    // Master tab controls the default device directly
    ESP_LOGI(TAG, "Master tab: Setting default device volume to %d", volume);
    if (state.currentStatus.hasDefaultDevice) {
      setDeviceVolume("", volume); // Empty string triggers default device logic
    } else {
      ESP_LOGW(TAG, "No default device available for master volume control");
    }
  } else if (state.isInSingleTab()) {
    AudioLevel *currentDevice = state.getCurrentSelectedDevice();
    VALIDATE_DEVICE_SELECTION_VOID(currentDevice, "Single");

    ESP_LOGI(TAG, "%s tab: Setting session device '%s' volume to %d",
             getTabName(state.currentTab), currentDevice->processName.c_str(),
             volume);
    setDeviceVolume(currentDevice->processName, volume);
  } else if (state.isInBalanceTab()) {
    // FIXED: Proper balance volume control
    setBalanceVolume(volume, 0.0f); // Even balance by default
  }
}

void AudioManager::setVolumeLocalOnly(int volume) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  // Clamp volume
  volume = constrain(volume, 0, 100);

  ESP_LOGI(TAG, "Setting volume locally only (no messaging): %d (Core %d)",
           volume, xPortGetCoreID());

  if (state.isInMasterTab()) {
    // Update default device volume locally
    if (state.currentStatus.hasDefaultDevice) {
      state.currentStatus.defaultDevice.volume = volume;
      ESP_LOGI(TAG, "Updated default device volume locally to %d", volume);
    } else {
      ESP_LOGW(TAG, "No default device available for local volume control");
      return;
    }
  } else if (state.isInSingleTab()) {
    AudioLevel *currentDevice = state.getCurrentSelectedDevice();
    if (currentDevice) {
      currentDevice->volume = volume;
      currentDevice->lastUpdate = Hardware::Device::getMillis();
      currentDevice->stale = false;
      ESP_LOGI(TAG, "Updated session device %s volume locally to %d",
               currentDevice->processName.c_str(), volume);
    } else {
      ESP_LOGW(TAG, "No device selected for local volume control");
      return;
    }
  } else if (state.isInBalanceTab()) {
    // Update both balance devices locally
    if (state.selectedDevice1) {
      state.selectedDevice1->volume = volume;
      state.selectedDevice1->lastUpdate = Hardware::Device::getMillis();
    }
    if (state.selectedDevice2) {
      state.selectedDevice2->volume = volume;
      state.selectedDevice2->lastUpdate = Hardware::Device::getMillis();
    }
    ESP_LOGI(TAG, "Updated balance devices volume locally to %d", volume);
  }

  // Update timestamp and notify UI (but don't publish to Core 1)
  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::volumeChanged("local", volume));

  ESP_LOGI(TAG, "Local volume update complete - no Core 1 messaging triggered");
}

void AudioManager::setDeviceVolume(const String &deviceName, int volume) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  // Clamp volume
  volume = constrain(volume, 0, 100);

  EXECUTE_DEVICE_OPERATION(
      deviceName,
      // Non-empty deviceName = update specific session device
      {
        AudioLevel *device = getDevice(deviceName);
        if (device) {
          device->volume = volume;
          device->lastUpdate = Hardware::Device::getMillis();
          device->stale = false;
          ESP_LOGI(TAG, "Updated session device volume: %s = %d",
                   deviceName.c_str(), volume);
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
          ESP_LOGI(TAG, "Added new session device: %s = %d", deviceName.c_str(),
                   volume);
        }
      },
      // Empty deviceName = update default device
      {
        if (state.currentStatus.hasDefaultDevice) {
          state.currentStatus.defaultDevice.volume = volume;
          ESP_LOGI(TAG, "Set default device volume to %d", volume);
        } else {
          ESP_LOGW(TAG, "No default device available for volume control");
          return;
        }
      });

  // Update timestamp and notify
  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::volumeChanged(deviceName, volume));

  // Publish update
  publishStatusUpdate();
}

void AudioManager::muteCurrentDevice() {
  AudioLevel *currentDevice = state.getCurrentSelectedDevice();
  if (currentDevice) {
    muteDevice(currentDevice->processName);
  } else if (state.isInBalanceTab()) {
    muteBalanceDevices();
  } else {
    ESP_LOGW(TAG, "No device selected for mute control");
  }
}

void AudioManager::unmuteCurrentDevice() {
  AudioLevel *currentDevice = state.getCurrentSelectedDevice();
  if (currentDevice) {
    unmuteDevice(currentDevice->processName);
  } else if (state.isInBalanceTab()) {
    unmuteBalanceDevices();
  } else {
    ESP_LOGW(TAG, "No device selected for unmute control");
  }
}

void AudioManager::muteDevice(const String &deviceName) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  EXECUTE_DEVICE_OPERATION(
      deviceName,
      // Non-empty deviceName = mute specific session device
      {
        AudioLevel *device = getDevice(deviceName);
        if (device) {
          device->isMuted = true;
          ESP_LOGI(TAG, "Muted session device: %s", deviceName.c_str());
        } else {
          ESP_LOGW(TAG, "Session device not found for mute: %s",
                   deviceName.c_str());
          return;
        }
      },
      // Empty deviceName = mute default device
      {
        if (state.currentStatus.hasDefaultDevice) {
          state.currentStatus.defaultDevice.isMuted = true;
          ESP_LOGI(TAG, "Muted default device");
        } else {
          ESP_LOGW(TAG, "No default device available for mute control");
          return;
        }
      });

  // Update timestamp and notify
  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::muteChanged(deviceName));
  publishStatusUpdate();
}

void AudioManager::unmuteDevice(const String &deviceName) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  EXECUTE_DEVICE_OPERATION(
      deviceName,
      // Non-empty deviceName = unmute specific session device
      {
        AudioLevel *device = getDevice(deviceName);
        if (device) {
          device->isMuted = false;
          ESP_LOGI(TAG, "Unmuted session device: %s", deviceName.c_str());
        } else {
          ESP_LOGW(TAG, "Session device not found for unmute: %s",
                   deviceName.c_str());
          return;
        }
      },
      // Empty deviceName = unmute default device
      {
        if (state.currentStatus.hasDefaultDevice) {
          state.currentStatus.defaultDevice.isMuted = false;
          ESP_LOGI(TAG, "Unmuted default device");
        } else {
          ESP_LOGW(TAG, "No default device available for unmute control");
          return;
        }
      });

  // Update timestamp and notify
  updateTimestamp();
  notifyStateChange(AudioStateChangeEvent::muteChanged(deviceName));
  publishStatusUpdate();
}

void AudioManager::setCurrentTab(Events::UI::TabState tab) {
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

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
  // Create audio status message using NEW format
  Messaging::Message::AudioData audio;

  // Initialize structure
  audio.sessionCount = 0;
  audio.hasDefaultDevice = false;
  audio.activeSessionCount = state.currentStatus.getDeviceCount();

  // Populate sessions array from our current devices
  int sessionIndex = 0;
  for (const auto &devicePair : state.currentStatus.audioDevices) {
    if (sessionIndex >= 16)
      break; // Max sessions limit

    const auto &device = devicePair.second;
    auto &session = audio.sessions[sessionIndex];

    session.processId = 0; // We don't track process IDs locally
    strncpy(session.processName, device.processName.c_str(),
            sizeof(session.processName) - 1);
    strncpy(session.displayName, device.friendlyName.c_str(),
            sizeof(session.displayName) - 1);
    session.volume = device.volume / 100.0f; // Convert from 0-100 to 0-1
    session.isMuted = device.isMuted;
    strncpy(session.state, device.state.c_str(), sizeof(session.state) - 1);

    sessionIndex++;
  }
  audio.sessionCount = sessionIndex;

  // Set default device info
  if (state.currentStatus.hasDefaultDevice) {
    audio.hasDefaultDevice = true;
    strncpy(audio.defaultDevice.friendlyName,
            state.currentStatus.defaultDevice.friendlyName.c_str(),
            sizeof(audio.defaultDevice.friendlyName) - 1);
    audio.defaultDevice.volume = state.currentStatus.defaultDevice.volume /
                                 100.0f; // Convert from 0-100 to 0-1
    audio.defaultDevice.isMuted = state.currentStatus.defaultDevice.isMuted;

    // Parse state back to dataFlow/deviceRole if possible
    String deviceState = state.currentStatus.defaultDevice.state;
    int slashPos = deviceState.indexOf('/');
    if (slashPos >= 0) {
      String dataFlow = deviceState.substring(0, slashPos);
      String deviceRole = deviceState.substring(slashPos + 1);
      strncpy(audio.defaultDevice.dataFlow, dataFlow.c_str(),
              sizeof(audio.defaultDevice.dataFlow) - 1);
      strncpy(audio.defaultDevice.deviceRole, deviceRole.c_str(),
              sizeof(audio.defaultDevice.deviceRole) - 1);
    } else {
      strncpy(audio.defaultDevice.dataFlow, "Render",
              sizeof(audio.defaultDevice.dataFlow) - 1);
      strncpy(audio.defaultDevice.deviceRole, "Console",
              sizeof(audio.defaultDevice.deviceRole) - 1);
    }
  }

  // Set additional fields
  strncpy(audio.reason, "UpdateResponse", sizeof(audio.reason) - 1);
  audio.originatingRequestId[0] = '\0'; // Empty
  audio.originatingDeviceId[0] = '\0';  // Empty

  // Create and send the message
  auto msg = Messaging::Message::createAudioStatus(audio, "");
  Messaging::sendMessage(msg);

  ESP_LOGI(TAG,
           "Published audio status update - %d sessions, default device: %s",
           audio.sessionCount, audio.hasDefaultDevice ? "yes" : "no");
}

void AudioManager::publishStatusRequest(bool delayed) {
  // Create and send status request - BRUTAL!
  auto msg = Messaging::Message::createStatusRequest("");
  Messaging::sendMessage(msg);

  ESP_LOGI(TAG, "Published %sstatus request", delayed ? "delayed " : "");
}

// === UTILITY ===

const char *AudioManager::getTabName(Events::UI::TabState tab) const {
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
  REQUIRE_INIT_VOID("AudioManager", initialized, TAG);

  ESP_LOGI(TAG, "Performing smart auto-selection for tab: %s",
           getTabName(state.currentTab));

  // Always try auto-selection when explicitly requested
  autoSelectDeviceIfNeeded();

  // For Single tab: if we have devices but no selection, pick the best one
  if (state.currentTab == Events::UI::TabState::SINGLE &&
      !state.selectedSingleDevice && state.hasDevices()) {
    String deviceToSelect = findBestDeviceToSelect();
    if (!deviceToSelect.isEmpty()) {
      ESP_LOGI(TAG, "Smart auto-selection: choosing %s for Single tab",
               deviceToSelect.c_str());
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
          ESP_LOGI(TAG, "Smart auto-selection: choosing %s for Balance device1",
                   deviceToSelect.c_str());
          needsNotification = true;
        }
      }
    }

    if (!state.selectedDevice2) {
      String deviceToSelect = findBestDeviceToSelect();
      // Try to select a different device if possible
      if (!deviceToSelect.isEmpty()) {
        if (state.selectedDevice1 &&
            deviceToSelect != state.selectedDevice1->processName) {
          // Found a different device
          state.selectedDevice2 = state.findDevice(deviceToSelect);
          if (state.selectedDevice2) {
            ESP_LOGI(TAG,
                     "Smart auto-selection: choosing %s for Balance device2",
                     deviceToSelect.c_str());
            needsNotification = true;
          }
        } else {
          // Use the same device for both (better than no selection)
          state.selectedDevice2 = state.selectedDevice1;
          if (state.selectedDevice2) {
            ESP_LOGI(TAG,
                     "Smart auto-selection: using same device %s for both "
                     "Balance devices",
                     deviceToSelect.c_str());
            needsNotification = true;
          }
        }
      }
    }

    if (needsNotification) {
      String device1Name =
          state.selectedDevice1 ? state.selectedDevice1->processName : "";
      notifyStateChange(AudioStateChangeEvent::selectionChanged(device1Name));
    }
  }

  ESP_LOGI(TAG, "Smart auto-selection complete for %s tab",
           getTabName(state.currentTab));
}

// === PRIVATE METHODS ===

void AudioManager::notifyStateChange(const AudioStateChangeEvent &event) {
  for (auto &callback : callbacks) {
    callback(event);
  }
}

void AudioManager::autoSelectDeviceIfNeeded() {
  ESP_LOGD(TAG, "Checking if auto-selection is needed");

  // Check if we need to auto-select for Single tab
  if (state.currentTab == Events::UI::TabState::SINGLE &&
      !state.selectedSingleDevice) {
    String deviceToSelect = findBestDeviceToSelect();
    if (!deviceToSelect.isEmpty()) {
      state.selectedSingleDevice = state.findDevice(deviceToSelect);
      if (state.selectedSingleDevice) {
        ESP_LOGI(TAG, "Auto-selected single device: %s",
                 deviceToSelect.c_str());
        notifyStateChange(
            AudioStateChangeEvent::selectionChanged(deviceToSelect));
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
          ESP_LOGI(TAG, "Auto-selected balance device1: %s",
                   deviceToSelect.c_str());
          needsSelection = true;
        }
      }
    }

    if (!state.selectedDevice2) {
      String deviceToSelect = findBestDeviceToSelect();
      // Try to select a different device than device1
      if (!deviceToSelect.isEmpty() && state.selectedDevice1 &&
          deviceToSelect != state.selectedDevice1->processName) {
        state.selectedDevice2 = state.findDevice(deviceToSelect);
        if (state.selectedDevice2) {
          ESP_LOGI(TAG, "Auto-selected balance device2: %s",
                   deviceToSelect.c_str());
          needsSelection = true;
        }
      } else if (!deviceToSelect.isEmpty()) {
        // If only one device available, use it for both
        state.selectedDevice2 = state.findDevice(deviceToSelect);
        if (state.selectedDevice2) {
          ESP_LOGI(TAG, "Auto-selected balance device2 (same as device1): %s",
                   deviceToSelect.c_str());
          needsSelection = true;
        }
      }
    }

    if (needsSelection) {
      String device1Name =
          state.selectedDevice1 ? state.selectedDevice1->processName : "";
      notifyStateChange(AudioStateChangeEvent::selectionChanged(device1Name));
    }
  }
}

void AudioManager::markDevicesAsStale() {
  for (auto &pair : state.currentStatus) {
    if (!pair.second.stale) {
      ESP_LOGI(TAG, "Marking device as stale: %s",
               pair.second.processName.c_str());
    }
    pair.second.stale = true;
  }
}

void AudioManager::updateDeviceFromStatus(const AudioLevel &deviceData) {
  AudioLevel *existing = getDevice(deviceData.processName);
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

void AudioManager::refreshDevicePointersIfNeeded(const String &deviceName) {
  // Check if any of our selection pointers need to point to this new device
  // This handles the case where a device is added and should be selected

  bool needsRefresh = false;

  // This is mainly for cases where we're expecting a device but it wasn't in
  // the hash map yet
  if (!state.selectedSingleDevice &&
      state.currentTab == Events::UI::TabState::SINGLE) {
    // Could auto-select this new device
    autoSelectDeviceIfNeeded();
  }

  if ((!state.selectedDevice1 || !state.selectedDevice2) &&
      state.currentTab == Events::UI::TabState::BALANCE) {
    // Could auto-select this new device for balance
    autoSelectDeviceIfNeeded();
  }
}

String AudioManager::findBestDeviceToSelect() const {
  if (state.currentStatus.isEmpty()) {
    return "";
  }

  // Look for a non-stale device first
  for (const auto &pair : state.currentStatus) {
    if (!pair.second.stale) {
      return pair.second.processName;
    }
  }

  // If all devices are stale, just pick the first one
  return state.currentStatus.begin()->second.processName;
}

void AudioManager::updateTimestamp() { state.lastUpdateTime = millis(); }

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

void AudioManager::refreshDevicePointers(const String &primaryDeviceName,
                                         const String &singleDeviceName,
                                         const String &device1Name,
                                         const String &device2Name) {
  ESP_LOGI(TAG, "Refreshing device pointers after hash map update");

  // Refresh primary device pointer
  if (!primaryDeviceName.isEmpty()) {
    state.primaryAudioDevice = state.findDevice(primaryDeviceName);
    LOG_DEBUG_IF(state.primaryAudioDevice, TAG,
                 "Refreshed primary device pointer: %s",
                 primaryDeviceName.c_str());
    LOG_WARN_IF(
        !state.primaryAudioDevice, TAG,
        "Failed to refresh primary device pointer: %s (device not found)",
        primaryDeviceName.c_str());
  } else {
    state.primaryAudioDevice = nullptr;
  }

  // Refresh single device pointer
  if (!singleDeviceName.isEmpty()) {
    state.selectedSingleDevice = state.findDevice(singleDeviceName);
    LOG_DEBUG_IF(state.selectedSingleDevice, TAG,
                 "Refreshed single device pointer: %s",
                 singleDeviceName.c_str());
    LOG_WARN_IF(
        !state.selectedSingleDevice, TAG,
        "Failed to refresh single device pointer: %s (device not found)",
        singleDeviceName.c_str());
  } else {
    state.selectedSingleDevice = nullptr;
  }

  // Refresh device1 pointer (balance tab)
  if (!device1Name.isEmpty()) {
    state.selectedDevice1 = state.findDevice(device1Name);
    LOG_DEBUG_IF(state.selectedDevice1, TAG, "Refreshed device1 pointer: %s",
                 device1Name.c_str());
    LOG_WARN_IF(!state.selectedDevice1, TAG,
                "Failed to refresh device1 pointer: %s (device not found)",
                device1Name.c_str());
  } else {
    state.selectedDevice1 = nullptr;
  }

  // Refresh device2 pointer (balance tab)
  if (!device2Name.isEmpty()) {
    state.selectedDevice2 = state.findDevice(device2Name);
    LOG_DEBUG_IF(state.selectedDevice2, TAG, "Refreshed device2 pointer: %s",
                 device2Name.c_str());
    LOG_WARN_IF(!state.selectedDevice2, TAG,
                "Failed to refresh device2 pointer: %s (device not found)",
                device2Name.c_str());
  } else {
    state.selectedDevice2 = nullptr;
  }
}

void AudioManager::checkSingleProcessLogo(const char *processName) {
  VALIDATE_PARAM_VOID(processName, TAG, "processName");

  if (strlen(processName) == 0) {
    return;
  }

  if (!Logo::LogoManager::getInstance().isInitialized()) {
    ESP_LOGD(TAG, "Logo system not initialized, skipping logo check for: %s",
             processName);
    return;
  }

  // Check if logo already exists locally
  if (Logo::LogoManager::getInstance().hasLogo(processName)) {
    ESP_LOGD(TAG, "Logo already exists for process: %s", processName);
    return;
  }

  // Request logo from server via MessageBusLogoSupplier
  auto &logoSupplier =
      Application::LogoAssets::MessageBusLogoSupplier::getInstance();
  if (!logoSupplier.isReady()) {
    ESP_LOGD(TAG, "Logo supplier not ready, cannot request logo for: %s",
             processName);
    return;
  }

  ESP_LOGI(TAG, "Requesting logo for process: %s", processName);

  // Request logo asynchronously
  bool requested = logoSupplier.requestLogo(
      processName,
      [processName](const Application::LogoAssets::AssetResponse &response) {
        LOG_INFO_IF(response.success, TAG,
                    "Successfully received logo for process: %s", processName);
        LOG_WARN_IF(!response.success, TAG,
                    "Failed to receive logo for process: %s - %s", processName,
                    response.errorMessage.c_str());
      });

  LOG_DEBUG_IF(requested, TAG, "Logo request submitted for process: %s",
               processName);
  LOG_WARN_IF(!requested, TAG, "Failed to submit logo request for process: %s",
              processName);
}

} // namespace Audio
} // namespace Application
