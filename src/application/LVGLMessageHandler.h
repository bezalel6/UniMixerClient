#ifndef LVGL_MESSAGE_HANDLER_H
#define LVGL_MESSAGE_HANDLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lvgl.h>

namespace Application {
namespace LVGLMessageHandler {

// Message types for UI updates
typedef enum {
  MSG_UPDATE_WIFI_STATUS,
  MSG_UPDATE_NETWORK_INFO,
  MSG_UPDATE_OTA_PROGRESS,
  MSG_UPDATE_OTA_SCREEN,
  MSG_UPDATE_AUDIO_LEVELS,
  MSG_UPDATE_FPS_DISPLAY,
  MSG_SCREEN_CHANGE,
  MSG_UPDATE_VOLUME,
  MSG_REQUEST_DATA,
  MSG_MAX
} LVGLMessageType_t;

// Message structure
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
      void *screen;
      int anim_type; // lv_screen_load_anim_t cast to int for message passing
      int time;
      int delay;
    } screen_change;
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
bool changeScreen(void *screen, int anim_type, int time, int delay);

} // namespace LVGLMessageHandler
} // namespace Application

#endif // LVGL_MESSAGE_HANDLER_H