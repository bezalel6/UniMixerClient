#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace Application {
namespace TaskManager {

// =============================================================================
// NETWORK-FREE ARCHITECTURE: Task Configuration Constants
// =============================================================================

// Core task stack sizes (optimized for network-free mode)
#define LVGL_TASK_STACK_SIZE (8 * 1024)   // Core 0: UI rendering and event handling
#define AUDIO_TASK_STACK_SIZE (6 * 1024)  // Core 0: Audio management (increased from 4KB)

// LVGL performance monitoring thresholds
#define LVGL_DURATION_CRITICAL_STARTUP 300  // Critical threshold during startup (ms)
#define LVGL_DURATION_WARNING_STARTUP 200   // Warning threshold during startup (ms)
#define LVGL_DURATION_INFO_STARTUP 100      // Info threshold during startup (ms)
#define LVGL_DURATION_CRITICAL_NORMAL 100   // Critical threshold during normal operation (ms)
#define LVGL_DURATION_WARNING_NORMAL 50     // Warning threshold during normal operation (ms)

// Network-free priority management (simplified)
#define LVGL_TASK_PRIORITY_HIGH (configMAX_PRIORITIES - 1)      // Maximum UI responsiveness
#define LVGL_TASK_PRIORITY_CRITICAL (configMAX_PRIORITIES - 1)  // During OTA (maintain UI)
#define AUDIO_TASK_PRIORITY_NORMAL (configMAX_PRIORITIES - 2)   // High priority for audio
#define AUDIO_TASK_PRIORITY_SUSPENDED (0)                       // During OTA

// Core assignment for ESP32-S3 (network-free architecture)
#define LVGL_TASK_CORE 0   // Core 0: UI/LVGL + Audio (maximum performance)
#define AUDIO_TASK_CORE 0  // Core 0: Audio management with UI integration
// Note: Core 1 is DEDICATED to InterruptMessagingEngine (not managed by TaskManager)

// Update intervals (network-free optimized)
#define LVGL_UPDATE_INTERVAL 32             // 32ms for smooth 30+ FPS
#define AUDIO_UPDATE_INTERVAL_NORMAL 1000   // Normal audio status updates
#define AUDIO_UPDATE_INTERVAL_REDUCED 5000  // Low priority mode

// =============================================================================
// NETWORK-FREE ARCHITECTURE: Task State Management
// =============================================================================

// Simplified task states for network-free mode
typedef enum {
    TASK_STATE_NORMAL,     // Normal UI/Audio operation
    TASK_STATE_HIGH_LOAD,  // High message load from InterruptMessagingEngine
    TASK_STATE_LOW_POWER,  // Power saving mode
    TASK_STATE_OTA_ACTIVE  // OTA mode (minimal tasks)
} TaskSystemState_t;

// OTA state management (only relevant during boot mode OTA)
typedef enum {
    OTA_STATE_IDLE,         // No OTA activity
    OTA_STATE_CHECKING,     // Checking for updates
    OTA_STATE_DOWNLOADING,  // Active download
    OTA_STATE_INSTALLING,   // Installing update
    OTA_STATE_COMPLETE,     // Update complete
    OTA_STATE_ERROR         // Error occurred
} OTAState_t;

// Task configuration for network-free architecture
typedef struct {
    TaskSystemState_t currentState;
    OTAState_t otaState;
    uint32_t messageLoad;  // Messages/second from InterruptMessagingEngine
    uint32_t lastStateChange;
    bool emergencyMode;
    uint32_t taskLoadMetrics[4];  // Simplified for LVGL, Audio, and messaging stats
} TaskSystemConfig_t;

// =============================================================================
// NETWORK-FREE ARCHITECTURE: Task Handles and Synchronization
// =============================================================================

// Task handles (network-free mode - only Core 0 tasks managed here)
extern TaskHandle_t lvglTaskHandle;
extern TaskHandle_t audioTaskHandle;
// Note: Core 1 InterruptMessagingEngine managed separately

// Synchronization objects
extern SemaphoreHandle_t lvglMutex;
extern QueueHandle_t otaProgressQueue;

// Task system configuration
extern TaskSystemConfig_t taskSystemConfig;
extern SemaphoreHandle_t taskConfigMutex;

// OTA Progress structure
typedef struct {
    uint8_t progress;  // 0-100
    bool inProgress;   // true if OTA is active
    bool success;      // true if completed successfully
    char message[64];  // Status message
} OTAProgressData_t;

// =============================================================================
// TASK FUNCTION DECLARATIONS (Network-Free Mode)
// =============================================================================

// Core 0 tasks only
void lvglTask(void *parameter);
void audioTask(void *parameter);
// Note: messagingTask runs on Core 1 via InterruptMessagingEngine

// =============================================================================
// TASK MANAGEMENT FUNCTIONS
// =============================================================================

// Core lifecycle
bool init(void);
void deinit(void);
void suspend(void);
void resume(void);

// Dynamic task management
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

// LVGL thread safety
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
uint32_t getAudioTaskHighWaterMark(void);

// =============================================================================
// MESSAGING ENGINE INTEGRATION
// =============================================================================

// Message load monitoring (integrates with InterruptMessagingEngine)
void reportMessageActivity(void);        // Called by InterruptMessagingEngine
uint32_t getMessageLoadPerSecond(void);  // Get current message throughput
void reportCore1MessagingStats(uint32_t messagesReceived, uint32_t messagesSent,
                               uint32_t bufferOverruns);  // Stats from InterruptMessagingEngine

}  // namespace TaskManager
}  // namespace Application

#endif  // TASK_MANAGER_H
