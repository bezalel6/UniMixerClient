#include "TaskManager.h"
#include "MessagingConfig.h"
#include "OTAConfig.h"
#include "DebugUtils.h"
#include "../display/DisplayManager.h"
#include "UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/MessageAPI.h"
#include "../messaging/transport/SerialEngine.h"
#include "../messaging/protocol/MessageData.h"
#include "AppController.h"
#include "../application/audio/AudioUI.h"
#include "../logo/LogoSupplier.h"
#include "../application/ui/LVGLMessageHandler.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <ui/ui.h>
#include <driver/gpio.h>
#include <functional>

static const char *TAG = "TaskManager";

// =============================================================================
// TASK MANAGEMENT MACROS - Eliminate repetitiveness
// =============================================================================

#define TASK_CREATE_PINNED(task_func, task_name, stack_size, priority, handle, core)                   \
    do {                                                                                               \
        ESP_LOGI(TAG, "[INIT] Creating %s on Core %d with priority %d...", task_name, core, priority); \
        BaseType_t result = xTaskCreatePinnedToCore(                                                   \
            task_func, task_name, stack_size, NULL, priority, &handle, core);                          \
        if (result != pdPASS) {                                                                        \
            ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create %s - result: %d", task_name, result);     \
            return false;                                                                              \
        }                                                                                              \
        ESP_LOGI(TAG, "[INIT] %s created successfully", task_name);                                    \
    } while (0)

#define TASK_DELETE_SAFE(handle, task_name)                   \
    do {                                                      \
        if (handle) {                                         \
            ESP_LOGI(TAG, "[DEINIT] Deleting %s", task_name); \
            vTaskDelete(handle);                              \
            handle = NULL;                                    \
        }                                                     \
    } while (0)

#define TASK_SET_PRIORITY_SAFE(handle, priority, task_name)                        \
    do {                                                                           \
        if (handle) {                                                              \
            vTaskPrioritySet(handle, priority);                                    \
            ESP_LOGD(TAG, "[DYNAMIC] Set %s priority to %d", task_name, priority); \
        }                                                                          \
    } while (0)

#define TASK_SUSPEND_SAFE(handle, task_name)                    \
    do {                                                        \
        if (handle && eTaskGetState(handle) != eSuspended) {    \
            ESP_LOGI(TAG, "[OTA] Suspending %s...", task_name); \
            vTaskSuspend(handle);                               \
        }                                                       \
    } while (0)

#define TASK_RESUME_SAFE(handle, task_name)                   \
    do {                                                      \
        if (handle && eTaskGetState(handle) == eSuspended) {  \
            ESP_LOGI(TAG, "[OTA] Resuming %s...", task_name); \
            vTaskResume(handle);                              \
        }                                                     \
    } while (0)

#define MUTEX_CREATE_SAFE(mutex, mutex_name)                                   \
    do {                                                                       \
        ESP_LOGI(TAG, "[INIT] Creating %s...", mutex_name);                    \
        mutex = xSemaphoreCreateRecursiveMutex();                              \
        if (mutex == NULL) {                                                   \
            ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create %s", mutex_name); \
            return false;                                                      \
        }                                                                      \
        ESP_LOGI(TAG, "[INIT] %s created successfully", mutex_name);           \
    } while (0)

#define MUTEX_DELETE_SAFE(mutex, mutex_name)                   \
    do {                                                       \
        if (mutex) {                                           \
            ESP_LOGI(TAG, "[DEINIT] Deleting %s", mutex_name); \
            vSemaphoreDelete(mutex);                           \
            mutex = NULL;                                      \
        }                                                      \
    } while (0)

#define QUEUE_CREATE_SAFE(queue, size, item_size, queue_name)                  \
    do {                                                                       \
        ESP_LOGI(TAG, "[INIT] Creating %s...", queue_name);                    \
        queue = xQueueCreate(size, item_size);                                 \
        if (queue == NULL) {                                                   \
            ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create %s", queue_name); \
            return false;                                                      \
        }                                                                      \
        ESP_LOGI(TAG, "[INIT] %s created successfully", queue_name);           \
    } while (0)

#define QUEUE_DELETE_SAFE(queue, queue_name)                   \
    do {                                                       \
        if (queue) {                                           \
            ESP_LOGI(TAG, "[DEINIT] Deleting %s", queue_name); \
            vQueueDelete(queue);                               \
            queue = NULL;                                      \
        }                                                      \
    } while (0)

#define LOG_TASK_CONFIG(task_name, core, priority, stack_size)        \
    ESP_LOGI(TAG, "[STATS] %s: Core %d, Priority %d, Stack %d bytes", \
             task_name, core, priority, stack_size)

#define LOG_TASK_CONFIG_WITH_INTERVAL(task_name, core, priority, stack_size, interval) \
    ESP_LOGI(TAG, "[STATS] %s: Core %d, Priority %d, Stack %d bytes (Interval: %ums)", \
             task_name, core, priority, stack_size, interval)

namespace Application {
namespace TaskManager {

// =============================================================================
// NETWORK-FREE ARCHITECTURE: Task System State
// =============================================================================

// Task handles (Core 0 only - Core 1 managed by InterruptMessagingEngine)
TaskHandle_t lvglTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;

// Synchronization objects
SemaphoreHandle_t lvglMutex = NULL;
QueueHandle_t otaProgressQueue = NULL;

// Task system configuration (simplified for network-free mode)
TaskSystemConfig_t taskSystemConfig = {
    .currentState = TASK_STATE_NORMAL,
    .otaState = OTA_STATE_IDLE,
    .messageLoad = 0,
    .lastStateChange = 0,
    .emergencyMode = false,
    .taskLoadMetrics = {0}};
SemaphoreHandle_t taskConfigMutex = NULL;

// Task state variables
static bool tasksRunning = false;

// OTA progress tracking
static OTAProgressData_t currentOTAProgress = {0, false, false, "Ready"};

// Message load tracking (simplified for network-free mode)
static uint32_t messageCount = 0;
static uint32_t lastMessageCountReset = 0;
static uint32_t currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;

// =============================================================================
// INTERNAL SHARED INITIALIZATION FUNCTIONS
// =============================================================================

bool initializeSharedComponents(bool networkFreeMode) {
    ESP_LOGI(TAG, "[SHARED-INIT] Initializing shared components for network-free mode");

    tasksRunning = true;

    // Create synchronization objects
    MUTEX_CREATE_SAFE(taskConfigMutex, "task configuration mutex");
    MUTEX_CREATE_SAFE(lvglMutex, "LVGL mutex");

    // Initialize task system configuration
    taskSystemConfig.currentState = TASK_STATE_NORMAL;
    taskSystemConfig.otaState = OTA_STATE_IDLE;
    // taskSystemConfig.taskMode = TASK_MODE_NETWORK_FREE;  // Always network-free
    taskSystemConfig.messageLoad = 0;
    taskSystemConfig.lastStateChange = millis();
    taskSystemConfig.emergencyMode = false;
    // taskSystemConfig.networkTasksActive = false;  // Never active in normal mode

    // Initialize LVGL Message Handler - required for UI
    ESP_LOGI(TAG, "[SHARED-INIT] Initializing LVGL Message Handler...");
    if (!LVGLMessageHandler::init()) {
        ESP_LOGE(TAG, "[SHARED-INIT] CRITICAL: Failed to initialize LVGL Message Handler");
        return false;
    }
    ESP_LOGI(TAG, "[SHARED-INIT] LVGL Message Handler initialized successfully");

    ESP_LOGI(TAG, "[SHARED-INIT] Shared components initialization completed successfully");
    return true;
}

void printInitializationSummary(bool networkFreeMode) {
    ESP_LOGI(TAG, "[INIT] SUCCESS: Network-free task system initialized");
    ESP_LOGI(TAG, "[INIT] Core 0: LVGL + Audio (maximum performance)");
    ESP_LOGI(TAG, "[INIT] Core 1: InterruptMessagingEngine (dedicated messaging)");
    ESP_LOGI(TAG, "[INIT] Network tasks: Only in OTA boot mode");
    ESP_LOGI(TAG, "[INIT] Network-Free Task Manager initialization completed successfully");
}

// Update createEssentialTasks to only create LVGL and Audio
bool createEssentialTasks(bool networkFreeMode) {
    ESP_LOGI(TAG, "[ESSENTIAL-TASKS] Creating essential tasks (network-free mode)");

    // Create only UI and Audio tasks - Core 1 messaging handled separately
    TASK_CREATE_PINNED(lvglTask, "LVGL_Task", LVGL_TASK_STACK_SIZE, LVGL_TASK_PRIORITY_HIGH, lvglTaskHandle, LVGL_TASK_CORE);

    // Audio task gets maximum resources in network-free mode
    UBaseType_t audioPriority = AUDIO_TASK_PRIORITY_NORMAL + 1;  // Boost priority
    size_t audioStackSize = AUDIO_TASK_STACK_SIZE + 2048;        // Extra stack

    TASK_CREATE_PINNED(audioTask, "Audio_Task", audioStackSize, audioPriority, audioTaskHandle, AUDIO_TASK_CORE);

    ESP_LOGI(TAG, "[ESSENTIAL-TASKS] Essential tasks created successfully");
    ESP_LOGI(TAG, "[NETWORK-FREE] Core 1 dedicated to InterruptMessagingEngine");
    return true;
}

// Update init() for network-free mode
bool init(void) {
    ESP_LOGI(TAG, "[INIT] Starting Network-Free Task Manager for ESP32-S3 dual-core");

    // Initialize shared components for network-free mode
    if (!initializeSharedComponents(true)) {  // true = network-free
        return false;
    }

    // Create only essential tasks (LVGL + Audio)
    if (!createEssentialTasks(true)) {  // true = network-free
        return false;
    }

    // Create OTA progress queue
    otaProgressQueue = xQueueCreate(1, sizeof(OTAProgressData_t));
    if (!otaProgressQueue) {
        ESP_LOGE(TAG, "Failed to create OTA progress queue");
        return false;
    }

    printInitializationSummary(true);  // true = network-free mode

    return true;
}

// Update deinit() to only handle essential tasks
void deinit(void) {
    ESP_LOGI(TAG, "[DEINIT] Starting Network-Free Task Manager deinitialization");

    tasksRunning = false;

    // Wait a bit for tasks to finish their current operations
    vTaskDelay(pdMS_TO_TICKS(100));

    // Delete only the tasks we created
    TASK_DELETE_SAFE(lvglTaskHandle, "LVGL task");
    TASK_DELETE_SAFE(audioTaskHandle, "Audio task");

    // Clean up synchronization objects
    MUTEX_DELETE_SAFE(lvglMutex, "LVGL mutex");
    MUTEX_DELETE_SAFE(taskConfigMutex, "task configuration mutex");
    QUEUE_DELETE_SAFE(otaProgressQueue, "OTA progress queue");

    ESP_LOGI(TAG, "[DEINIT] Network-Free Task Manager deinitialization completed");
}

void suspend(void) {
    if (tasksRunning) {
        TASK_SUSPEND_SAFE(lvglTaskHandle, "LVGL task");
        TASK_SUSPEND_SAFE(audioTaskHandle, "Audio task");
    }
}

void resume(void) {
    if (tasksRunning) {
        TASK_RESUME_SAFE(lvglTaskHandle, "LVGL task");
        TASK_RESUME_SAFE(audioTaskHandle, "Audio task");
    }
}

void suspendForOTA(void) {
    if (tasksRunning) {
        ESP_LOGI(TAG, "[OTA] Suspending non-essential tasks for OTA update...");
        // Keep network, OTA, and LVGL tasks running for OTA process and UI feedback
        TASK_SUSPEND_SAFE(audioTaskHandle, "Audio_Task");
        ESP_LOGI(TAG, "[OTA] Finished suspending tasks for OTA.");
    } else {
        ESP_LOGW(TAG, "[OTA] Cannot suspend tasks - tasks not running");
    }
}

void resumeFromOTA(void) {
    if (tasksRunning) {
        ESP_LOGI(TAG, "[OTA] Resuming tasks after OTA update...");
        TASK_RESUME_SAFE(audioTaskHandle, "Audio_Task");
        ESP_LOGI(TAG, "[OTA] Finished resuming tasks after OTA.");
    } else {
        ESP_LOGW(TAG, "[OTA] Cannot resume tasks - tasks not running");
    }
}

void lvglLock(void) {
    if (lvglMutex) {
        if (xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "[MUTEX] CRITICAL: Failed to acquire LVGL mutex");
        }
    } else {
        ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL");
    }
}

void lvglUnlock(void) {
    if (lvglMutex) {
        if (xSemaphoreGiveRecursive(lvglMutex) != pdTRUE) {
            ESP_LOGE(TAG, "[MUTEX] CRITICAL: Failed to release LVGL mutex");
        }
    } else {
        ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL during unlock");
    }
}

bool lvglTryLock(uint32_t timeoutMs) {
    if (lvglMutex) {
        bool result = xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) ==
                      pdTRUE;
        if (!result) {
            ESP_LOGE(TAG, "[MUTEX] WARNING: Failed to acquire LVGL mutex within %d ms", timeoutMs);
        }
        return result;
    }
    ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL during tryLock");
    return false;
}

void updateOTAProgress(uint8_t progress, bool inProgress, bool success,
                       const char *message) {
    if (!otaProgressQueue) {
        ESP_LOGE(TAG, "[OTA] CRITICAL: OTA progress queue is NULL");
        return;
    }

    OTAProgressData_t data;
    data.progress = progress;
    data.inProgress = inProgress;
    data.success = success;
    strncpy(data.message, message ? message : "", sizeof(data.message) - 1);
    data.message[sizeof(data.message) - 1] = '\0';

    // Update current progress (non-blocking)
    currentOTAProgress = data;

    // Send to queue (non-blocking)
    if (xQueueOverwrite(otaProgressQueue, &data) != pdTRUE) {
        ESP_LOGE(TAG, "[OTA] WARNING: Failed to update OTA progress queue");
    }
}

bool getOTAProgress(OTAProgressData_t *data) {
    if (data == NULL) {
        ESP_LOGE(TAG, "[OTA] CRITICAL: NULL data pointer passed to getOTAProgress");
        return false;
    }

    if (!otaProgressQueue) {
        ESP_LOGE(TAG, "[OTA] CRITICAL: OTA progress queue is NULL");
        *data = currentOTAProgress;
        return true;
    }

    // Try to get latest from queue
    if (xQueuePeek(otaProgressQueue, data, 0) == pdTRUE) {
        return true;
    }

    // Fall back to current progress
    *data = currentOTAProgress;
    return true;
}

// LVGL Task - Core 0, Highest Priority
void lvglTask(void *parameter) {
    ESP_LOGI(TAG, "[LVGL_TASK] LVGL Task started on Core %d", xPortGetCoreID());

    // OPTIMIZED: Reduced stabilization time from 350ms to 100ms
    ESP_LOGI(TAG, "[LVGL_TASK] Waiting for display hardware stabilization...");
    vTaskDelay(pdMS_TO_TICKS(100));

    // LVGL SD filesystem is now managed by SDManager and will be initialized automatically
    ESP_LOGI(TAG, "[LVGL_TASK] LVGL SD filesystem will be managed by SDManager");

    ESP_LOGI(TAG, "[LVGL_TASK] Starting event-driven LVGL operations loop");

    static unsigned long lastDisplayUpdate = 0;
    static unsigned long lastLedUpdate = 0;
    static uint32_t lastLvglActivity = 0;
    static uint32_t lastForceUpdate = 0;
    static bool startupPhase = true;
    static uint32_t startupStartTime = millis();

    while (tasksRunning) {
        // Update LVGL tick system first (critical for animations)
        Display::tickUpdate();

        uint32_t currentTime = millis();
        uint32_t lvgl_start = currentTime;

        // Check if we're still in startup phase (first 10 seconds)
        if (startupPhase && (currentTime - startupStartTime > 10000)) {
            startupPhase = false;
            ESP_LOGI(TAG, "[LVGL_TASK] Exiting startup phase - switching to normal operation");
        }

        // Check if LVGL actually has pending work
        lv_disp_t *disp = lv_disp_get_default();
        bool hasMessages = false;
        bool hasInvalidations = false;
        bool forceUpdate = false;
        bool shouldProcessTimers = false;

        if (disp) {
            // Check for pending invalidations/redraws (most reliable indicator)
            hasInvalidations = disp->inv_p != 0;

            // Check for recent message activity (shorter window for responsiveness)
            hasMessages = (currentTime - lastLvglActivity < (startupPhase ? 100 : 25));

            // Process timers more conservatively - more frequent during startup
            shouldProcessTimers = (currentTime - lastLvglActivity > (startupPhase ? 5 : 20));

            // Force periodic update to ensure system stays responsive
            forceUpdate = (currentTime - lastForceUpdate > (startupPhase ? 100 : 500));
        }

        // Determine if we need to process LVGL - more aggressive during startup
        bool lvglNeedsUpdate = hasInvalidations || hasMessages || shouldProcessTimers || forceUpdate;

        if (!lvglNeedsUpdate) {
            // No immediate work - use different delays for startup vs normal
            ESP_LOGV(TAG, "[LVGL_TASK] No UI work pending - sleeping");

            // OPTIMIZED: Non-critical operations when UI is idle
            uint32_t displayUpdateInterval = startupPhase ? 5000 : 2000;  // Less frequent during startup
            if (currentTime - lastDisplayUpdate >= displayUpdateInterval) {
                Display::update();
                lastDisplayUpdate = currentTime;
            }

#ifdef BOARD_HAS_RGB_LED
            uint32_t ledUpdateInterval = startupPhase ? 10000 : 3000;  // Much less frequent during startup
            if (currentTime - lastLedUpdate >= ledUpdateInterval) {
                Hardware::Device::ledCycleColors();
                lastLedUpdate = currentTime;
            }
#endif

            // Shorter sleep during startup to handle heavy UI initialization
            uint32_t idleSleep = startupPhase ? 20 : 100;  // 20ms during startup, 100ms normal
            vTaskDelay(pdMS_TO_TICKS(idleSleep));
            continue;
        }

        // Process LVGL work
        bool workDone = false;

        // Acquire mutex with different timeout for startup vs normal
        uint32_t mutexTimeout = startupPhase ? 50 : 15;  // Longer timeout during startup
        if (lvglTryLock(mutexTimeout)) {
            uint32_t processing_start = millis();
            uint32_t processed = 0;

            // Process LVGL timers and rendering
            if (hasInvalidations || shouldProcessTimers || forceUpdate) {
                // During startup, use chunked processing to prevent long blocks
                if (startupPhase) {
                    // Process in smaller chunks during startup
                    uint32_t chunkStart = millis();
                    processed = lv_timer_handler();
                    uint32_t chunkDuration = millis() - chunkStart;

                    // If processing took too long, yield briefly to other tasks
                    if (chunkDuration > 30) {
                        lvglUnlock();
                        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms yield
                        if (!lvglTryLock(mutexTimeout)) {
                            ESP_LOGD(TAG, "[LVGL_TASK] Startup chunked processing - mutex timeout on re-acquire");
                            goto skip_processing;
                        }
                    }
                } else {
                    processed = lv_timer_handler();
                }

                lastLvglActivity = millis();
                workDone = true;

                if (forceUpdate) {
                    lastForceUpdate = currentTime;
                }
            }

            uint32_t single_call_duration = millis() - processing_start;

            // Track rendering completion
            if (disp && !disp->rendering_in_progress) {
                Display::onLvglRenderComplete();
            }

            lvglUnlock();

            ESP_LOGV(TAG, "[LVGL_TASK] Processed %u timers in %ums", processed, single_call_duration);
        } else {
            ESP_LOGD(TAG, "[LVGL_TASK] Skipped update - mutex timeout");
        }

    skip_processing:

        uint32_t lvgl_duration = millis() - lvgl_start;

        // Performance monitoring with different thresholds for startup vs normal operation
        if (startupPhase) {
            // More lenient thresholds during startup - UI initialization is expected to be heavy
            if (lvgl_duration > LVGL_DURATION_CRITICAL_STARTUP) {
                ESP_LOGE(TAG, "[LVGL_TASK] STARTUP: CRITICAL processing time %ums (>%ums)", lvgl_duration, LVGL_DURATION_CRITICAL_STARTUP);
            } else if (lvgl_duration > LVGL_DURATION_WARNING_STARTUP) {
                ESP_LOGW(TAG, "[LVGL_TASK] STARTUP: Long processing time %ums (>%ums)", lvgl_duration, LVGL_DURATION_WARNING_STARTUP);
            } else if (lvgl_duration > LVGL_DURATION_INFO_STARTUP) {
                ESP_LOGI(TAG, "[LVGL_TASK] STARTUP: Heavy processing %ums (expected during UI init)", lvgl_duration);
            } else if (lvgl_duration > 0 && workDone) {
                ESP_LOGV(TAG, "[LVGL_TASK] STARTUP: Processing %ums", lvgl_duration);
            }
        } else {
            // Normal operation thresholds
            if (lvgl_duration > LVGL_DURATION_CRITICAL_NORMAL) {
                ESP_LOGE(TAG, "[LVGL_TASK] CRITICAL: LVGL processing took %ums (>%ums)", lvgl_duration, LVGL_DURATION_CRITICAL_NORMAL);
            } else if (lvgl_duration > LVGL_DURATION_WARNING_NORMAL) {
                ESP_LOGW(TAG, "[LVGL_TASK] LVGL processing took %ums (>%ums)", lvgl_duration, LVGL_DURATION_WARNING_NORMAL);
            } else if (lvgl_duration > 0 && workDone) {
                ESP_LOGV(TAG, "[LVGL_TASK] LVGL processing: %ums", lvgl_duration);
            }
        }

        // More frequent non-critical operations when UI is active
        if (workDone) {
            if (currentTime - lastDisplayUpdate >= 1000) {  // Every 1s when active
                Display::update();
                lastDisplayUpdate = currentTime;
            }

#ifdef BOARD_HAS_RGB_LED
            if (currentTime - lastLedUpdate >= 2000) {  // Every 2s when active
                Hardware::Device::ledCycleColors();
                lastLedUpdate = currentTime;
            }
#endif
        }

        // Dynamic delay based on work done and system state
        uint32_t delay_ms;
        if (startupPhase) {
            // Startup phase - more aggressive processing
            if (hasInvalidations) {
                delay_ms = 1;  // Immediate redraw
            } else if (shouldProcessTimers || forceUpdate) {
                delay_ms = 2;  // Very short delay for rapid UI setup
            } else if (hasMessages) {
                delay_ms = 5;  // Short delay for startup activity
            } else {
                delay_ms = 10;  // Moderate delay during startup
            }
        } else {
            // Normal operation
            if (hasInvalidations) {
                delay_ms = 1;  // Immediate redraw needed
            } else if (shouldProcessTimers) {
                delay_ms = 10;  // Timer processing - smooth updates
            } else if (hasMessages) {
                delay_ms = 25;  // Recent activity - moderate delay
            } else {
                delay_ms = 50;  // Minimal activity - longer delay
            }
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    ESP_LOGI(TAG, "[LVGL_TASK] LVGL Task ended");
    vTaskDelete(NULL);
}
void updateMessagingEngineIntegration();
// Audio Task - Core 0, Improved Priority with Adaptive Intervals
void audioTask(void *parameter) {
    ESP_LOGI(TAG, "[AUDIO_TASK] Audio Task started on Core %d with improved priority management", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    static unsigned long lastFpsUpdate = 0;

    // CRITICAL: State-based UI updates - only refresh when audio state changes
    static unsigned long lastAudioUpdate = 0;
    static unsigned long lastSuccessfulUpdate = 0;
    static int consecutiveFailures = 0;
    static bool emergencyMode = false;

    // Track audio state changes to avoid unnecessary UI updates
    static uint32_t lastAudioStateHash = 0;
    static unsigned long lastForceUpdate = 0;

    while (tasksRunning) {
        // Check if task is suspended due to emergency/OTA mode
        if (taskSystemConfig.currentState == TASK_STATE_OTA_ACTIVE) {
            // Minimal operations during critical states
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(5000));  // 5 second intervals
            continue;
        }

        // Update FPS display less frequently for monitoring
        unsigned long currentTime = millis();
        if (currentTime - lastFpsUpdate >= 10000) {  // Every 10 seconds (reduced frequency)
            // Get actual FPS from display manager
            float currentFps = Display::getFPS();
            LVGLMessageHandler::updateFpsDisplay(currentFps);
            lastFpsUpdate = currentTime;
        }

        // CRITICAL: Only update UI when audio state actually changes
        bool shouldUpdateUI = false;
        uint32_t currentAudioStateHash = 0;

        // Calculate a simple hash of the current audio state
        try {
            const auto &audioState = Application::Audio::AudioManager::getInstance().getState();
            std::hash<std::string> hasher;

            // Create a simple hash based on key audio state values
            currentAudioStateHash = 0;
            if (audioState.selectedDevice1) {
                currentAudioStateHash += audioState.selectedDevice1->volume;
                currentAudioStateHash += audioState.selectedDevice1->isMuted ? 1000 : 0;
                currentAudioStateHash += hasher(audioState.selectedDevice1->processName.c_str()) % 10000;
            }
            if (audioState.selectedDevice2) {
                currentAudioStateHash += audioState.selectedDevice2->volume * 13;  // Different multiplier
                currentAudioStateHash += audioState.selectedDevice2->isMuted ? 2000 : 0;
                currentAudioStateHash += hasher(audioState.selectedDevice2->processName.c_str()) % 20000;
            }

            // Check if state has changed
            shouldUpdateUI = (currentAudioStateHash != lastAudioStateHash);

        } catch (...) {
            ESP_LOGW(TAG, "[AUDIO_TASK] Exception while checking audio state");
            shouldUpdateUI = false;
        }

        // Force periodic updates even if state hasn't changed (every 30 seconds)
        if (currentTime - lastForceUpdate > 30000) {
            shouldUpdateUI = true;
            lastForceUpdate = currentTime;
            ESP_LOGD(TAG, "[AUDIO_TASK] Force UI update after 30s");
        }

        // Emergency mode: Force update if we haven't updated in too long
        if (currentTime - lastSuccessfulUpdate > 60000) {  // 60s
            shouldUpdateUI = true;
            if (!emergencyMode) {
                ESP_LOGW(TAG, "[AUDIO_TASK] Entering emergency mode (60s without update)");
                emergencyMode = true;
                consecutiveFailures = 0;
            }
        }

        if (shouldUpdateUI) {
            // Reset watchdog before potentially long-running UI operation
#ifdef CONFIG_ESP_TASK_WDT_EN
            esp_task_wdt_reset();
#endif

            // CRITICAL: Ultra-short timeout to prevent blocking LVGL
            uint32_t timeout = emergencyMode ? 2 : (consecutiveFailures > 3) ? 5
                                                                             : 10;

            // Use LVGL mutex protection for thread-safe UI updates
            if (lvglTryLock(timeout)) {
                // CRITICAL: Only do full refresh when state has changed
                try {
                    Application::Audio::AudioUI::getInstance().refreshAllUI();
                    consecutiveFailures = 0;
                    lastSuccessfulUpdate = currentTime;
                    lastAudioStateHash = currentAudioStateHash;  // Update our state tracking
                    emergencyMode = false;
                    ESP_LOGV(TAG, "[AUDIO_TASK] UI updated due to state change (hash: %u)", currentAudioStateHash);
                } catch (...) {
                    ESP_LOGD(TAG, "[AUDIO_TASK] Exception during UI update");
                    consecutiveFailures++;
                }
                lvglUnlock();
            } else {
                consecutiveFailures++;
                ESP_LOGV(TAG, "[AUDIO_TASK] Skipped UI update (mutex timeout: %dms, failures: %d)",
                         timeout, consecutiveFailures);
            }

            // Exit emergency mode on successful update
            if (emergencyMode && (currentTime - lastSuccessfulUpdate < 10000)) {
                ESP_LOGI(TAG, "[AUDIO_TASK] Exiting emergency mode (successful update)");
                emergencyMode = false;
                consecutiveFailures = 0;
            }

            // Reset watchdog after UI operation
#ifdef CONFIG_ESP_TASK_WDT_EN
            esp_task_wdt_reset();
#endif
        }

        // OPTIMIZED: Determine sleep interval based on system state and performance
        uint32_t sleepInterval;
        if (emergencyMode) {
            sleepInterval = 2000;  // 2s during emergency
        } else if (consecutiveFailures > 3) {
            sleepInterval = 1500;  // 1.5s when struggling
        } else {
            sleepInterval = currentAudioInterval;  // Normal adaptive interval
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(sleepInterval));

        // OPTIMIZED: Heartbeat logging reduced frequency + InterruptMessagingEngine integration
        static int heartbeat = 0;
        if (++heartbeat % 50 == 0) {  // Reduced from every 10 to every 50
            ESP_LOGD(TAG, "[AUDIO_TASK] Heartbeat: %d (failures: %d, emergency: %s)",
                     heartbeat, consecutiveFailures, emergencyMode ? "YES" : "NO");

            // Update InterruptMessagingEngine integration every ~50 cycles (every ~50 seconds)
            if (heartbeat % 200 == 0) {  // Every ~200 cycles = ~3-4 minutes
                updateMessagingEngineIntegration();
            }
        }
    }

    ESP_LOGI(TAG, "[AUDIO_TASK] Audio Task ended");
    vTaskDelete(NULL);
}

void printTaskStats(void) {
    ESP_LOGI(TAG, "[STATS] === Dynamic Task Configuration ===");
    ESP_LOGI(TAG, "[STATS] System State: %d, OTA State: %d, Emergency Mode: %s",
             taskSystemConfig.currentState, taskSystemConfig.otaState,
             taskSystemConfig.emergencyMode ? "YES" : "NO");
    ESP_LOGI(TAG, "[STATS] Message Load: %u msg/s, Last State Change: %u ms ago",
             taskSystemConfig.messageLoad, millis() - taskSystemConfig.lastStateChange);
    LOG_TASK_CONFIG("LVGL Task", LVGL_TASK_CORE, LVGL_TASK_PRIORITY_HIGH, LVGL_TASK_STACK_SIZE);
    LOG_TASK_CONFIG("Audio Task", AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY_NORMAL, AUDIO_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "[STATS] Core 1: InterruptMessagingEngine (dedicated)");
    ESP_LOGI(TAG, "[STATS] =========================");
}

void printTaskLoadAnalysis(void) {
    ESP_LOGI(TAG, "[ANALYSIS] === Task Performance Analysis ===");

    // Task priority analysis
    if (lvglTaskHandle) {
        UBaseType_t currentPriority = uxTaskPriorityGet(lvglTaskHandle);
        ESP_LOGI(TAG, "[ANALYSIS] LVGL Task: Current Priority %d, Stack HWM: %d bytes",
                 currentPriority, uxTaskGetStackHighWaterMark(lvglTaskHandle) * sizeof(StackType_t));
    }

    if (audioTaskHandle) {
        UBaseType_t currentPriority = uxTaskPriorityGet(audioTaskHandle);
        eTaskState taskState = eTaskGetState(audioTaskHandle);
        const char *stateStr = (taskState == eReady) ? "Ready" : (taskState == eRunning) ? "Running"
                                                             : (taskState == eBlocked)   ? "Blocked"
                                                             : (taskState == eSuspended) ? "Suspended"
                                                                                         : "Unknown";
        ESP_LOGI(TAG, "[ANALYSIS] Audio Task: Current Priority %d, State: %s, Stack HWM: %d bytes",
                 currentPriority, stateStr, uxTaskGetStackHighWaterMark(audioTaskHandle) * sizeof(StackType_t));
    }

    // System resource analysis
    ESP_LOGI(TAG, "[ANALYSIS] Free Heap: %d bytes, Free Stack (this task): %d bytes",
             esp_get_free_heap_size(), uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));

    // Performance optimization summary - Network-free mode eliminates OTA polling
    uint32_t otaEfficiencyGain = 100;  // 100% improvement - no OTA polling in normal mode
    ESP_LOGI(TAG, "[ANALYSIS] OTA Efficiency Gain: %u%% (OTA polling eliminated in network-free mode)",
             otaEfficiencyGain);

    ESP_LOGI(TAG, "[ANALYSIS] Core Load Distribution:");
    ESP_LOGI(TAG, "[ANALYSIS]   Core 0: LVGL (Priority %d), Audio (Priority %d-%d adaptive)",
             LVGL_TASK_PRIORITY_HIGH, AUDIO_TASK_PRIORITY_NORMAL);

    ESP_LOGI(TAG, "[ANALYSIS] =====================================");
}

uint32_t getTaskCPUUsage(TaskHandle_t taskHandle) {
    if (taskHandle == NULL) {
        return 0;
    }

#if (configGENERATE_RUN_TIME_STATS == 1)
    TaskStatus_t taskStatus;
    vTaskGetInfo(taskHandle, &taskStatus, pdTRUE, eInvalid);
    return taskStatus.ulRunTimeCounter;
#else
    return 0;
#endif
}

uint32_t getLvglTaskHighWaterMark(void) {
    if (lvglTaskHandle) {
        return uxTaskGetStackHighWaterMark(lvglTaskHandle);
    }
    return 0;
}

uint32_t getAudioTaskHighWaterMark(void) {
    if (audioTaskHandle) {
        return uxTaskGetStackHighWaterMark(audioTaskHandle);
    }
    return 0;
}

// =============================================================================
// DYNAMIC TASK MANAGEMENT FUNCTIONS
// =============================================================================

void setTaskSystemState(TaskSystemState_t newState) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (taskSystemConfig.currentState != newState) {
            ESP_LOGI(TAG, "[DYNAMIC] Task system state change: %d -> %d", taskSystemConfig.currentState, newState);
            taskSystemConfig.currentState = newState;
            taskSystemConfig.lastStateChange = millis();

            // Trigger immediate optimization
            optimizeTaskPriorities();
            adjustTaskIntervals();
        }
        xSemaphoreGiveRecursive(taskConfigMutex);
    }
}

void setOTAState(OTAState_t newState) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (taskSystemConfig.otaState != newState) {
            ESP_LOGI(TAG, "[DYNAMIC] OTA state change: %d -> %d", taskSystemConfig.otaState, newState);
            OTAState_t oldState = taskSystemConfig.otaState;
            taskSystemConfig.otaState = newState;

            // Update task system state based on OTA state
            switch (newState) {
                case OTA_STATE_IDLE:
                    if (taskSystemConfig.currentState == TASK_STATE_OTA_ACTIVE) {
                        setTaskSystemState(TASK_STATE_NORMAL);
                    }
                    break;
                case OTA_STATE_CHECKING:
                case OTA_STATE_DOWNLOADING:
                case OTA_STATE_INSTALLING:
                    setTaskSystemState(TASK_STATE_OTA_ACTIVE);
                    break;
                case OTA_STATE_COMPLETE:
                case OTA_STATE_ERROR:
                    // Will transition back to normal after a delay
                    break;
            }

            // Immediate priority adjustment for critical OTA states
            if (newState == OTA_STATE_DOWNLOADING || newState == OTA_STATE_INSTALLING) {
                configureForOTADownload();
            } else if (oldState == OTA_STATE_DOWNLOADING || oldState == OTA_STATE_INSTALLING) {
                // Restore normal operation
                optimizeTaskPriorities();
                adjustTaskIntervals();
            }
        }
        xSemaphoreGiveRecursive(taskConfigMutex);
    }
}

void optimizeTaskPriorities(void) {
    if (!tasksRunning) return;

    ESP_LOGD(TAG, "[DYNAMIC] Optimizing task priorities for state %d, OTA state %d",
             taskSystemConfig.currentState, taskSystemConfig.otaState);

    switch (taskSystemConfig.currentState) {
        case TASK_STATE_NORMAL:
            // Standard operating priorities
            TASK_SET_PRIORITY_SAFE(lvglTaskHandle, LVGL_TASK_PRIORITY_HIGH, "LVGL task");
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL, "Audio task");
            break;

        case TASK_STATE_OTA_ACTIVE:
            // Boost OTA priority, maintain UI responsiveness
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_SUSPENDED, "Audio task");
            break;

        case TASK_STATE_HIGH_LOAD:
            // Boost messaging for high message load
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL, "Audio task");
            break;

        default:
            break;
    }
}

