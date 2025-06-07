#ifndef UI_EVENT_HANDLERS_H
#define UI_EVENT_HANDLERS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI Event Handlers
void OnAddOneClicked(lv_event_t *e);
void OnRotateClicked(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif  // UI_EVENT_HANDLERS_H