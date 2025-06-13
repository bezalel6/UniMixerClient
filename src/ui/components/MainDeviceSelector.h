#ifndef MAIN_DEVICE_SELECTOR_H
#define MAIN_DEVICE_SELECTOR_H

#include "DeviceSelector.h"

namespace UI {
namespace Components {

class MainDeviceSelector : public DeviceSelector {
   public:
    MainDeviceSelector(lv_obj_t* dropdown);
    virtual ~MainDeviceSelector() = default;

    // Override availability checking - main selector can select any device
    virtual bool isAvailableFor(const String& deviceName) const override;
};

}  // namespace Components
}  // namespace UI

#endif  // MAIN_DEVICE_SELECTOR_H