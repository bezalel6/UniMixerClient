#include "BalanceDeviceSelector.h"
#include <esp_log.h>

static const char* TAG = "BalanceDeviceSelector";

namespace UI {
namespace Components {

BalanceDeviceSelector::BalanceDeviceSelector(lv_obj_t* dropdown, BalanceDeviceSelector* otherSelector)
    : DeviceSelector(dropdown), otherSelector(otherSelector) {
}

void BalanceDeviceSelector::setOtherSelector(BalanceDeviceSelector* otherSelector) {
    this->otherSelector = otherSelector;
}

bool BalanceDeviceSelector::isAvailableFor(const String& deviceName) const {
    if (deviceName.isEmpty()) return false;

    // Check if the other balance selector has this device selected
    if (otherSelector && otherSelector->getSelection() == deviceName) {
        return false;
    }

    return true;
}

void BalanceDeviceSelector::setSelection(const String& deviceName) {
    // If we're selecting a device that the other selector has, clear the other selection
    if (otherSelector && otherSelector->getSelection() == deviceName) {
        otherSelector->clearSelection();
        ESP_LOGI(TAG, "Cleared other balance selector due to mutual exclusivity");
    }

    // Call parent implementation
    DeviceSelector::setSelection(deviceName);
}

}  // namespace Components
}  // namespace UI