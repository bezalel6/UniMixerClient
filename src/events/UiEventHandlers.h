#ifndef UI_EVENT_HANDLERS_H
#define UI_EVENT_HANDLERS_H

#include <lvgl.h>

namespace Events {
namespace UI {

// UI Event Handlers
void btnRequestDataClickedHandler(lv_event_t* e);

// Audio device selection handlers
void audioDeviceDropdownChangedHandler(lv_event_t* e);

// Volume control handlers
void volumeSliderChangedHandler(lv_event_t* e);

}  // namespace UI
}  // namespace Events

#endif  // UI_EVENT_HANDLERS_H