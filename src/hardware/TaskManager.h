#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

namespace Hardware {
namespace TaskManager {

// Core assignments
#define DISPLAY_CORE 0      // Core 0 for display/UI tasks
#define APPLICATION_CORE 1  // Core 1 for application tasks

// Task priorities (higher number = higher priority)
#define PRIORITY_LVGL_RENDER 3  // Highest priority for smooth UI
#define PRIORITY_DISPLAY_MGR 2  // High priority for display updates
#define PRIORITY_NETWORK_MGR 2  // Medium priority for network
#define PRIORITY_APP_CTRL 2     // Medium priority for app logic
#define PRIORITY_OTA_MGR 1      // Low priority for background OTA

// Task stack sizes
#define STACK_SIZE_LVGL_RENDER 8192
#define STACK_SIZE_DISPLAY_MGR 4096
#define STACK_SIZE_NETWORK_MGR 4096
#define STACK_SIZE_APP_CTRL 8192
#define STACK_SIZE_OTA_MGR 4096

// Task handles
extern TaskHandle_t lvglRenderTaskHandle;
extern TaskHandle_t displayMgrTaskHandle;
extern TaskHandle_t networkMgrTaskHandle;
extern TaskHandle_t appCtrlTaskHandle;
extern TaskHandle_t otaMgrTaskHandle;

// Inter-task communication
extern QueueHandle_t uiUpdateQueue;
extern QueueHandle_t networkStatusQueue;
extern QueueHandle_t otaProgressQueue;
extern SemaphoreHandle_t displayMutex;
extern SemaphoreHandle_t networkMutex;
extern EventGroupHandle_t systemEventGroup;

// System events
extern const EventBits_t EVENT_WIFI_CONNECTED;
extern const EventBits_t EVENT_WIFI_DISCONNECTED;
extern const EventBits_t EVENT_OTA_START;
extern const EventBits_t EVENT_OTA_PROGRESS;
extern const EventBits_t EVENT_OTA_COMPLETE;
extern const EventBits_t EVENT_OTA_ERROR;

// UI update message types
typedef enum {
    UI_UPDATE_NETWORK_STATUS,
    UI_UPDATE_OTA_PROGRESS,
    UI_UPDATE_AUDIO_STATUS,
    UI_UPDATE_FPS
} ui_update_type_t;

// UI update message structure
typedef struct {
    ui_update_type_t type;
    union {
        struct {
            char status[32];
            bool connected;
            char ssid[64];
            char ip[16];
        } network;
        struct {
            uint8_t progress;
            char status[64];
            bool in_progress;
        } ota;
        struct {
            int active_processes;
            int total_volume;
        } audio;
        struct {
            float fps;
        } fps;
    } data;
} ui_update_msg_t;

// Network status message structure
typedef struct {
    bool connected;
    char status[32];
    char ssid[64];
    char ip[16];
} network_status_msg_t;

// OTA progress message structure
typedef struct {
    uint8_t progress;
    char status[64];
    bool in_progress;
    bool error;
} ota_progress_msg_t;

// Task management functions
bool init(void);
void deinit(void);
bool startAllTasks(void);
void stopAllTasks(void);

// Inter-task communication helpers
bool sendUIUpdate(const ui_update_msg_t* msg);
bool sendNetworkStatus(const network_status_msg_t* msg);
bool sendOTAProgress(const ota_progress_msg_t* msg);
void setSystemEvent(EventBits_t events);
void clearSystemEvent(EventBits_t events);
EventBits_t waitForSystemEvent(EventBits_t events, TickType_t timeout);

// Mutex helpers
bool lockDisplay(TickType_t timeout = portMAX_DELAY);
void unlockDisplay(void);
bool lockNetwork(TickType_t timeout = portMAX_DELAY);
void unlockNetwork(void);

// Task functions (implemented in TaskManager.cpp)
void lvglRenderTask(void* parameter);
void displayMgrTask(void* parameter);
void networkMgrTask(void* parameter);
void appCtrlTask(void* parameter);
void otaMgrTask(void* parameter);

}  // namespace TaskManager
}  // namespace Hardware

#endif  // TASK_MANAGER_H