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
    MSG_HIDE_OTA_SCREEN,

    // State overview messages
    MSG_SHOW_STATE_OVERVIEW,
    MSG_UPDATE_STATE_OVERVIEW,
    MSG_HIDE_STATE_OVERVIEW,

    // SD card status messages
    MSG_UPDATE_SD_STATUS,

    // SD card format messages
    MSG_FORMAT_SD_REQUEST,
    MSG_FORMAT_SD_CONFIRM,
    MSG_FORMAT_SD_PROGRESS,
    MSG_FORMAT_SD_COMPLETE
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

        // State overview data
        struct {
            uint32_t free_heap;
            uint32_t free_psram;
            uint32_t cpu_freq;
            uint32_t uptime_ms;
            char wifi_status[32];
            int wifi_rssi;
            char ip_address[16];
            char mqtt_status[16];
            char current_tab[16];
            // Selected devices for all tabs
            char main_device[64];  // Master/Single tab device
            int main_device_volume;
            bool main_device_muted;
            char balance_device1[64];  // Balance tab device 1
            int balance_device1_volume;
            bool balance_device1_muted;
            char balance_device2[64];  // Balance tab device 2
            int balance_device2_volume;
            bool balance_device2_muted;
            // Legacy for compatibility
            char selected_device[64];
            int current_volume;
            bool is_muted;
        } state_overview;

        // SD card status data
        struct {
            const char *status;
            bool mounted;
            uint64_t total_mb;
            uint64_t used_mb;
            uint8_t card_type;
        } sd_status;

        // SD card format data
        struct {
            bool in_progress;
            bool success;
            uint8_t progress;
            char message[64];
        } sd_format;
    } data;
} LVGLMessage_t;

// Queue handle
extern QueueHandle_t lvglMessageQueue;

// Functions
bool init(void);
void deinit(void);
bool sendMessage(const LVGLMessage_t *message);
void processMessageQueue(lv_timer_t *timer);
void processComplexMessage(const LVGLMessage_t *message);

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

// Helper functions for state overview
bool showStateOverview(void);
bool updateStateOverview(void);
bool hideStateOverview(void);

// Helper functions for SD status
bool updateSDStatus(const char *status, bool mounted, uint64_t total_mb, uint64_t used_mb, uint8_t card_type);

// Helper functions for SD format operations
bool requestSDFormat(void);
bool confirmSDFormat(void);
bool updateSDFormatProgress(uint8_t progress, const char *message);
bool completeSDFormat(bool success, const char *message);

}  // namespace LVGLMessageHandler
}  // namespace Application

#endif  // LVGL_MESSAGE_HANDLER_H