void adjustTaskIntervals(void) {
    // Update interval variables based on current state
    switch (taskSystemConfig.currentState) {
        case TASK_STATE_NORMAL:
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;
            break;

        case TASK_STATE_OTA_ACTIVE:
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_REDUCED;
            break;

        case TASK_STATE_HIGH_LOAD:
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;
            break;

        default:
            break;
    }

    ESP_LOGD(TAG, "[DYNAMIC] Adjusted intervals - Audio: %ums", currentAudioInterval);
}

bool enterEmergencyMode(uint32_t durationMs) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGW(TAG, "[EMERGENCY] Entering emergency mode for %u ms", durationMs);
        taskSystemConfig.emergencyMode = true;
        // setTaskSystemState(TASK_STATE_EMERGENCY);

        // Set up timer to exit emergency mode (simplified - in production use FreeRTOS timer)
        // For now, tasks will check and exit after duration
        xSemaphoreGiveRecursive(taskConfigMutex);
        return true;
    }
    return false;
}

void exitEmergencyMode(void) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (taskSystemConfig.emergencyMode) {
            ESP_LOGI(TAG, "[EMERGENCY] Exiting emergency mode");
            taskSystemConfig.emergencyMode = false;

            // Resume suspended tasks
            if (audioTaskHandle && eTaskGetState(audioTaskHandle) == eSuspended) {
                vTaskResume(audioTaskHandle);
            }

            setTaskSystemState(TASK_STATE_NORMAL);
        }
        xSemaphoreGiveRecursive(taskConfigMutex);
    }
}

