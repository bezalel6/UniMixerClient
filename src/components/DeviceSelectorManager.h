#ifndef DEVICE_SELECTOR_MANAGER_H
#define DEVICE_SELECTOR_MANAGER_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "../application/AudioTypes.h"

using Application::Audio::AudioLevel;

namespace UI {
namespace Components {

class DeviceSelectorManager {
   public:
    DeviceSelectorManager();
    ~DeviceSelectorManager();

    // Initialize with UI dropdown elements
    bool initialize(lv_obj_t* mainDropdown, lv_obj_t* balanceDropdown1, lv_obj_t* balanceDropdown2);
    void deinitialize();

    // Device selection management
    void setMainSelection(const String& deviceName);
    String getMainSelection() const;

    void setBalanceSelections(const String& device1, const String& device2);
    String getBalanceSelection1() const;
    String getBalanceSelection2() const;

    // Tab-aware device selection
    String getSelectedDeviceForTab(int tabIndex) const;
    void setSelectedDeviceForTab(int tabIndex, const String& deviceName);

    // UI updates
    void refreshAllDropdowns(const std::vector<AudioLevel>& audioLevels);
    void setDropdownSelection(lv_obj_t* dropdown, const String& deviceName);
    String getDropdownSelection(lv_obj_t* dropdown) const;

    // Balance dropdown initialization
    void initializeBalanceSelections(const std::vector<AudioLevel>& audioLevels);

   private:
    // UI elements
    lv_obj_t* mainDropdown;
    lv_obj_t* balanceDropdown1;
    lv_obj_t* balanceDropdown2;

    // Current selections
    String mainSelection;
    String balanceSelection1;
    String balanceSelection2;

    // Helper methods
    void updateDropdownOptions(lv_obj_t* dropdown, const std::vector<AudioLevel>& audioLevels);
    void ensureBalanceExclusivity();
    String extractDeviceNameFromDropdownText(const String& dropdownText) const;
};

}  // namespace Components
}  // namespace UI

#endif  // DEVICE_SELECTOR_MANAGER_H