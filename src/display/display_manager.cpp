#include "display_manager.h"
#include <ui/ui.h>
#include <string.h>
#include <stdio.h>

// Private variables
static lv_obj_t* current_qr_code = nullptr;
static unsigned long lv_last_tick = 0;

bool display_manager_init(void) {
    // Initialize the smart display
    smartdisplay_init();

    // Initialize the UI
    ui_init();

    // Initialize tick tracking
    lv_last_tick = millis();

    return true;
}

void display_manager_deinit(void) {
    if (current_qr_code) {
        lv_obj_del(current_qr_code);
        current_qr_code = nullptr;
    }
    ui_destroy();
}

void display_manager_update(void) {
    display_tick_update();
    lv_timer_handler();
}

void display_set_rotation(display_rotation_t rotation) {
    auto disp = lv_disp_get_default();
    if (disp) {
        lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
    }
}

display_rotation_t display_get_rotation(void) {
    auto disp = lv_disp_get_default();
    if (disp) {
        return (display_rotation_t)lv_disp_get_rotation(disp);
    }
    return DISPLAY_ROTATION_0;
}

void display_rotate_next(void) {
    auto current = display_get_rotation();
    auto next = (display_rotation_t)((current + 1) % (DISPLAY_ROTATION_270 + 1));
    display_set_rotation(next);
}

lv_obj_t* display_create_qr_code(lv_obj_t* parent, const char* data, uint16_t size) {
    if (!parent || !data) return nullptr;

    // Clean up existing QR code if any
    if (current_qr_code) {
        lv_obj_del(current_qr_code);
    }

    // Create new QR code
    current_qr_code = lv_qrcode_create(parent);
    if (current_qr_code) {
        lv_qrcode_set_size(current_qr_code, size);
        lv_qrcode_set_dark_color(current_qr_code, lv_color_black());
        lv_qrcode_set_light_color(current_qr_code, lv_color_white());
        lv_qrcode_update(current_qr_code, data, strlen(data));
        lv_obj_center(current_qr_code);
    }

    return current_qr_code;
}

void display_update_qr_code(lv_obj_t* qr_obj, const char* data) {
    if (qr_obj && data) {
        lv_qrcode_update(qr_obj, data, strlen(data));
    }
}

void display_update_label_uint32(lv_obj_t* label, uint32_t value) {
    if (label) {
        char buffer[32];
        sprintf(buffer, "%lu", value);
        lv_label_set_text(label, buffer);
    }
}

void display_update_label_string(lv_obj_t* label, const char* text) {
    if (label && text) {
        lv_label_set_text(label, text);
    }
}

void display_update_label_millivolts(lv_obj_t* label, uint32_t millivolts) {
    if (label) {
        char buffer[32];
        sprintf(buffer, "%lu mV", millivolts);
        lv_label_set_text(label, buffer);
    }
}

void display_tick_update(void) {
    auto now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
}