void configureForOTADownload(void) {
    ESP_LOGI(TAG, "[OTA] Configuring high-performance mode for OTA download");

    // Suspend non-essential tasks during critical OTA operations
    if (tasksRunning) {
        if (audioTaskHandle && eTaskGetState(audioTaskHandle) != eSuspended) {
            ESP_LOGI(TAG, "[OTA] Suspending Audio task for OTA download");
            vTaskSuspend(audioTaskHandle);
        }
    }
}

void configureForOTAInstall(void) {
    ESP_LOGI(TAG, "[OTA] Configuring minimal interruption mode for OTA installation");

    // During installation, minimize all non-critical operations
    if (tasksRunning) {
        // Keep only LVGL for user feedback and OTA task for installation
        if (audioTaskHandle && eTaskGetState(audioTaskHandle) != eSuspended) {
            ESP_LOGI(TAG, "[OTA] Suspending Audio task for OTA installation");
            vTaskSuspend(audioTaskHandle);
        }
    }
}

// Message load monitoring
void reportMessageActivity(void) {
    messageCount++;

    // Update load statistics every second
    uint32_t now = millis();
    if (now - lastMessageCountReset >= 1000) {
        if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            taskSystemConfig.messageLoad = messageCount;
            messageCount = 0;
            lastMessageCountReset = now;

            // Adjust system state based on message load
            if (taskSystemConfig.messageLoad > 20 && taskSystemConfig.currentState == TASK_STATE_NORMAL) {
                ESP_LOGI(TAG, "[DYNAMIC] High message load detected (%u/s), adjusting priorities", taskSystemConfig.messageLoad);
                setTaskSystemState(TASK_STATE_HIGH_LOAD);
            } else if (taskSystemConfig.messageLoad < 5 && taskSystemConfig.currentState == TASK_STATE_HIGH_LOAD) {
                ESP_LOGI(TAG, "[DYNAMIC] Message load normalized (%u/s), returning to normal state", taskSystemConfig.messageLoad);
                setTaskSystemState(TASK_STATE_NORMAL);
            }

            xSemaphoreGiveRecursive(taskConfigMutex);
        }
    }
}

