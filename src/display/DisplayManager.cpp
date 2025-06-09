#include "DisplayManager.h"
#include <ui/ui.h>
#include <string.h>
#include <stdio.h>
#include <cinttypes>

namespace Display {

// Private variables
static lv_obj_t* currentQrCode = nullptr;
static unsigned long lvLastTick = 0;

bool init(void) {
    // Initialize the smart display
    smartdisplay_init();

    // Initialize the UI
    ui_init();

    // Initialize tick tracking
    lvLastTick = millis();

    return true;
}

void deinit(void) {
    if (currentQrCode) {
        lv_obj_del(currentQrCode);
        currentQrCode = nullptr;
    }
    ui_destroy();
}

void update(void) {
    tickUpdate();
    lv_timer_handler();
}

void setRotation(Rotation rotation) {
    auto disp = lv_disp_get_default();
    if (disp) {
        lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
    }
}

Rotation getRotation(void) {
    auto disp = lv_disp_get_default();
    if (disp) {
        return (Rotation)lv_disp_get_rotation(disp);
    }
    return ROTATION_0;
}

void rotateNext(void) {
    auto current = getRotation();
    auto next = (Rotation)((current + 1) % (ROTATION_270 + 1));
    setRotation(next);
}

lv_obj_t* createQrCode(lv_obj_t* parent, const char* data, uint16_t size) {
    if (!parent || !data) return nullptr;

    // Clean up existing QR code if any
    if (currentQrCode) {
        lv_obj_del(currentQrCode);
    }

    // Create new QR code
    currentQrCode = lv_qrcode_create(parent);
    if (currentQrCode) {
        lv_qrcode_set_size(currentQrCode, size);
        lv_qrcode_set_dark_color(currentQrCode, lv_color_black());
        lv_qrcode_set_light_color(currentQrCode, lv_color_white());
        lv_qrcode_update(currentQrCode, data, strlen(data));
        lv_obj_center(currentQrCode);
    }

    return currentQrCode;
}

void updateQrCode(lv_obj_t* qr_obj, const char* data) {
    if (qr_obj && data) {
        lv_qrcode_update(qr_obj, data, strlen(data));
    }
}

void updateLabelUint32(lv_obj_t* label, uint32_t value) {
    if (label) {
        char buffer[32];
        sprintf(buffer, "%" PRIu32, value);
        lv_label_set_text(label, buffer);
    }
}

void updateLabelString(lv_obj_t* label, const char* text) {
    if (label && text) {
        lv_label_set_text(label, text);
    }
}

void updateLabelMillivolts(lv_obj_t* label, uint32_t millivolts) {
    if (label) {
        char buffer[32];
        sprintf(buffer, "%" PRIu32 " mV", millivolts);
        lv_label_set_text(label, buffer);
    }
}

void tickUpdate(void) {
    auto now = millis();
    lv_tick_inc(now - lvLastTick);
    lvLastTick = now;
}

void updateWifiStatus(lv_obj_t* statusLabel, lv_obj_t* indicatorObj, const char* statusText, bool connected) {
    if (statusLabel && statusText) {
        lv_label_set_text(statusLabel, statusText);
    }

    if (indicatorObj) {
        // Position indicator to the left of the status label
        if (statusLabel) {
            lv_obj_align_to(indicatorObj, statusLabel, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }

        // Clear text and style as round background indicator
        lv_label_set_text(indicatorObj, "");
        lv_obj_set_style_radius(indicatorObj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicatorObj, LV_OPA_80, LV_PART_MAIN);

        if (connected) {
            // Green background for connected
            lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0x00FF00), LV_PART_MAIN);
        } else if (strcmp(statusText, "Connecting...") == 0) {
            // Yellow background for connecting
            lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        } else {
            // Red background for disconnected/failed
            lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFF0000), LV_PART_MAIN);
        }
    }
}

void updateNetworkInfo(lv_obj_t* ssidLabel, lv_obj_t* ipLabel, const char* ssid, const char* ipAddress) {
    if (ssidLabel && ssid && strlen(ssid) > 0) {
        lv_label_set_text(ssidLabel, ssid);
    } else if (ssidLabel) {
        lv_label_set_text(ssidLabel, "N/A");
    }

    if (ipLabel && ipAddress && strlen(ipAddress) > 0) {
        lv_label_set_text(ipLabel, ipAddress);
    } else if (ipLabel) {
        lv_label_set_text(ipLabel, "0.0.0.0");
    }
}

void updateMqttStatus(lv_obj_t* mqttLabel, const char* statusText) {
    if (mqttLabel && statusText) {
        lv_label_set_text(mqttLabel, statusText);
    }
}

}  // namespace Display