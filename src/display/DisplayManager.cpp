#include "DisplayManager.h"
#include <ui/ui.h>
#include <string.h>
#include <stdio.h>
#include <cinttypes>
static const char* TAG = "DisplayManager";

namespace Display {

// Private variables
static unsigned long lvLastTick = 0;
static unsigned long frameCount = 0;
static unsigned long lastFpsTime = 0;
static float currentFPS = 0.0f;
static const unsigned long FPS_UPDATE_INTERVAL = 500;  // Update FPS every 500ms

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
    ui_destroy();
}

void update(void) {
    // Update frame count for FPS calculation
    frameCount++;

    unsigned long now = millis();
    if (now - lastFpsTime >= FPS_UPDATE_INTERVAL) {
        // Calculate FPS based on actual time difference
        if (now > lastFpsTime) {  // Prevent division by zero
            currentFPS = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
        } else {
            currentFPS = 0.0f;
        }

        // Clamp FPS to reasonable range (0-120 FPS)
        if (currentFPS > 120.0f) {
            currentFPS = 120.0f;
        }

        frameCount = 0;
        lastFpsTime = now;
    }
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
    if (label == NULL) return;

    float volts = millivolts / 1000.0f;
    char text[32];
    snprintf(text, sizeof(text), "%.2fV", volts);
    lv_label_set_text(label, text);
}

void updateDropdownOptions(lv_obj_t* dropdown, const char* options) {
    if (dropdown == NULL || options == NULL) {
        ESP_LOGW(TAG, "updateDropdownOptions: Invalid parameters");
        return;
    }

    // Update the dropdown options
    lv_dropdown_set_options(dropdown, options);
    ESP_LOGD(TAG, "Updated dropdown options: %s", options);
}

void tickUpdate(void) {
    auto now = millis();
    lv_tick_inc(now - lvLastTick);
    lvLastTick = now;
}

void updateConnectionStatus(lv_obj_t* statusLabel, lv_obj_t* indicatorObj, const char* statusText, ConnectionStatus status) {
    // Update status text
    if (statusLabel && statusText) {
        lv_label_set_text(statusLabel, statusText);
    }

    // Update indicator if provided
    if (indicatorObj) {
        // Position indicator to the left of the status label
        if (statusLabel) {
            lv_obj_align_to(indicatorObj, statusLabel, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }

        // Clear text and style as round background indicator
        lv_label_set_text(indicatorObj, "");
        lv_obj_set_style_radius(indicatorObj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicatorObj, LV_OPA_80, LV_PART_MAIN);

        // Set color based on connection status
        switch (status) {
            case CONNECTION_STATUS_CONNECTED:
                // Green background for connected
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0x00FF00), LV_PART_MAIN);
                break;
            case CONNECTION_STATUS_CONNECTING:
                // Yellow background for connecting
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFFFF00), LV_PART_MAIN);
                break;
            case CONNECTION_STATUS_FAILED:
            case CONNECTION_STATUS_ERROR:
            case CONNECTION_STATUS_DISCONNECTED:
            default:
                // Red background for disconnected/failed/error
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFF0000), LV_PART_MAIN);
                break;
        }
    }
}

void updateWifiStatus(lv_obj_t* statusLabel, lv_obj_t* indicatorObj, const char* statusText, bool connected) {
    // Convert boolean to ConnectionStatus enum
    ConnectionStatus status;
    if (connected) {
        status = CONNECTION_STATUS_CONNECTED;
    } else if (statusText && strcmp(statusText, "Connecting...") == 0) {
        status = CONNECTION_STATUS_CONNECTING;
    } else {
        status = CONNECTION_STATUS_DISCONNECTED;
    }

    updateConnectionStatus(statusLabel, indicatorObj, statusText, status);
}

void updateNetworkInfo(lv_obj_t* ssidLabel, lv_obj_t* ipLabel, const char* ssid, const char* ipAddress) {
    if (ssidLabel && ssid) {
        lv_label_set_text(ssidLabel, ssid);
    }
    if (ipLabel && ipAddress) {
        lv_label_set_text(ipLabel, ipAddress);
    }
}

static ConnectionStatus statusStringToConnectionStatus(const char* statusText) {
    if (!statusText) return CONNECTION_STATUS_DISCONNECTED;

    if (strstr(statusText, "Connected") != NULL) {
        return CONNECTION_STATUS_CONNECTED;
    } else if (strstr(statusText, "Connecting") != NULL) {
        return CONNECTION_STATUS_CONNECTING;
    } else if (strstr(statusText, "Failed") != NULL) {
        return CONNECTION_STATUS_FAILED;
    } else if (strstr(statusText, "Error") != NULL) {
        return CONNECTION_STATUS_ERROR;
    } else {
        return CONNECTION_STATUS_DISCONNECTED;
    }
}

void updateMqttStatus(lv_obj_t* mqttLabel, const char* statusText) {
    updateLabelString(mqttLabel, statusText);
}

void updateMqttStatus(lv_obj_t* mqttLabel, lv_obj_t* indicatorObj, const char* statusText) {
    ConnectionStatus status = statusStringToConnectionStatus(statusText);
    updateConnectionStatus(mqttLabel, indicatorObj, statusText, status);
}

float getFPS(void) {
    return currentFPS;
}

void updateFpsDisplay(lv_obj_t* fpsLabel) {
    if (fpsLabel) {
        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%.1f FPS", currentFPS);
        lv_label_set_text(fpsLabel, fpsText);
    }
}

}  // namespace Display