uint32_t getMessageLoadPerSecond(void) {
    return taskSystemConfig.messageLoad;
}

// =============================================================================
// MESSAGING ENGINE INTEGRATION FUNCTIONS
// =============================================================================

void reportCore1MessagingStats(uint32_t messagesReceived, uint32_t messagesSent, uint32_t bufferOverruns) {
    // Update task load metrics with Core 1 messaging statistics
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        taskSystemConfig.taskLoadMetrics[0] = messagesReceived;
        taskSystemConfig.taskLoadMetrics[1] = messagesSent;
        taskSystemConfig.taskLoadMetrics[2] = bufferOverruns;
        taskSystemConfig.taskLoadMetrics[3] = millis();  // timestamp

        ESP_LOGD(TAG, "[CORE1] Messaging stats - RX: %u, TX: %u, Overruns: %u",
                 messagesReceived, messagesSent, bufferOverruns);

        xSemaphoreGiveRecursive(taskConfigMutex);
    }
}

/**
 * Complete InterruptMessagingEngine Integration
 * This function should be called periodically (every 5-10 seconds) to:
 * 1. Get statistics from InterruptMessagingEngine
 * 2. Update TaskManager's dynamic priority system
 * 3. Adjust Core 0 task priorities based on Core 1 messaging load
 */
