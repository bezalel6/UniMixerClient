#include "TaskManager.h"
#include "../application/audio/AudioUI.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../display/DisplayManager.h"
#include "../logo/SimpleLogoManager.h"
#include "../messaging/SimplifiedSerialEngine.h"
#include <esp_log.h>
#include <ui/ui.h>

static const char *TAG = "TaskManager";

namespace Application {
namespace TaskManager {

// =============================================================================
// SIMPLIFIED TASK MANAGEMENT
// =============================================================================

// Task handles
TaskHandle_t lvglTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;

// Synchronization
SemaphoreHandle_t lvglMutex = NULL;

// Simple state tracking
static bool tasksRunning = false;
static uint32_t messageCount = 0;
static uint32_t lastMessageCountReset = 0;

// =============================================================================
// LIFECYCLE FUNCTIONS
// =============================================================================

bool init(void) {
    ESP_LOGI(TAG, "Initializing simplified TaskManager");

    tasksRunning = true;

    // Create LVGL mutex
    lvglMutex = xSemaphoreCreateRecursiveMutex();
    if (lvglMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return false;
    }

    // Initialize LVGL Message Handler
    if (!LVGLMessageHandler::init()) {
        ESP_LOGE(TAG, "Failed to initialize LVGL Message Handler");
        return false;
    }

    // Create LVGL task
    BaseType_t lvglResult = xTaskCreatePinnedToCore(
        lvglTask, "LVGL_Task", LVGL_TASK_STACK_SIZE, NULL,
        LVGL_TASK_PRIORITY, &lvglTaskHandle, LVGL_TASK_CORE);

    if (lvglResult != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return false;
    }

    // Create Audio task
    BaseType_t audioResult = xTaskCreatePinnedToCore(
        audioTask, "Audio_Task", AUDIO_TASK_STACK_SIZE, NULL,
        AUDIO_TASK_PRIORITY, &audioTaskHandle, AUDIO_TASK_CORE);

    if (audioResult != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Audio task");
        return false;
    }

    ESP_LOGI(TAG, "TaskManager initialized successfully");
    ESP_LOGI(TAG, "Core 0: LVGL + Audio tasks");
    ESP_LOGI(TAG, "Core 1: SimplifiedSerialEngine");

    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing TaskManager");

    tasksRunning = false;
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for tasks to finish

    // Delete tasks
    if (lvglTaskHandle) {
        vTaskDelete(lvglTaskHandle);
        lvglTaskHandle = NULL;
    }

    if (audioTaskHandle) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = NULL;
    }

    // Clean up mutex
    if (lvglMutex) {
        vSemaphoreDelete(lvglMutex);
        lvglMutex = NULL;
    }

    ESP_LOGI(TAG, "TaskManager deinitialization complete");
}

void suspend(void) {
    if (tasksRunning) {
        if (lvglTaskHandle && eTaskGetState(lvglTaskHandle) != eSuspended) {
            vTaskSuspend(lvglTaskHandle);
        }
        if (audioTaskHandle && eTaskGetState(audioTaskHandle) != eSuspended) {
            vTaskSuspend(audioTaskHandle);
        }
    }
}

void resume(void) {
    if (tasksRunning) {
        if (lvglTaskHandle && eTaskGetState(lvglTaskHandle) == eSuspended) {
            vTaskResume(lvglTaskHandle);
        }
        if (audioTaskHandle && eTaskGetState(audioTaskHandle) == eSuspended) {
            vTaskResume(audioTaskHandle);
        }
    }
}

// =============================================================================
// LVGL THREAD SAFETY
// =============================================================================

void lvglLock(void) {
    if (lvglMutex) {
        xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY);
    }
}

void lvglUnlock(void) {
    if (lvglMutex) {
        xSemaphoreGiveRecursive(lvglMutex);
    }
}

bool lvglTryLock(uint32_t timeoutMs) {
    if (lvglMutex) {
        return xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }
    return false;
}

// =============================================================================
// BASIC MONITORING
// =============================================================================

void printTaskStats(void) {
    ESP_LOGI(TAG, "=== Task Statistics ===");
    ESP_LOGI(TAG, "LVGL Task: Core %d, Priority %d, Stack: %d bytes",
             LVGL_TASK_CORE, LVGL_TASK_PRIORITY, LVGL_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "Audio Task: Core %d, Priority %d, Stack: %d bytes",
             AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, AUDIO_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "Message Load: %u msg/s", getMessageLoadPerSecond());
    ESP_LOGI(TAG, "Free Heap: %d bytes", esp_get_free_heap_size());
}

uint32_t getLvglTaskHighWaterMark(void) {
    return lvglTaskHandle ? uxTaskGetStackHighWaterMark(lvglTaskHandle) : 0;
}

uint32_t getAudioTaskHighWaterMark(void) {
    return audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0;
}

