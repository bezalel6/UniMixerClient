#ifndef LVGL_MESSAGE_HANDLER_H
#define LVGL_MESSAGE_HANDLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lvgl.h>

namespace Application {
namespace LVGLMessageHandler {

// Message types
typedef enum {
    MSG_UPDATE_WIFI_STATUS,
    MSG_UPDATE_NETWORK_INFO,
    MSG_UPDATE_OTA_PROGRESS,
    MSG_UPDATE_FPS_DISPLAY,
    MSG_SCREEN_CHANGE,
    MSG_REQUEST_DATA,

    // Tab-specific volume update messages
    MSG_UPDATE_MASTER_VOLUME,
    MSG_UPDATE_SINGLE_VOLUME,
    MSG_UPDATE_BALANCE_VOLUME,

    // Tab-specific device update messages
    MSG_UPDATE_MASTER_DEVICE,
    MSG_UPDATE_SINGLE_DEVICE,
    MSG_UPDATE_BALANCE_DEVICES,

    // Dedicated messages for the custom, non-conflicting OTA screen
    MSG_SHOW_OTA_SCREEN,
    MSG_UPDATE_OTA_SCREEN_PROGRESS,
    MSG_HIDE_OTA_SCREEN
} LVGLMessageType_t;

// Message data structures
typedef struct {
    LVGLMessageType_t type;
    union {
        struct {
            const char *status;
            bool connected;
        } wifi_status;

        struct {
            const char *ssid;
            const char *ip;
        } network_info;

        struct {
            uint8_t progress;
            bool in_progress;
            bool success;
            char message[64];
        } ota_progress;

        struct {
            float fps;
        } fps_display;

        // Tab-specific volume update data
        struct {
            int volume;
        } master_volume;

        struct {
            int volume;
        } single_volume;

        struct {
            int volume;
        } balance_volume;

        // Tab-specific device update data
        struct {
            char device_name[64];
        } master_device;

        struct {
            char device_name[64];
        } single_device;

        struct {
            char device1_name[64];
            char device2_name[64];
        } balance_devices;

        struct {
            void *screen;
            int anim_type;
            int time;
            int delay;
        } screen_change;

        struct {
            uint8_t progress;
            char message[64];
        } ota_screen_progress;
    } data;
} LVGLMessage_t;

// Queue handle
extern QueueHandle_t lvglMessageQueue;

// Functions
bool init(void);
void deinit(void);
bool sendMessage(const LVGLMessage_t *message);
void processMessageQueue(lv_timer_t *timer);

// Helper functions for common messages
bool updateWifiStatus(const char *status, bool connected);
bool updateNetworkInfo(const char *ssid, const char *ip);
bool updateOTAProgress(uint8_t progress, bool in_progress, bool success,
                       const char *message);
bool updateFpsDisplay(float fps);
bool changeScreen(void *screen, int anim_type, int time, int delay);

// Tab-specific volume update functions
bool updateMasterVolume(int volume);
bool updateSingleVolume(int volume);
bool updateBalanceVolume(int volume);

// Tab-specific device update functions
bool updateMasterDevice(const char *device_name);
bool updateSingleDevice(const char *device_name);
bool updateBalanceDevices(const char *device1_name, const char *device2_name);

// Convenience function to update volume for the currently active tab
bool updateCurrentTabVolume(int volume);

// Helper functions for the custom OTA screen
bool showOtaScreen(void);
bool updateOtaScreenProgress(uint8_t progress, const char *msg);
bool hideOtaScreen(void);
void updateOtaScreenDirectly(uint8_t progress, const char *msg);

}  // namespace LVGLMessageHandler
}  // namespace Application

#endif  // LVGL_MESSAGE_HANDLER_H