void updateMessagingEngineIntegration(void) {
    // Get statistics from InterruptMessagingEngine
    uint32_t messagesReceived, messagesSent, bufferOverruns, core1Routed;
    Messaging::Core1::InterruptMessagingEngine::getStats(messagesReceived, messagesSent, bufferOverruns, core1Routed);

    // Update TaskManager statistics
    reportCore1MessagingStats(messagesReceived, messagesSent, bufferOverruns);

    // Dynamic priority adjustment based on messaging load
    if (bufferOverruns > 0) {
        ESP_LOGW(TAG, "[MESSAGING-INTEGRATION] Buffer overruns detected (%u), optimizing priorities", bufferOverruns);
        // Give Core 1 more breathing room by slightly reducing Core 0 task frequency
        if (audioTaskHandle) {
            // Temporarily reduce audio task priority to reduce Core 0 load
            vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL - 1);
        }
    } else if (messagesReceived > 50) {
        ESP_LOGI(TAG, "[MESSAGING-INTEGRATION] High message throughput (%u/period), maintaining high performance", messagesReceived);
        // Ensure optimal performance for high message loads
        if (audioTaskHandle) {
            vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL);
        }
    }

    ESP_LOGD(TAG, "[MESSAGING-INTEGRATION] Core 1 routed %u messages, total RX: %u, TX: %u",
             core1Routed, messagesReceived, messagesSent);
}

