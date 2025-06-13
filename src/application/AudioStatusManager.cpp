#include "AudioStatusManager.h"
#include "../hardware/DeviceManager.h"
#include "DebugUtils.h"
// #include "UIConstants.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <memory>
#include <ui/ui.h>

static const char *TAG = "AudioStatusManager";

// Macros to reduce repetition in device actions and messaging publishing
#define AUDIO_DEVICE_ACTION_PROLOGUE(action_name)                              \
  String currentDevice = StatusManager::getSelectedDevice();                   \
  if (!initialized) {                                                          \
    ESP_LOGW(TAG, "AudioStatusManager not initialized");                       \
    return;                                                                    \
  }                                                                            \
  if (currentDevice.isEmpty()) {                                               \
    ESP_LOGW(TAG, "No device selected for " action_name " control");           \
    return;                                                                    \
  }

// Updated macro for new protocol format
#define AUDIO_COMMAND_BASE(command_type)                                       \
  if (!Messaging::MessageBus::IsConnected()) {                                 \
    ESP_LOGW(TAG, "Cannot publish command: No transport connected");           \
    return;                                                                    \
  }                                                                            \
  JsonDocument doc;                                                            \
  doc["commandType"] = command_type;                                           \
  doc["processId"] = getProcessIdForDevice(deviceName);                        \
  doc["processName"] = deviceName;                                             \
  doc["requestId"] = Messaging::Protocol::generateRequestId();

#define AUDIO_PUBLISH_COMMAND_FINISH(action_name)                              \
  String jsonPayload;                                                          \
  serializeJson(doc, jsonPayload);                                             \
  bool published =                                                             \
      Messaging::MessageBus::Publish("COMMAND", jsonPayload.c_str());          \
  if (published) {                                                             \
    ESP_LOGI(TAG, "Published " action_name " command for %s",                  \
             deviceName.c_str());                                              \
  } else {                                                                     \
    ESP_LOGE(TAG, "Failed to publish " action_name " command");                \
  }

