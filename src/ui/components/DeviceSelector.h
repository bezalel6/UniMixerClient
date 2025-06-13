#ifndef DEVICE_SELECTOR_H
#define DEVICE_SELECTOR_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "../../application/AudioTypes.h"

namespace UI {
namespace Components {

// Use AudioLevel from Application::Audio namespace
using AudioLevel = Application::Audio::AudioLevel;

class DeviceSelector {
   public:
    DeviceSelector(lv_obj_t* dropdown);
    virtual ~DeviceSelector() = default;

    // Core functionality
    virtual void setSelection(const String& deviceName);
    virtual String getSelection() const;
    virtual void clearSelection();

    // UI management
    virtual void refresh(const std::vector<AudioLevel>& audioLevels);
    virtual void updateOptions(const std::vector<AudioLevel>& audioLevels);
    virtual void updateSelection();

    // Availability checking
    virtual bool isAvailableFor(const String& deviceName) const;

    // Getters
    lv_obj_t* getDropdown() const { return dropdown; }
    bool hasSelection() const { return !selectedDevice.isEmpty(); }

   protected:
    lv_obj_t* dropdown;
    String selectedDevice;

    // Helper methods for subclasses
    virtual void updateDropdownOptions(const std::vector<AudioLevel>& audioLevels);
    virtual void updateDropdownSelection();
    virtual String formatDisplayName(const AudioLevel& level) const;
};

}  // namespace Components
}  // namespace UI

#endif  // DEVICE_SELECTOR_H