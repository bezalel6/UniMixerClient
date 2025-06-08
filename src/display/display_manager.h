#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <lvgl.h>
#include <esp32_smartdisplay.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display rotation management
typedef enum {
    DISPLAY_ROTATION_0 = LV_DISPLAY_ROTATION_0,
    DISPLAY_ROTATION_90 = LV_DISPLAY_ROTATION_90,
    DISPLAY_ROTATION_180 = LV_DISPLAY_ROTATION_180,
    DISPLAY_ROTATION_270 = LV_DISPLAY_ROTATION_270
} display_rotation_t;

// Display manager functions
bool display_manager_init(void);
void display_manager_deinit(void);
void display_manager_update(void);

// Display control
void display_set_rotation(display_rotation_t rotation);
display_rotation_t display_get_rotation(void);
void display_rotate_next(void);

// QR Code management
lv_obj_t* display_create_qr_code(lv_obj_t* parent, const char* data, uint16_t size);
void display_update_qr_code(lv_obj_t* qr_obj, const char* data);

// UI Component helpers
void display_update_label_uint32(lv_obj_t* label, uint32_t value);
void display_update_label_string(lv_obj_t* label, const char* text);
void display_update_label_millivolts(lv_obj_t* label, uint32_t millivolts);

// Network UI update functions
void display_update_wifi_status(lv_obj_t* status_label, lv_obj_t* indicator_obj, const char* status_text, bool connected);
void display_update_mqtt_status(lv_obj_t* mqtt_label, const char* status_text);
void display_update_network_info(lv_obj_t* ssid_label, lv_obj_t* ip_label, const char* ssid, const char* ip_address);

// LVGL tick management
void display_tick_update(void);

#ifdef __cplusplus
}
#endif

#endif  // DISPLAY_MANAGER_H