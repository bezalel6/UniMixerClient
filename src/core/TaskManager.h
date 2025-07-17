#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace Application {
namespace TaskManager {

// =============================================================================
// SIMPLIFIED TASK CONFIGURATION
// =============================================================================

// Core task stack sizes
#define LVGL_TASK_STACK_SIZE (8 * 1024)    // 8KB for UI
#define AUDIO_TASK_STACK_SIZE (16 * 1024)  // 16KB for Audio

// Task priorities
#define LVGL_TASK_PRIORITY (configMAX_PRIORITIES - 1)   // Highest priority
#define AUDIO_TASK_PRIORITY (configMAX_PRIORITIES - 2)  // High priority

// Core assignment (ESP32-S3 dual-core)
#define LVGL_TASK_CORE 0   // Core 0: UI/LVGL + Audio
#define AUDIO_TASK_CORE 0  // Core 0: Audio management
// Note: Core 1 dedicated to SimplifiedSerialEngine

// Performance monitoring thresholds
#define LVGL_DURATION_WARNING 50    // Warning threshold (ms)
#define LVGL_DURATION_CRITICAL 120  // Critical threshold (ms)

// INITIALIZATION ORDER:
// TaskManager::init() is called AFTER all dependencies are ready:
// 1. Hardware & Display systems (5-20%)
// 2. Logo Manager & Messaging (30-60%)
// 3. Audio Manager & Audio UI (70-80%)
// 4. TaskManager::init() creates and starts tasks (90%)
// This prevents race conditions and premature task execution.

// =============================================================================
// TASK HANDLES AND SYNCHRONIZATION
// =============================================================================

extern TaskHandle_t lvglTaskHandle;
extern TaskHandle_t audioTaskHandle;
extern SemaphoreHandle_t lvglMutex;

// =============================================================================
// CORE FUNCTIONS
// =============================================================================

// Lifecycle
bool init(void);
void deinit(void);
void suspend(void);
void resume(void);

// LVGL thread safety
void lvglLock(void);
void lvglUnlock(void);
bool lvglTryLock(uint32_t timeoutMs = 100);

// Basic monitoring
void printTaskStats(void);
uint32_t getLvglTaskHighWaterMark(void);
uint32_t getAudioTaskHighWaterMark(void);

// Simple messaging integration
void reportMessageActivity(void);
uint32_t getMessageLoadPerSecond(void);

// =============================================================================
// TASK FUNCTION DECLARATIONS
// =============================================================================

void lvglTask(void *parameter);
void audioTask(void *parameter);

}  // namespace TaskManager
}  // namespace Application

#endif  // TASK_MANAGER_H