// =============================================================================
// PHASE 4: OTA PERFORMANCE OPTIMIZATION
// =============================================================================

/**
 * ULTRA-MINIMAL OTA BOOT MODE CONFIGURATION
 * Eliminates ALL non-essential operations during OTA boot mode
 */
void configureUltraMinimalOTAMode(void) {
    ESP_LOGI(TAG, "[PHASE4-OTA] Configuring ultra-minimal OTA boot mode");

    // Suspend ALL non-essential tasks immediately
    if (tasksRunning) {
        if (audioTaskHandle && eTaskGetState(audioTaskHandle) != eSuspended) {
            ESP_LOGI(TAG, "[PHASE4-OTA] Suspending Audio task for minimal OTA mode");
            vTaskSuspend(audioTaskHandle);
        }
    }

    // Set ultra-conservative intervals for remaining operations
    currentAudioInterval = 30000;  // 30 seconds (effectively disabled)

    // Disable all periodic operations
    taskSystemConfig.otaState = OTA_STATE_ULTRA_MINIMAL;
    taskSystemConfig.currentState = TASK_STATE_OTA_ACTIVE;
    taskSystemConfig.emergencyMode = false;  // Ensure no emergency processing

    ESP_LOGI(TAG, "[PHASE4-OTA] Ultra-minimal OTA mode configured - maximum performance for OTA download");
}

