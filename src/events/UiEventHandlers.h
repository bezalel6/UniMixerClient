#ifndef UI_EVENT_HANDLERS_H
#define UI_EVENT_HANDLERS_H

#include <lvgl.h>

// UI Event Handler Macros - focused on functionality
// Only proceed if the specified event type occurs
#define ON_EVENT(event_type)                     \
    lv_event_code_t code = lv_event_get_code(e); \
    if (code != event_type)                      \
        return;

// Get the UI widget that triggered the event with proper casting
#define GET_WIDGET(type) ((type *)lv_event_get_target(e))

// Get the UI widget as a generic UI object
#define GET_UI_WIDGET() GET_WIDGET(lv_obj_t)

// Log UI events with consistent formatting
#define UI_LOG(tag, message, ...) ESP_LOGI(tag, message, ##__VA_ARGS__)

// Most common pattern: only proceed on event and get the triggering widget
#define ON_EVENT_GET_WIDGET(event_type, widget_var) \
    ON_EVENT(event_type)                            \
    lv_obj_t *widget_var = GET_UI_WIDGET();

namespace Events {
namespace UI {

// Tab state enumeration
enum class TabState { MASTER = 0,
                      SINGLE = 1,
                      BALANCE = 2 };

// UI Event Handlers
void btnRequestDataClickedHandler(lv_event_t *e);

// Audio device selection handlers
void audioDeviceDropdownChangedHandler(lv_event_t *e);

// Volume control handlers
void volumeArcChangedHandler(lv_event_t *e);

// New visual-only handler for real-time label updates during arc dragging
void volumeArcVisualHandler(lv_event_t *e);

// Tab state management
void tabSwitchHandler(lv_event_t *e);
TabState getCurrentTab(void);
void setCurrentTab(TabState tab);
const char *getTabName(TabState tab);

// State overview handler
void stateOverviewLongPressHandler(lv_event_t *e);

}  // namespace UI
}  // namespace Events

#endif  // UI_EVENT_HANDLERS_H