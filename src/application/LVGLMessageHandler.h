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
  MSG_UPDATE_OTA_PROGRESS, // Kept for other potential uses
  MSG_UPDATE_FPS_DISPLAY,
  MSG_UPDATE_VOLUME,
  MSG_UPDATE_DEFAULT_DEVICE,
  MSG_SCREEN_CHANGE,
  MSG_REQUEST_DATA,

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

    struct {
      int volume;
    } volume_update;

    struct {
      char device_name[64];
    } default_device;

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
bool updateVolumeLevel(int volume);
bool updateDefaultDevice(const char *device_name);
bool changeScreen(void *screen, int anim_type, int time, int delay);

// Helper functions for the custom OTA screen
bool showOtaScreen(void);
bool updateOtaScreenProgress(uint8_t progress, const char *msg);
bool hideOtaScreen(void);
void updateOtaScreenDirectly(uint8_t progress, const char *msg);

} // namespace LVGLMessageHandler
} // namespace Application

#endif // LVGL_MESSAGE_HANDLER_H