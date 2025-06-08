#include "display_manager.h"
#include <ui/ui.h>
#include <string.h>
#include <stdio.h>
#include <cinttypes>

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
        sprintf(buffer, "%" PRIu32, value);
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
        sprintf(buffer, "%" PRIu32 " mV", millivolts);
        lv_label_set_text(label, buffer);
    }
}

void display_tick_update(void) {
    auto now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;
}

void display_update_wifi_status(lv_obj_t* status_label, lv_obj_t* indicator_obj, const char* status_text, bool connected) {
    if (status_label && status_text) {
        lv_label_set_text(status_label, status_text);
    }

    if (indicator_obj) {
        // Position indicator to the left of the status label
        if (status_label) {
            lv_obj_align_to(indicator_obj, status_label, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }

        // Clear text and style as round background indicator
        lv_label_set_text(indicator_obj, "");
        lv_obj_set_style_radius(indicator_obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicator_obj, LV_OPA_80, LV_PART_MAIN);

        if (connected) {
            // Green background for connected
            lv_obj_set_style_bg_color(indicator_obj, lv_color_hex(0x00FF00), LV_PART_MAIN);
        } else if (strcmp(status_text, "Connecting...") == 0) {
            // Yellow background for connecting
            lv_obj_set_style_bg_color(indicator_obj, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        } else {
            // Red background for disconnected/failed
            lv_obj_set_style_bg_color(indicator_obj, lv_color_hex(0xFF0000), LV_PART_MAIN);
        }
    }
}

void display_update_network_info(lv_obj_t* ssid_label, lv_obj_t* ip_label, const char* ssid, const char* ip_address) {
    if (ssid_label && ssid && strlen(ssid) > 0) {
        lv_label_set_text(ssid_label, ssid);
    } else if (ssid_label) {
        lv_label_set_text(ssid_label, "N/A");
    }

    if (ip_label && ip_address && strlen(ip_address) > 0) {
        lv_label_set_text(ip_label, ip_address);
    } else if (ip_label) {
        lv_label_set_text(ip_label, "0.0.0.0");
    }
}

void display_update_mqtt_status(lv_obj_t* mqtt_label, const char* status_text) {
    if (mqtt_label && status_text) {
        lv_label_set_text(mqtt_label, status_text);
    }
}