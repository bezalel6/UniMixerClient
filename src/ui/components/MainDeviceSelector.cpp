#include "MainDeviceSelector.h"

namespace UI {
namespace Components {

MainDeviceSelector::MainDeviceSelector(lv_obj_t* dropdown) : DeviceSelector(dropdown) {
}

bool MainDeviceSelector::isAvailableFor(const String& deviceName) const {
    // Main dropdown can select any device
    return !deviceName.isEmpty();
}

}  // namespace Components
}  // namespace UI