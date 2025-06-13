#include "DeviceSelectorManager.h"
#include <algorithm>
#include <esp_log.h>

static const char* TAG = "DeviceSelectorManager";

namespace UI {
namespace Components {

DeviceSelectorManager::DeviceSelectorManager() {
    // Initialize with empty state
    mainSelection = DeviceSelection{};
    balanceSelection = BalanceSelection{};
    availableDevices.clear();
}

DeviceSelectorManager::~DeviceSelectorManager() {
    // Clear all callbacks
    mainSelectionCallback = nullptr;
    balanceSelectionCallback = nullptr;
    deviceListCallback = nullptr;
}

void DeviceSelectorManager::setMainSelection(const DeviceSelection& selection) {
    if (mainSelection != selection) {
        ESP_LOGI(TAG, "Setting main selection from '%s' to '%s'",
                 mainSelection.getValue().c_str(), selection.getValue().c_str());
        mainSelection = selection;
        validateAndFixSelections();
        notifyMainSelectionChanged();
    }
}

DeviceSelection DeviceSelectorManager::getMainSelection() const {
    return mainSelection;
}

void DeviceSelectorManager::setBalanceSelections(const BalanceSelection& selection) {
    BalanceSelection newSelection = selection;

    // Ensure no conflicts in balance selections
    if (newSelection.hasConflict()) {
        ESP_LOGW(TAG, "Balance selection conflict detected: both devices set to '%s', clearing device2",
                 newSelection.device1.getValue().c_str());
        // If there's a conflict, keep only the first device
        newSelection.device2.clear();
    }

    if (balanceSelection != newSelection) {
        ESP_LOGI(TAG, "Setting balance selections from [%s, %s] to [%s, %s]",
                 balanceSelection.device1.getValue().c_str(),
                 balanceSelection.device2.getValue().c_str(),
                 newSelection.device1.getValue().c_str(),
                 newSelection.device2.getValue().c_str());
        balanceSelection = newSelection;
        validateAndFixSelections();
        notifyBalanceSelectionChanged();
    }
}

BalanceSelection DeviceSelectorManager::getBalanceSelections() const {
    return balanceSelection;
}

DeviceSelection DeviceSelectorManager::getSelectionForTab(int tabIndex) const {
    switch (tabIndex) {
        case 0:  // MASTER tab
        case 1:  // SINGLE tab
            return mainSelection;
        case 2:                               // BALANCE tab
            return balanceSelection.device1;  // Return first balance selection
        default:
            return DeviceSelection{};
    }
}

BalanceSelection DeviceSelectorManager::getBalanceSelectionForTab() const {
    return balanceSelection;
}

void DeviceSelectorManager::setSelectionForTab(int tabIndex, const DeviceSelection& selection) {
    switch (tabIndex) {
        case 0:  // MASTER tab
        case 1:  // SINGLE tab
            setMainSelection(selection);
            break;
        case 2:  // BALANCE tab
            // For balance tab, set the first selection and ensure no conflict with second
            BalanceSelection newBalance = balanceSelection;
            newBalance.device1 = selection;

            // Check for conflict and clear second device if needed
            if (newBalance.hasConflict()) {
                newBalance.device2.clear();
            }

            setBalanceSelections(newBalance);
            break;
    }
}

void DeviceSelectorManager::setBalanceSelectionForTab(const BalanceSelection& selection) {
    setBalanceSelections(selection);
}

void DeviceSelectorManager::updateAvailableDevices(const std::vector<AudioLevel>& audioLevels) {
    bool deviceListChanged = false;

    // Check if the device list actually changed
    if (availableDevices.size() != audioLevels.size()) {
        ESP_LOGI(TAG, "Device list size changed from %d to %d", availableDevices.size(), audioLevels.size());
        deviceListChanged = true;
    } else {
        for (size_t i = 0; i < availableDevices.size(); i++) {
            if (availableDevices[i].processName != audioLevels[i].processName ||
                availableDevices[i].stale != audioLevels[i].stale) {
                ESP_LOGI(TAG, "Device list content changed at index %d: '%s' -> '%s'",
                         i, availableDevices[i].processName.c_str(), audioLevels[i].processName.c_str());
                deviceListChanged = true;
                break;
            }
        }
    }

    if (deviceListChanged) {
        ESP_LOGI(TAG, "Updating available devices list:");
        for (size_t i = 0; i < audioLevels.size(); i++) {
            ESP_LOGI(TAG, "  [%d] %s (%d%%)%s%s", i, audioLevels[i].processName.c_str(), audioLevels[i].volume,
                     audioLevels[i].isMuted ? " [MUTED]" : "",
                     audioLevels[i].stale ? " [STALE]" : "");
        }
        availableDevices = audioLevels;
        validateAndFixSelections();
        notifyDeviceListChanged();
    }
}

std::vector<AudioLevel> DeviceSelectorManager::getAvailableDevices() const {
    return availableDevices;
}

void DeviceSelectorManager::initializeBalanceSelections() {
    auto validDevices = getValidDeviceNames();

    ESP_LOGI(TAG, "Initializing balance selections with %d valid devices:", validDevices.size());
    for (size_t i = 0; i < validDevices.size(); i++) {
        ESP_LOGI(TAG, "  [%d] %s", i, validDevices[i].c_str());
    }

    if (validDevices.empty()) {
        ESP_LOGI(TAG, "No valid devices available, clearing balance selections");
        setBalanceSelections(BalanceSelection{});
        return;
    }

    if (validDevices.size() == 1) {
        ESP_LOGI(TAG, "Only one device available, setting device1 to '%s', clearing device2", validDevices[0].c_str());
        BalanceSelection newSelection;
        newSelection.device1 = DeviceSelection{validDevices[0]};
        newSelection.device2 = DeviceSelection{};
        setBalanceSelections(newSelection);
        return;
    }

    // Find two different devices
    String device1 = validDevices[0];
    String device2 = validDevices[1];

    // Ensure they're different
    if (device1 == device2 && validDevices.size() > 2) {
        ESP_LOGW(TAG, "First two devices are identical ('%s'), searching for different device2", device1.c_str());
        for (size_t i = 2; i < validDevices.size(); i++) {
            if (validDevices[i] != device1) {
                device2 = validDevices[i];
                ESP_LOGI(TAG, "Found different device2: '%s'", device2.c_str());
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Setting balance selections: device1='%s', device2='%s'", device1.c_str(), device2.c_str());
    BalanceSelection newSelection;
    newSelection.device1 = DeviceSelection{device1};
    newSelection.device2 = (device1 != device2) ? DeviceSelection{device2} : DeviceSelection{};
    setBalanceSelections(newSelection);
}

bool DeviceSelectorManager::validateSelections() const {
    // Check if main selection is valid and available
    if (mainSelection.isValid() && !isDeviceAvailable(mainSelection.getValue())) {
        return false;
    }

    // Check if balance selections are valid and available
    if (balanceSelection.device1.isValid() && !isDeviceAvailable(balanceSelection.device1.getValue())) {
        return false;
    }

    if (balanceSelection.device2.isValid() && !isDeviceAvailable(balanceSelection.device2.getValue())) {
        return false;
    }

    // Check for balance conflicts
    if (balanceSelection.hasConflict()) {
        return false;
    }

    return true;
}

bool DeviceSelectorManager::isDeviceAvailable(const String& deviceName) const {
    if (!isValidDeviceName(deviceName)) {
        return false;
    }

    for (const auto& device : availableDevices) {
        if (device.processName == deviceName) {
            return true;
        }
    }

    return false;
}

void DeviceSelectorManager::setMainSelectionCallback(MainSelectionCallback callback) {
    mainSelectionCallback = callback;
}

void DeviceSelectorManager::setBalanceSelectionCallback(BalanceSelectionCallback callback) {
    balanceSelectionCallback = callback;
}

void DeviceSelectorManager::setDeviceListCallback(DeviceListCallback callback) {
    deviceListCallback = callback;
}

void DeviceSelectorManager::validateAndFixSelections() {
    bool changed = false;

    ESP_LOGD(TAG, "Validating selections...");

    // Validate and fix main selection
    if (mainSelection.isValid() && !isDeviceAvailable(mainSelection.getValue())) {
        ESP_LOGW(TAG, "Main selection '%s' is no longer available, clearing", mainSelection.getValue().c_str());
        mainSelection.clear();
        changed = true;
    }

    // Validate and fix balance selections
    if (balanceSelection.device1.isValid() && !isDeviceAvailable(balanceSelection.device1.getValue())) {
        ESP_LOGW(TAG, "Balance device1 '%s' is no longer available, clearing", balanceSelection.device1.getValue().c_str());
        balanceSelection.device1.clear();
        changed = true;
    }

    if (balanceSelection.device2.isValid() && !isDeviceAvailable(balanceSelection.device2.getValue())) {
        ESP_LOGW(TAG, "Balance device2 '%s' is no longer available, clearing", balanceSelection.device2.getValue().c_str());
        balanceSelection.device2.clear();
        changed = true;
    }

    // Fix balance conflicts
    if (balanceSelection.hasConflict()) {
        ESP_LOGW(TAG, "Balance conflict detected, clearing device2 (both were set to '%s')",
                 balanceSelection.device1.getValue().c_str());
        balanceSelection.device2.clear();
        changed = true;
    }

    // Notify if changes were made
    if (changed) {
        ESP_LOGI(TAG, "Selections were validated and fixed, notifying callbacks");
        notifyMainSelectionChanged();
        notifyBalanceSelectionChanged();
    } else {
        ESP_LOGD(TAG, "All selections are valid");
    }
}

void DeviceSelectorManager::notifyMainSelectionChanged() {
    if (mainSelectionCallback) {
        mainSelectionCallback(mainSelection);
    }
}

void DeviceSelectorManager::notifyBalanceSelectionChanged() {
    if (balanceSelectionCallback) {
        balanceSelectionCallback(balanceSelection);
    }
}

void DeviceSelectorManager::notifyDeviceListChanged() {
    if (deviceListCallback) {
        deviceListCallback(availableDevices);
    }
}

bool DeviceSelectorManager::isValidDeviceName(const String& deviceName) const {
    return !deviceName.isEmpty() && deviceName != "-";
}

std::vector<String> DeviceSelectorManager::getValidDeviceNames() const {
    std::vector<String> devices;
    for (const auto& device : availableDevices) {
        if (isValidDeviceName(device.processName)) {
            devices.push_back(device.processName);
        }
    }
    return devices;
}

}  // namespace Components
}  // namespace UI