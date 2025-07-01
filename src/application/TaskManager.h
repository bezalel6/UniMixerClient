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

// LVGL performance monitoring thresholds
#define LVGL_DURATION_CRITICAL_STARTUP 300  // Critical threshold during startup (ms)
#define LVGL_DURATION_WARNING_STARTUP 200   // Warning threshold during startup (ms)
#define LVGL_DURATION_INFO_STARTUP 100      // Info threshold during startup (ms)
#define LVGL_DURATION_CRITICAL_NORMAL 100   // Critical threshold during normal operation (ms)
#define LVGL_DURATION_WARNING_NORMAL 50     // Warning threshold during normal operation (ms)

// Dynamic priority management
#define LVGL_TASK_PRIORITY_HIGH (configMAX_PRIORITIES - 1)       // During normal operation
#define LVGL_TASK_PRIORITY_CRITICAL (configMAX_PRIORITIES - 1)   // During OTA (maintain UI)
#define MESSAGING_TASK_PRIORITY_HIGH (configMAX_PRIORITIES - 2)  // Normal operation
#define MESSAGING_TASK_PRIORITY_LOW (3)                          // During OTA
#define OTA_TASK_PRIORITY_IDLE (2)                               // When no OTA active
#define OTA_TASK_PRIORITY_CRITICAL (configMAX_PRIORITIES - 1)    // During active OTA
#define NETWORK_TASK_PRIORITY_HIGH (configMAX_PRIORITIES - 3)    // Normal + OTA support
#define AUDIO_TASK_PRIORITY_NORMAL (4)                           // Improved from 1
#define AUDIO_TASK_PRIORITY_SUSPENDED (0)                        // During OTA

// Core assignment for ESP32-S3 (optimized for performance)
#define LVGL_TASK_CORE 0       // Core 0 for UI/LVGL (Arduino loop core)
#define NETWORK_TASK_CORE 1    // Core 1 for network operations
#define MESSAGING_TASK_CORE 1  // Moved to Core 0 to balance load
#define OTA_TASK_CORE 1        // Core 1 for OTA (network intensive)
#define AUDIO_TASK_CORE 0      // Core 0 for audio (needs UI updates)

// Adaptive update intervals
#define LVGL_UPDATE_INTERVAL 32                 //
#define NETWORK_UPDATE_INTERVAL_NORMAL 500      // Normal operation
#define NETWORK_UPDATE_INTERVAL_OTA 100         // During OTA for responsiveness
#define MESSAGING_UPDATE_INTERVAL_NORMAL 50     // Reduced CPU load from 20ms
#define MESSAGING_UPDATE_INTERVAL_HIGH_LOAD 20  // When high message volume
#define OTA_UPDATE_INTERVAL_IDLE 30000          // 30s when idle (massive improvement)
#define OTA_UPDATE_INTERVAL_CHECKING 5000       // 5s when checking for updates
#define OTA_UPDATE_INTERVAL_ACTIVE 50           // 50ms during active download
#define AUDIO_UPDATE_INTERVAL_NORMAL 1000       // Normal operation
#define AUDIO_UPDATE_INTERVAL_REDUCED 5000      // Low priority mode
// Task state management
typedef enum {
    TASK_STATE_NORMAL,
    TASK_STATE_OTA_ACTIVE,
    TASK_STATE_HIGH_LOAD,
    TASK_STATE_LOW_POWER,
    TASK_STATE_EMERGENCY
} TaskSystemState_t;

// OTA state management
typedef enum {
    OTA_STATE_IDLE,         // No OTA activity
    OTA_STATE_CHECKING,     // Checking for updates
    OTA_STATE_DOWNLOADING,  // Active download
    OTA_STATE_INSTALLING,   // Installing update
    OTA_STATE_COMPLETE,     // Update complete
    OTA_STATE_ERROR         // Error occurred
} OTAState_t;

// NETWORK-FREE ARCHITECTURE: Task configuration
enum TaskMode {
    TASK_MODE_NETWORK_FREE,  // Default: No network tasks, maximum UI/audio performance
    TASK_MODE_OTA_ACTIVE     // Temporary: Network tasks active during OTA only
};

// NETWORK-FREE ARCHITECTURE: Enhanced task system configuration
typedef struct {
    TaskSystemState_t currentState;
    OTAState_t otaState;
    TaskMode taskMode;  // NEW: Current task mode
    uint32_t messageLoad;
    uint32_t lastStateChange;
    bool emergencyMode;
    bool networkTasksActive;  // NEW: Track if network tasks are running
    uint32_t taskLoadMetrics[8];
} TaskSystemConfig_t;

// Task handles
extern TaskHandle_t lvglTaskHandle;
extern TaskHandle_t networkTaskHandle;
extern TaskHandle_t messagingTaskHandle;
extern TaskHandle_t otaTaskHandle;
extern TaskHandle_t audioTaskHandle;

// Synchronization objects
extern SemaphoreHandle_t lvglMutex;
extern QueueHandle_t otaProgressQueue;

// Dynamic task management
extern TaskSystemConfig_t taskSystemConfig;
extern SemaphoreHandle_t taskConfigMutex;

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

// Dynamic task management functions
void setTaskSystemState(TaskSystemState_t newState);
void setOTAState(OTAState_t newState);
void optimizeTaskPriorities(void);
void adjustTaskIntervals(void);
bool enterEmergencyMode(uint32_t durationMs);
void exitEmergencyMode(void);

// OTA-specific task management
void suspendForOTA(void);
void resumeFromOTA(void);
void configureForOTADownload(void);  // High-performance OTA mode
void configureForOTAInstall(void);   // Minimize interruptions during install

// LVGL thread safety functions
void lvglLock(void);
void lvglUnlock(void);
bool lvglTryLock(uint32_t timeoutMs = 100);

// OTA progress functions
void updateOTAProgress(uint8_t progress, bool inProgress, bool success,
                       const char *message);
bool getOTAProgress(OTAProgressData_t *data);

// Task monitoring and diagnostics
void printTaskStats(void);
void printTaskLoadAnalysis(void);
uint32_t getLvglTaskHighWaterMark(void);
uint32_t getNetworkTaskHighWaterMark(void);
uint32_t getTaskCPUUsage(TaskHandle_t taskHandle);  // If FreeRTOS stats available

// Message load monitoring
void reportMessageActivity(void);
uint32_t getMessageLoadPerSecond(void);

// NETWORK-FREE ARCHITECTURE: Task control functions
bool initNetworkFreeTasks(void);     // Initialize only UI/audio tasks
bool createOTATasks(void);           // Create network tasks for OTA
void destroyOTATasks(void);          // Remove network tasks after OTA
void switchToNetworkFreeMode(void);  // Boost UI/audio with freed resources
void switchToOTAMode(void);          // Prepare for OTA operation

// NETWORK-FREE ARCHITECTURE: Resource management
size_t getFreedNetworkMemory(void);     // Memory available from disabled network tasks
void reallocateNetworkResources(void);  // Boost UI/audio with network resources
void restoreNetworkResources(void);     // Restore resources for OTA

// NETWORK-FREE ARCHITECTURE: Task mode control
TaskMode getCurrentTaskMode(void);
bool setTaskMode(TaskMode mode);
bool isNetworkFree(void);

}  // namespace TaskManager
}  // namespace Application

#endif  // TASK_MANAGER_H