namespace Application {
namespace Audio {

// StatusManager implementation
AudioStatus StatusManager::currentAudioStatus;
Messaging::Handler StatusManager::audioStatusHandler;
unsigned long StatusManager::lastUpdateTime = 0;
bool StatusManager::initialized = false;
bool StatusManager::suppressArcEvents = false;
bool StatusManager::suppressDropdownEvents = false;
Events::UI::TabState StatusManager::currentTab = Events::UI::TabState::MASTER;
std::unique_ptr<UI::Components::DeviceSelectorManager>
    StatusManager::deviceSelectorManager;

bool StatusManager::init() {
  if (initialized) {
    ESP_LOGW(TAG, "AudioStatusManager already initialized");
    return true;
  }

  ESP_LOGI(TAG, "Initializing AudioStatusManager");

  // Clear existing data
  currentAudioStatus.audioLevels.clear();
  currentAudioStatus.hasDefaultDevice = false;
  currentAudioStatus.timestamp = 0;
  lastUpdateTime = Hardware::Device::getMillis();

  // Initialize device selector manager
  deviceSelectorManager =
      std::make_unique<UI::Components::DeviceSelectorManager>();
  if (!deviceSelectorManager) {
    ESP_LOGE(TAG, "Failed to create device selector manager");
    return false;
  }

  // Setup callbacks for device selector manager
  deviceSelectorManager->setMainSelectionCallback(
      [](const UI::Components::DeviceSelection &selection) {
        // Handle main selection changes - update UI dropdowns for main/single
        // tabs
        ESP_LOGI(TAG, "Main selection changed to: %s",
                 selection.getValue().c_str());
        LOG_TO_UI(ui_txtAreaDebugLog,
                  String("DeviceSelector: Main selection changed to '") +
                      selection.getValue() + String("'"));

        // Update the main dropdown (used in MASTER and SINGLE tabs)
        if (ui_selectAudioDevice) {
          // Get the current dropdown text to find the index
          String currentOptions = lv_dropdown_get_options(ui_selectAudioDevice);

          // Find the index of the selected device by parsing options string
          int selectedIndex = 0;
          int currentIndex = 0;
          int startPos = 0;
          int pos = currentOptions.indexOf('\n');

          while (pos != -1) {
            String option = currentOptions.substring(startPos, pos);
            if (option == selection.getValue()) {
              selectedIndex = currentIndex;
              break;
            }
            currentIndex++;
            startPos = pos + 1;
            pos = currentOptions.indexOf('\n', startPos);
          }

          // Check the last option
          if (startPos < currentOptions.length()) {
            String option = currentOptions.substring(startPos);
            if (option == selection.getValue()) {
              selectedIndex = currentIndex;
            }
          }

          suppressDropdownEvents = true;
          lv_dropdown_set_selected(ui_selectAudioDevice, selectedIndex);
          suppressDropdownEvents = false;
        }
      });

  deviceSelectorManager->setBalanceSelectionCallback(
      [](const UI::Components::BalanceSelection &selection) {
        // Handle balance selection changes - update UI dropdowns for balance
        // tab
        ESP_LOGI(TAG, "Balance selection changed: %s, %s",
                 selection.device1.getValue().c_str(),
                 selection.device2.getValue().c_str());
        LOG_TO_UI(ui_txtAreaDebugLog,
                  String("DeviceSelector: Balance selection changed"));
        LOG_TO_UI(ui_txtAreaDebugLog, String("  Device 1: '") +
                                          selection.device1.getValue() +
                                          String("'"));
        LOG_TO_UI(ui_txtAreaDebugLog, String("  Device 2: '") +
                                          selection.device2.getValue() +
                                          String("'"));
        if (selection.hasConflict()) {
          LOG_TO_UI(ui_txtAreaDebugLog,
                    String("  WARNING: Balance selection has conflict!"));
        }

        // Helper lambda to update dropdown selection
        auto updateDropdownSelection = [](lv_obj_t *dropdown,
                                          const String &deviceName) {
          if (!dropdown)
            return;

          String currentOptions = lv_dropdown_get_options(dropdown);

          // Find the index of the selected device by parsing options string
          int selectedIndex = 0;
          int currentIndex = 0;
          int startPos = 0;
          int pos = currentOptions.indexOf('\n');

          while (pos != -1) {
            String option = currentOptions.substring(startPos, pos);
            if (option == deviceName) {
              selectedIndex = currentIndex;
              break;
            }
            currentIndex++;
            startPos = pos + 1;
            pos = currentOptions.indexOf('\n', startPos);
          }

          // Check the last option
          if (startPos < currentOptions.length()) {
            String option = currentOptions.substring(startPos);
            if (option == deviceName) {
              selectedIndex = currentIndex;
            }
          }

          lv_dropdown_set_selected(dropdown, selectedIndex);
        };

        // Update balance dropdowns
        suppressDropdownEvents = true;
        updateDropdownSelection(ui_selectAudioDevice1,
                                selection.device1.getValue());
        updateDropdownSelection(ui_selectAudioDevice2,
                                selection.device2.getValue());
        suppressDropdownEvents = false;
      });

  deviceSelectorManager->setDeviceListCallback(
      [](const std::vector<Application::Audio::AudioLevel> &devices) {
        // Handle device list changes - update all UI dropdowns with new device
        // list
        ESP_LOGI(TAG, "Device list updated with %d devices",
                 (int)devices.size());
        LOG_TO_UI(ui_txtAreaDebugLog,
                  String("DeviceSelector: Device list updated with ") +
                      String((int)devices.size()) + String(" devices"));

        // List each device in the array
        for (int i = 0; i < (int)devices.size(); i++) {
          const Application::Audio::AudioLevel &device = devices[i];
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

        // Build options string for dropdowns (LVGL format:
        // "Option1\nOption2\nOption3")
        String optionsString = "";
        if (devices.size() == 0) {
          optionsString = "-"; // Default option when no devices
        } else {
          for (int i = 0; i < (int)devices.size(); i++) {
            if (i > 0) {
              optionsString += "\n";
            }
            optionsString += devices[i].processName;
          }
        }

        // Update all dropdown widgets with new options
        suppressDropdownEvents = true;
        if (ui_selectAudioDevice) {
          lv_dropdown_set_options(ui_selectAudioDevice, optionsString.c_str());
        }
        if (ui_selectAudioDevice1) {
          lv_dropdown_set_options(ui_selectAudioDevice1, optionsString.c_str());
        }
        if (ui_selectAudioDevice2) {
          lv_dropdown_set_options(ui_selectAudioDevice2, optionsString.c_str());
        }

        // After updating options, restore the current selections
        if (deviceSelectorManager) {
          auto mainSelection = deviceSelectorManager->getMainSelection();
          if (mainSelection.isValid() && ui_selectAudioDevice) {
            // Find index of main selection and set it
            for (size_t i = 0; i < devices.size(); i++) {
              if (devices[i].processName == mainSelection.getValue()) {
                lv_dropdown_set_selected(ui_selectAudioDevice, i);
                break;
              }
            }
          }

          auto balanceSelection = deviceSelectorManager->getBalanceSelections();
          if (balanceSelection.device1.isValid() && ui_selectAudioDevice1) {
            for (size_t i = 0; i < devices.size(); i++) {
              if (devices[i].processName ==
                  balanceSelection.device1.getValue()) {
                lv_dropdown_set_selected(ui_selectAudioDevice1, i);
                break;
              }
            }
          }
          if (balanceSelection.device2.isValid() && ui_selectAudioDevice2) {
            for (size_t i = 0; i < devices.size(); i++) {
              if (devices[i].processName ==
                  balanceSelection.device2.getValue()) {
                lv_dropdown_set_selected(ui_selectAudioDevice2, i);
                break;
              }
            }
          }
        }
        suppressDropdownEvents = false;
      });

  // Initialize and register message handlers
  initializeMessageHandlers();
  if (!Messaging::MessageBus::RegisterHandler(audioStatusHandler)) {
    ESP_LOGE(TAG, "Failed to register audio status message handler");
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

  // Unregister message handlers
  Messaging::MessageBus::UnregisterHandler(audioStatusHandler.Identifier);

  // Clear data
  currentAudioStatus.audioLevels.clear();
  currentAudioStatus.hasDefaultDevice = false;
  currentAudioStatus.timestamp = 0;

  // Clear device selector manager
  if (deviceSelectorManager) {
    deviceSelectorManager.reset();
  }

  initialized = false;
}

void StatusManager::updateAudioLevel(const String &processName, int volume) {
  if (!initialized) {
    ESP_LOGW(TAG, "AudioStatusManager not initialized");
    return;
  }

  unsigned long now = Hardware::Device::getMillis();

  // Find existing process or create new entry
  for (auto &level : currentAudioStatus.audioLevels) {
    if (level.processName == processName) {
      level.volume = volume;
      level.lastUpdate = now;
      return;
    }
  }

  // Add new process
  AudioLevel newLevel;
  newLevel.processName = processName;
  newLevel.volume = volume;
  newLevel.lastUpdate = now;
  newLevel.stale = false;
  currentAudioStatus.audioLevels.push_back(newLevel);

  lastUpdateTime = now;

  ESP_LOGI(TAG, "Updated audio level - Process: %s, Volume: %d",
           processName.c_str(), volume);
}

int StatusManager::getProcessIdForDevice(const String &deviceName) {
  // In a real implementation, maintain a mapping of process names to IDs
  // For now, use a simple hash of the process name
  unsigned long hash = 0;
  for (int i = 0; i < deviceName.length(); i++) {
    hash = hash * 31 + deviceName.charAt(i);
  }
  return (int)(hash % 65536); // Keep it reasonable
}

void StatusManager::initializeBalanceDropdownSelections(void) {
  if (!deviceSelectorManager)
    return;

  // First update the available devices, then initialize balance selections
  deviceSelectorManager->updateAvailableDevices(currentAudioStatus.audioLevels);
  deviceSelectorManager->initializeBalanceSelections();
}

std::vector<AudioLevel> StatusManager::getAllAudioLevels(void) {
  return currentAudioStatus.audioLevels;
}

AudioLevel *StatusManager::getAudioLevel(const String &processName) {
  for (auto &level : currentAudioStatus.audioLevels) {
    if (level.processName == processName) {
      return &level;
    }
  }
  return nullptr;
}

AudioStatus StatusManager::getCurrentAudioStatus(void) {
  return currentAudioStatus;
}

void StatusManager::onAudioStatusReceived(const AudioStatus &status) {
  ESP_LOGI(
      TAG,
      "Received audio status update with %d processes and %s default device",
      status.audioLevels.size(), status.hasDefaultDevice ? "a" : "no");

  // Update the current audio status with the received data
  currentAudioStatus = status;
  currentAudioStatus.timestamp = Hardware::Device::getMillis();

  // First, mark all existing devices as stale
  for (auto &level : currentAudioStatus.audioLevels) {
    if (!level.stale) {
      ESP_LOGI(TAG, "Marking device as stale: %s", level.processName.c_str());
    }
    level.stale = true;
  }

  // Update levels from the received data and mark them as fresh
  for (const auto &level : status.audioLevels) {
    updateAudioLevel(level.processName, level.volume);
    // Find the updated device and mark it as fresh
    for (auto &existingLevel : currentAudioStatus.audioLevels) {
      if (existingLevel.processName == level.processName) {
        existingLevel.stale = false;
        break;
      }
    }
  }

  // Auto-select first non-stale device if none is selected and devices are
  // available
  if (deviceSelectorManager && !currentAudioStatus.audioLevels.empty()) {
    auto currentSelection = deviceSelectorManager->getMainSelection();

    // Only auto-select if no valid selection exists
    if (!currentSelection.isValid()) {
      // Find first non-stale device, or fall back to first device if all are
      // stale
      String deviceToSelect = "";

      // Look for a non-stale device first
      for (int i = 0; i < (int)currentAudioStatus.audioLevels.size(); i++) {
        const auto &level = currentAudioStatus.audioLevels[i];
        if (!level.stale) {
          deviceToSelect = level.processName;
          break;
        }
      }

      // If all devices are stale, just pick the first one
      if (deviceToSelect.isEmpty() &&
          currentAudioStatus.audioLevels.size() > 0) {
        deviceToSelect = currentAudioStatus.audioLevels[0].processName;
      }

      if (!deviceToSelect.isEmpty()) {
        ESP_LOGI(TAG, "Auto-selecting device: %s", deviceToSelect.c_str());
        deviceSelectorManager->setMainSelection(
            UI::Components::DeviceSelection{deviceToSelect});
      }
    } else {
      ESP_LOGI(TAG, "Keeping current selection: %s",
               currentSelection.getValue().c_str());
    }
  }

  // Initialize balance dropdown selections to ensure mutual exclusivity from
  // the start
  initializeBalanceDropdownSelections();

  // Update the UI
  onAudioLevelsChangedUI();
}

void StatusManager::onAudioLevelsChangedUI(void) {
  // Update available devices in the device selector manager
  if (deviceSelectorManager) {
    deviceSelectorManager->updateAvailableDevices(
        currentAudioStatus.audioLevels);
  }

  // Update default device label if available
  if (currentAudioStatus.hasDefaultDevice && ui_lblPrimaryAudioDeviceValue) {
    lv_label_set_text(ui_lblPrimaryAudioDeviceValue,
                      currentAudioStatus.defaultDevice.friendlyName.c_str());
  }

  // Update volume arc based on current tab - this ensures volume arcs update
  // when device data changes
  updateVolumeArcFromSelectedDevice();
  ESP_LOGI(TAG, "Updated UI after audio levels changed");
}

String StatusManager::getSelectedDevice(void) {
  if (!deviceSelectorManager)
    return "";

  // Convert tab state to index for DeviceSelectorManager
  int tabIndex = 0; // Default to MASTER
  switch (currentTab) {
  case Events::UI::TabState::MASTER:
    tabIndex = 0;
    break;
  case Events::UI::TabState::SINGLE:
    tabIndex = 1;
    break;
  case Events::UI::TabState::BALANCE:
    tabIndex = 2;
    break;
  }

  auto selection = deviceSelectorManager->getSelectionForTab(tabIndex);
  return selection.getValue();
}

void StatusManager::setDropdownSelection(lv_obj_t *dropdown,
                                         const String &deviceName) {
  if (!dropdown) {
    ESP_LOGW(TAG, "setDropdownSelection called with null dropdown");
    return;
  }

  ESP_LOGI(TAG, "Setting dropdown selection to: '%s'", deviceName.c_str());

  if (!deviceSelectorManager) {
    ESP_LOGW(TAG, "Device selector manager not available");
    return;
  }

  // Validate device name - don't set if empty or default placeholder
  if (deviceName.isEmpty() || deviceName == "-") {
    ESP_LOGW(TAG, "Ignoring invalid device name: '%s'", deviceName.c_str());
    return;
  }

  // Convert tab state to index for DeviceSelectorManager
  int tabIndex = 0; // Default to MASTER
  switch (currentTab) {
  case Events::UI::TabState::MASTER:
    tabIndex = 0;
    break;
  case Events::UI::TabState::SINGLE:
    tabIndex = 1;
    break;
  case Events::UI::TabState::BALANCE:
    tabIndex = 2;
    break;
  }

  // Create valid device selection
  UI::Components::DeviceSelection selection{deviceName};
  deviceSelectorManager->setSelectionForTab(tabIndex, selection);

  // Update UI to reflect the selection
  updateVolumeArcFromSelectedDevice();

  ESP_LOGI(TAG, "Successfully set device selection for tab %d to: '%s'",
           tabIndex, deviceName.c_str());
}

String StatusManager::getDropdownSelection(lv_obj_t *dropdown) {
  if (!dropdown) {
    return "";
  }

  // First try to get the text directly from the LVGL dropdown object
  const char *selectedText = lv_dropdown_get_text(dropdown);
  String directValue = String(selectedText ? selectedText : "");

  // If we got a valid value and it's not empty or the default "-", return it
  if (!directValue.isEmpty() && directValue != "-") {
    return directValue;
  }

  // Fallback to device selector manager if available
  if (!deviceSelectorManager) {
    return directValue; // Return the direct value even if empty
  }

  // Determine which dropdown this is and return the appropriate selection
  if (dropdown == ui_selectAudioDevice) {
    auto selection = deviceSelectorManager->getMainSelection();
    return selection.isValid() ? selection.getValue() : directValue;
  } else if (dropdown == ui_selectAudioDevice1) {
    auto selections = deviceSelectorManager->getBalanceSelections();
    return selections.device1.isValid() ? selections.device1.getValue()
                                        : directValue;
  } else if (dropdown == ui_selectAudioDevice2) {
    auto selections = deviceSelectorManager->getBalanceSelections();
    return selections.device2.isValid() ? selections.device2.getValue()
                                        : directValue;
  }

  return directValue;
}

void StatusManager::updateVolumeArcFromSelectedDevice(void) {
  lv_obj_t *slider;
  int resVolume = 0;
  bool volumeFound = false;

  switch (currentTab) {
  case Events::UI::TabState::MASTER: {
    slider = ui_primaryVolumeSlider;
    break;
  };
  case Events::UI::TabState::SINGLE: {
    slider = ui_singleVolumeSlider;
    break;
  };
  case Events::UI::TabState::BALANCE: {
    slider = ui_balanceVolumeSlider;
    break;
  };
  default:
    slider = nullptr;
  }

  if (!slider) {
    ESP_LOGW(TAG, "No volume slider found for current tab");
    return;
  }

  suppressArcEvents = true;

  // If in MASTER tab, show default device volume
  if (currentTab == Events::UI::TabState::MASTER) {
    if (currentAudioStatus.hasDefaultDevice) {
      resVolume = (int)(currentAudioStatus.defaultDevice.volume * 100.0f);
      volumeFound = true;
      ESP_LOGI(TAG, "Using default device volume: %d", resVolume);
    } else {
      ESP_LOGW(TAG, "No default device available for master volume");
    }
  } else if (currentTab == Events::UI::TabState::BALANCE) {
    if (deviceSelectorManager) {
      auto selections = deviceSelectorManager->getBalanceSelections();
      if (selections.isValid()) {
        auto level1 = getAudioLevel(selections.device1.getValue());
        auto level2 = getAudioLevel(selections.device2.getValue());

        if (level1 && level2) {
          resVolume = 50 + (level1->volume - level2->volume);
          // Clamp to valid range
          resVolume = constrain(resVolume, 0, 100);
          volumeFound = true;
          ESP_LOGI(TAG, "Using balance volume: %d (device1: %d, device2: %d)",
                   resVolume, level1->volume, level2->volume);
        } else {
          ESP_LOGW(TAG, "Balance devices not found: %s=%p, %s=%p",
                   selections.device1.getValue().c_str(), level1,
                   selections.device2.getValue().c_str(), level2);
        }
      } else {
        ESP_LOGW(TAG, "No valid balance selections");
      }
    }
  } else {
    // For other tabs (SINGLE), show selected device volume
    String selectedDevice = getSelectedDevice();
    ESP_LOGI(TAG, "Getting volume for selected device: '%s'",
             selectedDevice.c_str());

    if (!selectedDevice.isEmpty() && selectedDevice != "-") {
      AudioLevel *level = getAudioLevel(selectedDevice);
      if (level) {
        resVolume = level->volume;
        volumeFound = true;
        ESP_LOGI(TAG, "Using device volume: %d for %s", resVolume,
                 selectedDevice.c_str());
      } else {
        ESP_LOGW(TAG, "Audio level not found for device: %s",
                 selectedDevice.c_str());
      }
    } else {
      ESP_LOGW(TAG, "No valid device selected for volume update");
    }
  }

  if (volumeFound) {
    // Ensure volume is in valid range
    resVolume = constrain(resVolume, 0, 100);
    lv_arc_set_value(slider, resVolume);
    updateVolumeArcLabel(resVolume);
    ESP_LOGI(TAG, "Updated volume arc to: %d", resVolume);
  } else {
    ESP_LOGW(TAG, "No volume found, keeping current arc value");
  }

  suppressArcEvents = false;
}

void StatusManager::updateVolumeArcLabel(int volume) {
  lv_obj_t *label = nullptr;
  switch (currentTab) {
  case Events::UI::TabState::MASTER:
    label = ui_lblPrimaryVolumeSlider;
    break;
  case Events::UI::TabState::SINGLE:
    label = ui_lblSingleVolumeSlider;
    break;
  case Events::UI::TabState::BALANCE:
    label = ui_lblBalanceVolumeSlider;
    break;
  default:
    break;
  }
  if (label) {
    char labelText[16];
    snprintf(labelText, sizeof(labelText), "%d%%", volume);
    lv_label_set_text(label, labelText);
  }
}

void StatusManager::forceVolumeArcUpdate(void) {
  ESP_LOGI(TAG, "Force updating volume arc for current tab: %s",
           getTabName(currentTab));
  updateVolumeArcFromSelectedDevice();
}

// Volume control

void StatusManager::setSelectedDeviceVolume(int volume) {
  AUDIO_DEVICE_ACTION_PROLOGUE("volume control")

  // Clamp volume to valid range
  volume = constrain(volume, 0, 100);

  // Update the volume arc label
  updateVolumeArcLabel(volume);

  // If in MASTER tab, control the default device
  if (currentTab == Events::UI::TabState::MASTER) {
    if (currentAudioStatus.hasDefaultDevice) {
      // Update local default device volume for responsive UI
      currentAudioStatus.defaultDevice.volume = volume / 100.0f;
      ESP_LOGI(TAG, "Set default device volume to %d", volume);

      // Update default device label if available
      if (currentAudioStatus.hasDefaultDevice &&
          ui_lblPrimaryAudioDeviceValue) {
        lv_label_set_text(
            ui_lblPrimaryAudioDeviceValue,
            currentAudioStatus.defaultDevice.friendlyName.c_str());
      }
    } else {
      ESP_LOGW(TAG, "No default device available for master volume control");
      return;
    }
  } else {
    // Update local audio level immediately for responsive UI
    updateAudioLevel(currentDevice, volume);
    ESP_LOGI(TAG, "Set volume to %d for device: %s", volume,
             currentDevice.c_str());
  }

  // Publish the updated status to server
  publishStatusUpdate();
}

void StatusManager::muteSelectedDevice(void) {
  AUDIO_DEVICE_ACTION_PROLOGUE("mute")

  // If in MASTER tab, control the default device
  if (currentTab == Events::UI::TabState::MASTER) {
    if (currentAudioStatus.hasDefaultDevice) {
      currentAudioStatus.defaultDevice.isMuted = true;
      ESP_LOGI(TAG, "Muted default device");
    } else {
      ESP_LOGW(TAG, "No default device available for master mute control");
      return;
    }
  } else {
    // Find and update the selected device's mute state
    AudioLevel *level = getAudioLevel(currentDevice);
    if (level) {
      level->isMuted = true;
    }
    ESP_LOGI(TAG, "Muted device: %s", currentDevice.c_str());
  }

  // Publish the updated status to server
  publishStatusUpdate();
}

void StatusManager::unmuteSelectedDevice(void) {
  AUDIO_DEVICE_ACTION_PROLOGUE("unmute")

  // If in MASTER tab, control the default device
  if (currentTab == Events::UI::TabState::MASTER) {
    if (currentAudioStatus.hasDefaultDevice) {
      currentAudioStatus.defaultDevice.isMuted = false;
      ESP_LOGI(TAG, "Unmuted default device");
    } else {
      ESP_LOGW(TAG, "No default device available for master unmute control");
      return;
    }
  } else {
    // Find and update the selected device's mute state
    AudioLevel *level = getAudioLevel(currentDevice);
    if (level) {
      level->isMuted = false;
    }
    ESP_LOGI(TAG, "Unmuted device: %s", currentDevice.c_str());
  }

  // Publish the updated status to server
  publishStatusUpdate();
}

void StatusManager::publishStatusUpdate(void) {
  if (!Messaging::MessageBus::IsConnected()) {
    ESP_LOGW(TAG, "Cannot publish status update: No transport connected");
    return;
  }

  JsonDocument doc;
  doc["messageType"] = Messaging::Protocol::MESSAGE_STATUS_UPDATE;
  doc["requestId"] = Messaging::Protocol::generateRequestId();
  doc["timestamp"] = Hardware::Device::getMillis();

  // Add sessions array
  JsonArray sessions = doc["sessions"].to<JsonArray>();
  for (const auto &level : currentAudioStatus.audioLevels) {
    JsonObject session = sessions.add<JsonObject>();
    session["processName"] = level.processName;
    session["volume"] = level.volume / 100.0f; // Convert to 0.0-1.0 range
    session["isMuted"] = level.isMuted;
    session["state"] = "Active";
  }

  // Add default device if available
  if (currentAudioStatus.hasDefaultDevice) {
    JsonObject defaultDevice = doc["defaultDevice"].to<JsonObject>();
    defaultDevice["friendlyName"] =
        currentAudioStatus.defaultDevice.friendlyName;
    defaultDevice["volume"] = currentAudioStatus.defaultDevice.volume;
    defaultDevice["isMuted"] = currentAudioStatus.defaultDevice.isMuted;
    defaultDevice["dataFlow"] = currentAudioStatus.defaultDevice.state;
    defaultDevice["deviceRole"] = "Console";
  }

  String jsonPayload;
  serializeJson(doc, jsonPayload);
  bool published =
      Messaging::MessageBus::Publish("STATUS_UPDATE", jsonPayload.c_str());
  if (published) {
    ESP_LOGI(TAG, "Published status update with %d sessions",
             currentAudioStatus.audioLevels.size());
  } else {
    ESP_LOGE(TAG, "Failed to publish status update");
  }
}

bool StatusManager::isSuppressingArcEvents(void) { return suppressArcEvents; }

bool StatusManager::isSuppressingDropdownEvents(void) {
  return suppressDropdownEvents;
}

// Tab state management methods
Events::UI::TabState StatusManager::getCurrentTab(void) { return currentTab; }

void StatusManager::setCurrentTab(Events::UI::TabState tab) {
  currentTab = tab;
  // Update UI elements for the new tab
  onAudioLevelsChangedUI();
}

const char *StatusManager::getTabName(Events::UI::TabState tab) {
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

// Message publishing methods

void StatusManager::publishAudioStatusRequest(bool delayed) {
  if (!delayed && !Messaging::MessageBus::IsConnected()) {
    ESP_LOGW(TAG,
             "Cannot publish audio status request: No transport connected");
    return;
  }

  // Create JSON request for getting status
  JsonDocument doc;
  doc["messageType"] = Messaging::Protocol::MESSAGE_GET_STATUS;
  doc["requestId"] = Messaging::Protocol::generateRequestId();

  // Serialize to string
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  // Publish the request (delayed or immediate)
  bool published;
  if (delayed) {
    published = Messaging::MessageBus::PublishDelayed("STATUS_REQUEST",
                                                      jsonPayload.c_str());
    if (published) {
      ESP_LOGI(TAG, "Queued delayed audio status request");
    } else {
      ESP_LOGE(TAG, "Failed to queue delayed audio status request");
    }
  } else {
    published =
        Messaging::MessageBus::Publish("STATUS_REQUEST", jsonPayload.c_str());
    if (published) {
      ESP_LOGI(TAG, "Published audio status request");
    } else {
      ESP_LOGE(TAG, "Failed to publish audio status request");
    }
  }
}

// Private methods

void StatusManager::initializeMessageHandlers(void) {
  // Status message handler
  audioStatusHandler.Identifier = "AudioStatusHandler";
  audioStatusHandler.SubscribeTopic = "STATUS";
  audioStatusHandler.PublishTopic = "";
  audioStatusHandler.Callback = audioStatusMessageHandler;
  audioStatusHandler.Active = true;
}

void StatusManager::audioStatusMessageHandler(const char *messageType,
                                              const char *payload) {
  if (!initialized) {
    ESP_LOGW(TAG, "AudioStatusManager not initialized, ignoring message");
    return;
  }

  AudioStatus status = parseAudioStatusJson(payload);

  if (status.audioLevels.empty() && !status.hasDefaultDevice) {
    ESP_LOGE(TAG, "Failed to parse audio status JSON or no valid data found");
    return;
  }

  // Process the received audio status
  onAudioStatusReceived(status);
}

AudioStatus StatusManager::parseAudioStatusJson(const char *jsonPayload) {
  AudioStatus result;

  if (!jsonPayload) {
    ESP_LOGE(TAG, "Invalid JSON payload");
    return result;
  }

  // Create a JSON document
  JsonDocument doc;

  // Parse the JSON
  DeserializationError error = deserializeJson(doc, jsonPayload);

  if (error) {
    ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
    return result;
  }

  // Check if the root is an object
  if (!doc.is<JsonObject>()) {
    ESP_LOGE(TAG, "JSON root is not an object");
    return result;
  }

  JsonObject root = doc.as<JsonObject>();
  unsigned long now = Hardware::Device::getMillis();
  result.timestamp = now;

  // Parse default device if present
  JsonObject defaultDevice = root["defaultDevice"];
  if (!defaultDevice.isNull()) {
    String friendlyName = defaultDevice["friendlyName"] | "";
    float volume = defaultDevice["volume"] | 0.0f;
    bool isMuted = defaultDevice["isMuted"] | false;
    String dataFlow = defaultDevice["dataFlow"] | "";
    String deviceRole = defaultDevice["deviceRole"] | "";

    if (friendlyName.length() > 0) {
      result.defaultDevice.friendlyName = friendlyName;
      result.defaultDevice.volume = volume;
      result.defaultDevice.isMuted = isMuted;
      result.defaultDevice.state = dataFlow + "/" + deviceRole;
      result.hasDefaultDevice = true;

      ESP_LOGI(TAG, "Parsed default device: %s = %.1f%% %s [%s]",
               friendlyName.c_str(), volume * 100.0f, isMuted ? "(muted)" : "",
               result.defaultDevice.state.c_str());
    }
  }

  // Parse the new server status format
  JsonArray sessions = root["sessions"];
  if (sessions.isNull()) {
    ESP_LOGW(TAG, "No sessions array in status message");
    return result;
  }

  for (JsonObject session : sessions) {
    String processName = session["processName"] | "";
    String displayName = session["displayName"] | "";
    float volume = session["volume"] | 0.0f;
    bool isMuted = session["isMuted"] | false;
    String state = session["state"] | "";

    if (processName.length() > 0) {
      AudioLevel level;
      level.processName = processName;
      level.volume = (int)(volume * 100.0f); // Convert from 0.0-1.0 to 0-100
      level.isMuted = isMuted;
      level.lastUpdate = now;
      level.stale = false;

      result.audioLevels.push_back(level);
      ESP_LOGI(TAG, "Parsed audio session: %s = %d%% %s", processName.c_str(),
               level.volume, isMuted ? "(muted)" : "");
    }
  }

  ESP_LOGI(TAG, "Parsed %d audio sessions from status message",
           result.audioLevels.size());
  return result;
}

} // namespace Audio
} // namespace Application