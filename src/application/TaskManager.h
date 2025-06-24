#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace Application {
namespace TaskManager {

// Task configuration constants
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define NETWORK_TASK_STACK_SIZE (6 * 1024)
#define MESSAGING_TASK_STACK_SIZE (8 * 1024)
#define OTA_TASK_STACK_SIZE (8 * 1024)
#define AUDIO_TASK_STACK_SIZE (4 * 1024)

// Task priorities (higher number = higher priority)
#define LVGL_TASK_PRIORITY (configMAX_PRIORITIES - 1)       // Highest priority
#define OTA_TASK_PRIORITY (configMAX_PRIORITIES - 2)        // Medium priority
#define NETWORK_TASK_PRIORITY (configMAX_PRIORITIES - 3)    // Medium-high priority
#define MESSAGING_TASK_PRIORITY (configMAX_PRIORITIES - 3)  // High priority
#define AUDIO_TASK_PRIORITY 1                               // Lower priority to prevent watchdog issues

// Core assignment for ESP32-S3 (optimized for performance)
#define LVGL_TASK_CORE 0     // Core 0 for UI/LVGL (Arduino loop core)
#define NETWORK_TASK_CORE 1  // Core 1 for network operations
#define MESSAGING_TASK_CORE \
    1                      // Core 1 for messaging (moved from Core 0 for better performance)
#define OTA_TASK_CORE 1    // Core 1 for OTA (network intensive)
#define AUDIO_TASK_CORE 0  // Core 0 for audio (needs UI updates)

// Update intervals (in milliseconds)
#define LVGL_UPDATE_INTERVAL 16  // 16ms = 60 FPS (was 33ms = 30 FPS)
#define NETWORK_UPDATE_INTERVAL \
    500                               // 500ms for network checks (reduced frequency for performance)
#define MESSAGING_UPDATE_INTERVAL 50  // 50ms to reduce CPU load (was 20ms for responsive messaging)
#define OTA_UPDATE_INTERVAL \
    2000  // 2000ms for OTA checks (further reduced CPU usage)
#define AUDIO_UPDATE_INTERVAL \
    1000  // 1000ms for audio status (reduced frequency for performance)

// Task handles
extern TaskHandle_t lvglTaskHandle;
extern TaskHandle_t networkTaskHandle;
extern TaskHandle_t messagingTaskHandle;
extern TaskHandle_t otaTaskHandle;
extern TaskHandle_t audioTaskHandle;

// Synchronization objects
extern SemaphoreHandle_t lvglMutex;
extern QueueHandle_t otaProgressQueue;

// OTA Progress structure
typedef struct {
    uint8_t progress;  // 0-100
    bool inProgress;   // true if OTA is active
    bool success;      // true if completed successfully
    char message[64];  // Status message
} OTAProgressData_t;

// Task function declarations
void lvglTask(void *parameter);
void networkTask(void *parameter);
void messagingTask(void *parameter);
void otaTask(void *parameter);
void audioTask(void *parameter);

// Task management functions
bool init(void);
void deinit(void);
void suspend(void);
void resume(void);

// OTA-specific task management
void suspendForOTA(void);
void resumeFromOTA(void);

// LVGL thread safety functions
void lvglLock(void);
void lvglUnlock(void);
bool lvglTryLock(uint32_t timeoutMs = 100);

// OTA progress functions
void updateOTAProgress(uint8_t progress, bool inProgress, bool success,
                       const char *message);
bool getOTAProgress(OTAProgressData_t *data);

// Task monitoring
void printTaskStats(void);
uint32_t getLvglTaskHighWaterMark(void);
uint32_t getNetworkTaskHighWaterMark(void);

}  // namespace TaskManager
}  // namespace Application

#endif  // TASK_MANAGER_H
