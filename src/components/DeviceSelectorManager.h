#ifndef DEVICE_SELECTOR_MANAGER_H
#define DEVICE_SELECTOR_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <utility>
#include <optional>
#include <functional>
#include "../application/AudioTypes.h"

using Application::Audio::AudioLevel;

namespace UI {
namespace Components {

// Selection state for different tabs
struct DeviceSelection {
    std::optional<String> deviceName;

    bool isValid() const {
        return deviceName.has_value() && !deviceName->isEmpty() && *deviceName != "-";
    }

    String getValue() const {
        return deviceName.value_or("-");
    }

    void clear() {
        deviceName.reset();
    }

    bool operator==(const DeviceSelection& other) const {
        return deviceName == other.deviceName;
    }

    bool operator!=(const DeviceSelection& other) const {
        return !(*this == other);
    }
};

struct BalanceSelection {
    DeviceSelection device1;
    DeviceSelection device2;

    bool isValid() const {
        return device1.isValid() && device2.isValid();
    }

    bool hasConflict() const {
        return device1.isValid() && device2.isValid() && device1.getValue() == device2.getValue();
    }

    std::pair<String, String> getValues() const {
        return {device1.getValue(), device2.getValue()};
    }

    void clear() {
        device1.clear();
        device2.clear();
    }

    bool operator==(const BalanceSelection& other) const {
        return device1 == other.device1 && device2 == other.device2;
    }

    bool operator!=(const BalanceSelection& other) const {
        return !(*this == other);
    }
};

// State change callback types
typedef std::function<void(const DeviceSelection&)> MainSelectionCallback;
typedef std::function<void(const BalanceSelection&)> BalanceSelectionCallback;
typedef std::function<void(const std::vector<AudioLevel>&)> DeviceListCallback;

class DeviceSelectorManager {
   public:
    DeviceSelectorManager();
    ~DeviceSelectorManager();

    // State management - no UI dependencies
    void setMainSelection(const DeviceSelection& selection);
    DeviceSelection getMainSelection() const;

    void setBalanceSelections(const BalanceSelection& selection);
    BalanceSelection getBalanceSelections() const;

    // Tab-aware device selection
    DeviceSelection getSelectionForTab(int tabIndex) const;
    BalanceSelection getBalanceSelectionForTab() const;
    void setSelectionForTab(int tabIndex, const DeviceSelection& selection);
    void setBalanceSelectionForTab(const BalanceSelection& selection);

    // Device list management
    void updateAvailableDevices(const std::vector<AudioLevel>& audioLevels);
    std::vector<AudioLevel> getAvailableDevices() const;

    // Auto-initialize balance selections from available devices
    void initializeBalanceSelections();

    // State validation
    bool validateSelections() const;
    bool isDeviceAvailable(const String& deviceName) const;

    // Callback registration for state changes
    void setMainSelectionCallback(MainSelectionCallback callback);
    void setBalanceSelectionCallback(BalanceSelectionCallback callback);
    void setDeviceListCallback(DeviceListCallback callback);

   private:
    // Current state - no UI references
    DeviceSelection mainSelection;
    BalanceSelection balanceSelection;
    std::vector<AudioLevel> availableDevices;

    // State change callbacks
    MainSelectionCallback mainSelectionCallback;
    BalanceSelectionCallback balanceSelectionCallback;
    DeviceListCallback deviceListCallback;

    // Helper methods for state management
    void validateAndFixSelections();
    void notifyMainSelectionChanged();
    void notifyBalanceSelectionChanged();
    void notifyDeviceListChanged();

    // Device name validation
    bool isValidDeviceName(const String& deviceName) const;
    std::vector<String> getValidDeviceNames() const;
};

}  // namespace Components
}  // namespace UI

#endif  // DEVICE_SELECTOR_MANAGER_H