// =============================================================================
// SIMPLE MESSAGING INTEGRATION
// =============================================================================

void reportMessageActivity(void) {
    messageCount++;
}

uint32_t getMessageLoadPerSecond(void) {
    uint32_t now = millis();
    if (now - lastMessageCountReset >= 1000) {
        uint32_t load = messageCount;
        messageCount = 0;
        lastMessageCountReset = now;
        return load;
    }
    return messageCount;  // Approximation for current second
}

// =============================================================================
// TASK IMPLEMENTATIONS
// =============================================================================

void lvglTask(void *parameter) {
    ESP_LOGI(TAG, "LVGL Task started on Core %d", xPortGetCoreID());

    // Brief stabilization delay
    vTaskDelay(pdMS_TO_TICKS(100));

    unsigned long lastDisplayUpdate = 0;
    unsigned long lastLedUpdate = 0;

    while (tasksRunning) {
        uint32_t lvgl_start = millis();

        // Update LVGL tick system
        Display::tickUpdate();

        // ✅ CRITICAL FIX: Always call lv_timer_handler() to process touch events
        // Touch events don't cause display invalidations, so we must process LVGL
        // timers regularly regardless of display update needs
        if (lvglTryLock(20)) {
            uint32_t processed = lv_timer_handler();
            lvglUnlock();

            // Check if display was updated during processing
            lv_disp_t *disp = lv_disp_get_default();
            if (disp && !disp->rendering_in_progress) {
                Display::onLvglRenderComplete();
            }
        }

        uint32_t lvgl_duration = millis() - lvgl_start;

        // Performance monitoring
        if (lvgl_duration > LVGL_DURATION_CRITICAL) {
            ESP_LOGW(TAG, "LVGL processing took %ums (>%ums)",
                     lvgl_duration, LVGL_DURATION_CRITICAL);
        } else if (lvgl_duration > LVGL_DURATION_WARNING) {
            ESP_LOGD(TAG, "LVGL processing: %ums", lvgl_duration);
        }

        // Periodic operations
        uint32_t currentTime = millis();
        if (currentTime - lastDisplayUpdate >= 2000) {
            Display::update();
            lastDisplayUpdate = currentTime;
        }

#ifdef BOARD_HAS_RGB_LED
        if (currentTime - lastLedUpdate >= 5000) {
            Hardware::Device::ledCycleColors();
            lastLedUpdate = currentTime;
        }
#endif

        // Fixed delay for consistent touch responsiveness
        // Touch processing requires regular LVGL timer handling
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "LVGL Task ended");
    vTaskDelete(NULL);
}

void audioTask(void *parameter) {
    ESP_LOGI(TAG, "Audio Task started on Core %d", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    unsigned long lastFpsUpdate = 0;

    // ✅ Wait for all dependencies to be initialized before starting work
    while (tasksRunning) {
        // Check if all required systems are initialized
        bool audioManagerReady = Application::Audio::AudioManager::getInstance().isInitialized();
        bool audioUIReady = Application::Audio::AudioUI::getInstance().isInitialized();

        if (audioManagerReady && audioUIReady) {
            ESP_LOGI(TAG, "Audio Task: All dependencies ready, starting normal operation");
            break;
        }

        // Log waiting status periodically
        static unsigned long lastWaitLog = 0;
        unsigned long currentTime = millis();
        if (currentTime - lastWaitLog >= 5000) {  // Every 5 seconds
            ESP_LOGI(TAG, "Audio Task waiting for dependencies: AudioManager=%s, AudioUI=%s",
                     audioManagerReady ? "ready" : "waiting",
                     audioUIReady ? "ready" : "waiting");
            lastWaitLog = currentTime;
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
    }

    while (tasksRunning) {
        unsigned long currentTime = millis();

        // Update FPS display periodically
        if (currentTime - lastFpsUpdate >= 10000) {
            float currentFps = Display::getFPS();
            LVGLMessageHandler::updateFpsDisplay(currentFps);
            lastFpsUpdate = currentTime;
        }

        // Commented out insanely spammy FULL UI updates
        // // Update audio UI (with mutex protection and additional safety check)
        // if (lvglTryLock(10)) {
        //     try {
        //         // ✅ Double-check initialization before calling
        //         if (Application::Audio::AudioUI::getInstance().isInitialized()) {
        //             Application::Audio::AudioUI::getInstance().refreshAllUI();
        //         }
        //     } catch (...) {
        //         ESP_LOGW(TAG, "Exception during audio UI update");
        //     }
        //     lvglUnlock();
        // }

        // Update logo manager (with safety check)
        SimpleLogoManager::getInstance().update();

        // Sleep for 1 second
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Audio Task ended");
    vTaskDelete(NULL);
}

}  // namespace TaskManager
}  // namespace Application
