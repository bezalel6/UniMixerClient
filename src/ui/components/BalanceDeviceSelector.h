#ifndef BALANCE_DEVICE_SELECTOR_H
#define BALANCE_DEVICE_SELECTOR_H

#include "DeviceSelector.h"

namespace UI {
namespace Components {

class BalanceDeviceSelector : public DeviceSelector {
   public:
    BalanceDeviceSelector(lv_obj_t* dropdown, BalanceDeviceSelector* otherSelector = nullptr);
    virtual ~BalanceDeviceSelector() = default;

    // Set the other balance selector for mutual exclusivity
    void setOtherSelector(BalanceDeviceSelector* otherSelector);

    // Override availability checking - balance selectors are mutually exclusive
    virtual bool isAvailableFor(const String& deviceName) const override;

    // Override setSelection to handle mutual exclusivity
    virtual void setSelection(const String& deviceName) override;

   private:
    BalanceDeviceSelector* otherSelector;
};

}  // namespace Components
}  // namespace UI

#endif  // BALANCE_DEVICE_SELECTOR_H