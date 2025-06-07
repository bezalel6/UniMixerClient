#include "ui_event_handlers.h"
#include <ui/ui.h>
#include <esp32_smartdisplay.h>

void OnAddOneClicked(lv_event_t *e) {
    static uint32_t cnt = 0;
    cnt++;
    lv_label_set_text_fmt(ui_lblCountValue, "%u", cnt);
}

void OnRotateClicked(lv_event_t *e) {
    auto disp = lv_disp_get_default();
    auto rotation = (lv_display_rotation_t)((lv_disp_get_rotation(disp) + 1) % (LV_DISPLAY_ROTATION_270 + 1));
    lv_display_set_rotation(disp, rotation);
}