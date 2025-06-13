#ifndef DEVICE_SELECTOR_MANAGER_H
#define DEVICE_SELECTOR_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <memory>
#include "MainDeviceSelector.h"
#include "BalanceDeviceSelector.h"
#include "../../application/AudioTypes.h"

namespace UI {
namespace Components {

// Use AudioLevel from Application::Audio namespace
using AudioLevel = Application::Audio::AudioLevel;

class DeviceSelectorManager {
   public:
    DeviceSelectorManager();
    ~DeviceSelectorManager();

    // Initialization
    bool initialize(lv_obj_t* mainDropdown, lv_obj_t* balanceLeftDropdown, lv_obj_t* balanceRightDropdown);
    void deinitialize();

    // Main device selection
    void setMainSelection(const String& deviceName);
    String getMainSelection() const;
    void clearMainSelection();

    // Balance device selections
    void setBalanceLeftSelection(const String& deviceName);
    void setBalanceRightSelection(const String& deviceName);
    String getBalanceLeftSelection() const;
    String getBalanceRightSelection() const;
    void clearBalanceSelections();

    // Generic dropdown operations
    void setDropdownSelection(lv_obj_t* dropdown, const String& deviceName);
    String getDropdownSelection(lv_obj_t* dropdown) const;
    void clearAllSelections();

    // UI management
    void refreshAllDropdowns(const std::vector<AudioLevel>& audioLevels);
    void refreshDropdown(lv_obj_t* dropdown, const std::vector<AudioLevel>& audioLevels);

    // Availability checking
    bool isAvailableFor(const String& deviceName, lv_obj_t* dropdown) const;

    // Balance initialization
    void initializeBalanceSelections(const std::vector<AudioLevel>& audioLevels);

    // Getters
    MainDeviceSelector* getMainSelector() const { return mainSelector.get(); }
    BalanceDeviceSelector* getBalanceLeftSelector() const { return balanceLeftSelector.get(); }
    BalanceDeviceSelector* getBalanceRightSelector() const { return balanceRightSelector.get(); }

   private:
    std::unique_ptr<MainDeviceSelector> mainSelector;
    std::unique_ptr<BalanceDeviceSelector> balanceLeftSelector;
    std::unique_ptr<BalanceDeviceSelector> balanceRightSelector;

    bool initialized;
};

}  // namespace Components
}  // namespace UI

#endif  // DEVICE_SELECTOR_MANAGER_H