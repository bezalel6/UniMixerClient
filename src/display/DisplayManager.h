#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <lvgl.h>
#include <esp32_smartdisplay.h>

namespace Display {

// Display rotation management
typedef enum {
    ROTATION_0 = LV_DISPLAY_ROTATION_0,
    ROTATION_90 = LV_DISPLAY_ROTATION_90,
    ROTATION_180 = LV_DISPLAY_ROTATION_180,
    ROTATION_270 = LV_DISPLAY_ROTATION_270
} Rotation;

// Display manager functions
bool init(void);
void deinit(void);
void update(void);

// Display control
void setRotation(Rotation rotation);
Rotation getRotation(void);
void rotateNext(void);

// QR Code management
lv_obj_t* createQrCode(lv_obj_t* parent, const char* data, uint16_t size);
void updateQrCode(lv_obj_t* qr_obj, const char* data);

// UI Component helpers
void updateLabelUint32(lv_obj_t* label, uint32_t value);
void updateLabelString(lv_obj_t* label, const char* text);
void updateLabelMillivolts(lv_obj_t* label, uint32_t millivolts);

// Network UI update functions
void updateWifiStatus(lv_obj_t* statusLabel, lv_obj_t* indicatorObj, const char* statusText, bool connected);
void updateMqttStatus(lv_obj_t* mqttLabel, const char* statusText);
void updateNetworkInfo(lv_obj_t* ssidLabel, lv_obj_t* ipLabel, const char* ssid, const char* ipAddress);

// LVGL tick management
void tickUpdate(void);

}  // namespace Display

#endif  // DISPLAY_MANAGER_H