/**
 * NORMAL MODE BACKGROUND TASK OPTIMIZATION
 * Eliminates unnecessary background operations during normal operation
 */
void optimizeNormalModePerformance(void) {
    ESP_LOGI(TAG, "[PHASE4-NORMAL] Optimizing normal mode for maximum performance");

    // Extend all background operation intervals significantly
    taskSystemConfig.backgroundOperationsDisabled = true;

    // Configure ultra-efficient task intervals
    currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;  // Keep audio responsive

    // Disable non-critical periodic operations
    taskSystemConfig.logoCheckingDisabled = true;
    taskSystemConfig.debugStatisticsDisabled = true;
    taskSystemConfig.nonEssentialUpdatesDisabled = true;

    ESP_LOGI(TAG, "[PHASE4-NORMAL] Normal mode optimized - background tasks minimized");
}

/**
 * ADAPTIVE PERFORMANCE MANAGEMENT
 * Dynamically adjusts performance based on system load and OTA state
 */
void updateAdaptivePerformanceSettings(void) {
    static unsigned long lastOptimizationCheck = 0;
    unsigned long currentTime = millis();

    // Only check every 10 seconds to avoid overhead
    if (currentTime - lastOptimizationCheck < 10000) {
        return;
    }
    lastOptimizationCheck = currentTime;

    // Get current system load
    uint32_t messageLoad = getMessageLoadPerSecond();
    uint32_t freeHeap = esp_get_free_heap_size();

    // Adaptive interval adjustment based on system resources
    if (freeHeap < 50000) {  // Low memory - reduce background operations
        currentAudioInterval = AUDIO_UPDATE_INTERVAL_REDUCED * 2;
        ESP_LOGW(TAG, "[PHASE4-ADAPTIVE] Low memory detected (%u bytes), reducing background operations", freeHeap);
    } else if (messageLoad > 25) {  // High message load - prioritize messaging
        currentAudioInterval = AUDIO_UPDATE_INTERVAL_REDUCED;
        ESP_LOGI(TAG, "[PHASE4-ADAPTIVE] High message load (%u/s), prioritizing messaging performance", messageLoad);
    } else {
        // Normal operation - standard intervals
        currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;
    }
}

}  // namespace TaskManager
}  // namespace Application
