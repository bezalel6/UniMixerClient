THIS SHOULD BE A LINTER ERROR#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <esp32_smartdisplay.h>
#include <lvgl.h>

namespace Display {

// Display rotation management
typedef enum {
    ROTATION_0 = LV_DISPLAY_ROTATION_0,
    ROTATION_90 = LV_DISPLAY_ROTATION_90,
    ROTATION_180 = LV_DISPLAY_ROTATION_180,
    ROTATION_270 = LV_DISPLAY_ROTATION_270
} Rotation;

// Connection status types for indicators
typedef enum {
    CONNECTION_STATUS_DISCONNECTED = 0,
    CONNECTION_STATUS_CONNECTING,
    CONNECTION_STATUS_CONNECTED,
    CONNECTION_STATUS_FAILED,
    CONNECTION_STATUS_ERROR
} ConnectionStatus;

// Display manager functions
bool init(void);
void deinit(void);
void tick(void);
void onLvglRenderComplete(void);
void update(void);

// FPS display
void updateFpsDisplay(lv_obj_t *fpsLabel);
float getFPS(void);
float getActualRenderFPS(void);
uint32_t getUIResponseTime(void);

// Display control
void setRotation(Rotation rotation);
Rotation getRotation(void);
void rotateNext(void);

// UI Component helpers
void updateLabelUint32(lv_obj_t *label, uint32_t value);
void updateLabelString(lv_obj_t *label, const char *text);
void updateLabelMillivolts(lv_obj_t *label, uint32_t millivolts);
void updateDropdownOptions(lv_obj_t *dropdown, const char *options);

// Connection status update functions (generalized)
void updateConnectionStatus(lv_obj_t *statusLabel, lv_obj_t *indicatorObj,
                            const char *statusText, ConnectionStatus status);

// Legacy functions for backward compatibility
void updateWifiStatus(lv_obj_t *statusLabel, lv_obj_t *indicatorObj,
                      const char *statusText, bool connected);
void updateMqttStatus(lv_obj_t *mqttLabel, const char *statusText);
void updateMqttStatus(lv_obj_t *mqttLabel, lv_obj_t *indicatorObj,
                      const char *statusText);
void updateNetworkInfo(lv_obj_t *ssidLabel, lv_obj_t *ipLabel, const char *ssid,
                       const char *ipAddress);

// LVGL tick management
void tickUpdate(void);

// Helper functions for consistent label initialization
void initializeLabelEmpty(lv_obj_t *label);
void initializeLabelDash(lv_obj_t *label);
void initializeLabelSpace(lv_obj_t *label);
void initializeLabelUnknown(lv_obj_t *label);
void initializeLabelNone(lv_obj_t *label);

// Function to move widgets with User 1 state to background
void moveUser1WidgetsToBackground(lv_obj_t *parent = nullptr);

}  // namespace Display

#endif  // DISPLAY_MANAGER_H