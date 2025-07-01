#include "TaskManager.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../../include/DebugUtils.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/MessageAPI.h"
#include "AppController.h"
#include "AudioUI.h"
#include "LogoSupplier.h"
#include "LVGLMessageHandler.h"
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

// Task handles
TaskHandle_t lvglTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t messagingTaskHandle = NULL;
TaskHandle_t otaTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;

// Synchronization objects
SemaphoreHandle_t lvglMutex = NULL;
QueueHandle_t otaProgressQueue = NULL;

// Dynamic task management
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
static OTAProgressData_t currentOTAProgress = {0, false, false, "Ready"};

// Message load tracking
static uint32_t messageCount = 0;
static uint32_t lastMessageCountReset = 0;
static uint32_t currentNetworkInterval = NETWORK_UPDATE_INTERVAL_NORMAL;
static uint32_t currentMessagingInterval = MESSAGING_UPDATE_INTERVAL_NORMAL;
static uint32_t currentOTAInterval = OTA_UPDATE_INTERVAL_IDLE;
static uint32_t currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;

// =============================================================================
// INTERNAL SHARED INITIALIZATION FUNCTIONS
// =============================================================================

bool initializeSharedComponents(bool networkFreeMode) {
    ESP_LOGI(TAG, "[SHARED-INIT] Initializing shared components (network-free: %s)",
             networkFreeMode ? "YES" : "NO");
    ESP_LOGE(TAG, "[DEBUG] initializeSharedComponents() - using ERROR level to force visibility");

    tasksRunning = true;

    // Create synchronization objects
    MUTEX_CREATE_SAFE(taskConfigMutex, "task configuration mutex");
    MUTEX_CREATE_SAFE(lvglMutex, "LVGL mutex");
    QUEUE_CREATE_SAFE(otaProgressQueue, 1, sizeof(OTAProgressData_t), "OTA progress queue");

    // Initialize task system configuration
    taskSystemConfig.currentState = TASK_STATE_NORMAL;
    taskSystemConfig.otaState = OTA_STATE_IDLE;
    taskSystemConfig.taskMode = networkFreeMode ? TASK_MODE_NETWORK_FREE : TASK_MODE_OTA_ACTIVE;
    taskSystemConfig.messageLoad = 0;
    taskSystemConfig.lastStateChange = millis();
    taskSystemConfig.emergencyMode = false;
    taskSystemConfig.networkTasksActive = !networkFreeMode;  // Network tasks active only if not network-free

    // CRITICAL: Initialize LVGL Message Handler - ALWAYS required for UI
    ESP_LOGI(TAG, "[SHARED-INIT] Initializing LVGL Message Handler...");
    if (!LVGLMessageHandler::init()) {
        ESP_LOGE(TAG, "[SHARED-INIT] CRITICAL: Failed to initialize LVGL Message Handler");
        return false;
    }
    ESP_LOGI(TAG, "[SHARED-INIT] LVGL Message Handler initialized successfully");

    ESP_LOGI(TAG, "[SHARED-INIT] Shared components initialization completed successfully");
    return true;
}

bool createEssentialTasks(bool networkFreeMode) {
    ESP_LOGI(TAG, "[ESSENTIAL-TASKS] Creating essential tasks (network-free: %s)",
             networkFreeMode ? "YES" : "NO");

    // Create essential tasks that are always needed
    TASK_CREATE_PINNED(lvglTask, "LVGL_Task", LVGL_TASK_STACK_SIZE, LVGL_TASK_PRIORITY_HIGH, lvglTaskHandle, LVGL_TASK_CORE);
    TASK_CREATE_PINNED(messagingTask, "Messaging_Task", MESSAGING_TASK_STACK_SIZE, MESSAGING_TASK_PRIORITY_HIGH, messagingTaskHandle, MESSAGING_TASK_CORE);

    // Audio task gets priority boost in network-free mode (freed network resources)
    UBaseType_t audioPriority = networkFreeMode ? (AUDIO_TASK_PRIORITY_NORMAL + 1) : AUDIO_TASK_PRIORITY_NORMAL;
    size_t audioStackSize = networkFreeMode ? (AUDIO_TASK_STACK_SIZE + 2048) : AUDIO_TASK_STACK_SIZE;

    TASK_CREATE_PINNED(audioTask, "Audio_Task", audioStackSize, audioPriority, audioTaskHandle, AUDIO_TASK_CORE);

    ESP_LOGI(TAG, "[ESSENTIAL-TASKS] Essential tasks created successfully");
    return true;
}

bool createNetworkTasks() {
    ESP_LOGI(TAG, "[NETWORK-TASKS] Creating network-dependent tasks");

    // Traditional always-on network mode or conditional network tasks
    TASK_CREATE_PINNED(networkTask, "Network_Task", NETWORK_TASK_STACK_SIZE, NETWORK_TASK_PRIORITY_HIGH, networkTaskHandle, NETWORK_TASK_CORE);

#if OTA_ENABLE_UPDATES
    TASK_CREATE_PINNED(otaTask, "OTA_Task", OTA_TASK_STACK_SIZE, OTA_TASK_PRIORITY_IDLE, otaTaskHandle, OTA_TASK_CORE);
    ESP_LOGI(TAG, "[NETWORK-TASKS] OTA task created with adaptive priority management");
#else
    ESP_LOGI(TAG, "[NETWORK-TASKS] OTA updates disabled - skipping OTA task creation");
#endif

    ESP_LOGI(TAG, "[NETWORK-TASKS] Network tasks created successfully");
    return true;
}

void printInitializationSummary(bool networkFreeMode) {
    ESP_LOGI(TAG, "[INIT] SUCCESS: Task system initialized with %s",
             networkFreeMode ? "network-free architecture" : "traditional always-on network");

    if (networkFreeMode) {
        ESP_LOGI(TAG, "[INIT] Network-free mode: Freed ~%d bytes from disabled network tasks", getFreedNetworkMemory());
    }

    // Print initial task configuration
    printTaskStats();
    printTaskLoadAnalysis();

    ESP_LOGI(TAG, "[INIT] Dynamic Task Manager initialization completed successfully");
}

// =============================================================================
// PUBLIC INITIALIZATION FUNCTIONS
// =============================================================================

bool init(void) {
    Serial.println("TaskManager::init() called - logging may be filtered");
    ESP_LOGI(TAG, "[INIT] Starting Dynamic Task Manager initialization for ESP32-S3 dual-core");
    ESP_LOGE(TAG, "[DEBUG] TaskManager init() - using ERROR level to force visibility");

    // Initialize shared components
    if (!initializeSharedComponents(false)) {  // false = not network-free
        return false;
    }

    // Create essential tasks
    if (!createEssentialTasks(false)) {  // false = not network-free
        return false;
    }

    // Network-free architecture - network tasks created on-demand for OTA only
    ESP_LOGI(TAG, "[INIT] Network-free mode: Skipping network tasks (will be created on-demand for OTA)");

    // Update task configuration for network-free mode
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        taskSystemConfig.taskMode = TASK_MODE_NETWORK_FREE;
        taskSystemConfig.networkTasksActive = false;
        xSemaphoreGiveRecursive(taskConfigMutex);
    }

    printInitializationSummary(true);  // true = network-free mode

    return true;
}

bool initNetworkFreeTasks(void) {
    Serial.println("TaskManager::initNetworkFreeTasks() called - this should show!");
    ESP_LOGI(TAG, "[NETWORK-FREE] Initializing network-free task system");
    ESP_LOGE(TAG, "[DEBUG] initNetworkFreeTasks() - using ERROR level to force visibility");

    // Initialize shared components in network-free mode
    if (!initializeSharedComponents(true)) {  // true = network-free
        return false;
    }

    // Create essential tasks with network-free optimizations
    if (!createEssentialTasks(true)) {  // true = network-free
        return false;
    }

    ESP_LOGE(TAG, "[NETWORK-FREE] Network-free task system initialized successfully");

    printInitializationSummary(true);  // true = network-free

    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "[DEINIT] Starting Task Manager deinitialization");

    tasksRunning = false;

    // Wait a bit for tasks to finish their current operations
    vTaskDelay(pdMS_TO_TICKS(100));

    // Delete all tasks
    TASK_DELETE_SAFE(lvglTaskHandle, "LVGL task");
    TASK_DELETE_SAFE(networkTaskHandle, "Network task");
    TASK_DELETE_SAFE(messagingTaskHandle, "Messaging task");
    TASK_DELETE_SAFE(otaTaskHandle, "OTA task");
    TASK_DELETE_SAFE(audioTaskHandle, "Audio task");

    // Clean up synchronization objects
    MUTEX_DELETE_SAFE(lvglMutex, "LVGL mutex");
    QUEUE_DELETE_SAFE(otaProgressQueue, "OTA progress queue");
    MUTEX_DELETE_SAFE(taskConfigMutex, "task configuration mutex");

    ESP_LOGI(TAG, "[DEINIT] Task Manager deinitialization completed");
}

void suspend(void) {
    if (tasksRunning) {
        TASK_SUSPEND_SAFE(lvglTaskHandle, "LVGL task");
        TASK_SUSPEND_SAFE(networkTaskHandle, "Network task");
        TASK_SUSPEND_SAFE(messagingTaskHandle, "Messaging task");
        TASK_SUSPEND_SAFE(otaTaskHandle, "OTA task");
        TASK_SUSPEND_SAFE(audioTaskHandle, "Audio task");
    }
}

void resume(void) {
    if (tasksRunning) {
        TASK_RESUME_SAFE(lvglTaskHandle, "LVGL task");
        TASK_RESUME_SAFE(networkTaskHandle, "Network task");
        TASK_RESUME_SAFE(messagingTaskHandle, "Messaging task");
        TASK_RESUME_SAFE(otaTaskHandle, "OTA task");
        TASK_RESUME_SAFE(audioTaskHandle, "Audio task");
    }
}

void suspendForOTA(void) {
    if (tasksRunning) {
        ESP_LOGI(TAG, "[OTA] Suspending non-essential tasks for OTA update...");
        // Keep network, OTA, and LVGL tasks running for OTA process and UI feedback
        TASK_SUSPEND_SAFE(messagingTaskHandle, "Messaging_Task");
        TASK_SUSPEND_SAFE(audioTaskHandle, "Audio_Task");
        ESP_LOGI(TAG, "[OTA] Finished suspending tasks for OTA.");
    } else {
        ESP_LOGW(TAG, "[OTA] Cannot suspend tasks - tasks not running");
    }
}

void resumeFromOTA(void) {
    if (tasksRunning) {
        ESP_LOGI(TAG, "[OTA] Resuming tasks after OTA update...");
        TASK_RESUME_SAFE(messagingTaskHandle, "Messaging_Task");
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

// Network Task - Core 1, Adaptive Intervals
void networkTask(void *parameter) {
    ESP_LOGI(TAG, "[NETWORK_TASK] Network Task started on Core %d with adaptive intervals", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (tasksRunning) {
        // Update network status
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || \
    OTA_ENABLE_UPDATES
        PERF_TIMER_START(network_update);
        Hardware::Network::update();
        PERF_TIMER_END(network_update, 50000);  // Warn if network update takes >50ms

        bool connected = Hardware::Network::isConnected();
        const char *status = Hardware::Network::getWifiStatusString();
        const char *ssid = Hardware::Network::getSsid();
        const char *ip = Hardware::Network::getIpAddress();

        // Send UI update messages instead of direct LVGL calls
        PERF_TIMER_START(network_ui_update);
        LVGLMessageHandler::updateWifiStatus(status, connected);
        LVGLMessageHandler::updateNetworkInfo(ssid, ip);
        PERF_TIMER_END(network_ui_update, 10000);  // Warn if UI update takes >10ms
#endif

        // Update SD card status (hardware monitoring)
        Hardware::SD::update();
        Hardware::SD::SDCardInfo cardInfo = Hardware::SD::getCardInfo();

        // Send SD status UI update messages - use advanced hash comparison
        static uint32_t lastCardInfoHash = 0;

        if (HASH_CHANGED(cardInfo, lastCardInfoHash)) {
            ESP_LOGI(TAG, "[NETWORK_TASK] Card Info Changed");

            const char *statusStr = Hardware::SD::getStatusString();

            // Send UI update
            LVGLMessageHandler::updateSDStatus(statusStr, cardInfo.isMounted(), cardInfo.getTotalMB(), cardInfo.getUsedMB(), cardInfo.cardType);
        }
        // Use adaptive interval based on current system state
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(currentNetworkInterval));
    }

    ESP_LOGI(TAG, "[NETWORK_TASK] Network Task ended");
    vTaskDelete(NULL);
}

// Messaging Task - Core 0, High Priority with Load Monitoring
void messagingTask(void *parameter) {
    ESP_LOGI(TAG, "[MESSAGING_TASK] Messaging Task started on Core %d with load monitoring", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    static unsigned long lastLoadReport = 0;

    while (tasksRunning) {
        // Update message system (high frequency for responsiveness)
        Messaging::MessageAPI::update();

        // Report message activity for load monitoring
        unsigned long currentTime = millis();
        if (currentTime - lastLoadReport >= 100) {  // Report every 100ms for responsiveness
            reportMessageActivity();
            lastLoadReport = currentTime;
        }

        // TODO: Re-enable logo system updates with new Logo system
        // Temporarily disabled during logo system migration

        // Use adaptive interval based on current system load
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(currentMessagingInterval));
    }

    ESP_LOGI(TAG, "[MESSAGING_TASK] Messaging Task ended");
    vTaskDelete(NULL);
}

#if OTA_ENABLE_UPDATES
// OTA Task - Core 1, Adaptive Priority (IDLE -> CRITICAL based on activity)
void otaTask(void *parameter) {
    ESP_LOGI(TAG, "[OTA_TASK] Adaptive OTA Task started on Core %d", xPortGetCoreID());

    TickType_t lastWakeTime = xTaskGetTickCount();
    static unsigned long lastOTACheck = 0;
    static bool wasOTAActive = false;

    // Initialize OTA state
    setOTAState(OTA_STATE_IDLE);

    while (tasksRunning) {
        unsigned long currentTime = millis();
        bool otaActive = Hardware::OTA::OTAManager::isActive();

        // Detect OTA state changes and update system accordingly
        if (otaActive != wasOTAActive) {
            if (otaActive) {
                ESP_LOGI(TAG, "[OTA_TASK] OTA update detected - switching to high-priority mode");
                setOTAState(OTA_STATE_DOWNLOADING);
            } else {
                ESP_LOGI(TAG, "[OTA_TASK] OTA update completed - returning to idle mode");
                setOTAState(OTA_STATE_IDLE);
            }
            wasOTAActive = otaActive;
        }

        // Only check for OTA updates at adaptive intervals
        if (currentTime - lastOTACheck >= currentOTAInterval) {
            lastOTACheck = currentTime;

            // Update OTA state before checking
            if (!otaActive) {
                setOTAState(OTA_STATE_CHECKING);
            }

            // Check for OTA updates or re-initialize if needed
            Hardware::OTA::OTAManager::update();

            // Return to idle if no activity detected
            if (!otaActive && !Hardware::OTA::OTAManager::isActive()) {
                setOTAState(OTA_STATE_IDLE);
            }

            ESP_LOGD(TAG, "[OTA_TASK] OTA check completed, next check in %ums", currentOTAInterval);
        }

        // Use dynamic interval for task sleep
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(currentOTAInterval));
    }

    ESP_LOGI(TAG, "[OTA_TASK] Adaptive OTA Task ended");
    vTaskDelete(NULL);
}
#endif

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
        if (taskSystemConfig.currentState == TASK_STATE_EMERGENCY ||
            taskSystemConfig.currentState == TASK_STATE_OTA_ACTIVE) {
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

        // OPTIMIZED: Heartbeat logging reduced frequency
        static int heartbeat = 0;
        if (++heartbeat % 50 == 0) {  // Reduced from every 10 to every 50
            ESP_LOGD(TAG, "[AUDIO_TASK] Heartbeat: %d (failures: %d, emergency: %s)",
                     heartbeat, consecutiveFailures, emergencyMode ? "YES" : "NO");
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
    LOG_TASK_CONFIG_WITH_INTERVAL("Network Task", NETWORK_TASK_CORE, NETWORK_TASK_PRIORITY_HIGH, NETWORK_TASK_STACK_SIZE, currentNetworkInterval);
    LOG_TASK_CONFIG_WITH_INTERVAL("Messaging Task", MESSAGING_TASK_CORE, MESSAGING_TASK_PRIORITY_HIGH, MESSAGING_TASK_STACK_SIZE, currentMessagingInterval);
    LOG_TASK_CONFIG_WITH_INTERVAL("OTA Task", OTA_TASK_CORE, OTA_TASK_PRIORITY_IDLE, OTA_TASK_STACK_SIZE, currentOTAInterval);
    LOG_TASK_CONFIG("Audio Task", AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY_NORMAL, AUDIO_TASK_STACK_SIZE);
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

    if (networkTaskHandle) {
        UBaseType_t currentPriority = uxTaskPriorityGet(networkTaskHandle);
        ESP_LOGI(TAG, "[ANALYSIS] Network Task: Current Priority %d, Stack HWM: %d bytes",
                 currentPriority, uxTaskGetStackHighWaterMark(networkTaskHandle) * sizeof(StackType_t));
    }

    if (messagingTaskHandle) {
        UBaseType_t currentPriority = uxTaskPriorityGet(messagingTaskHandle);
        ESP_LOGI(TAG, "[ANALYSIS] Messaging Task: Current Priority %d, Stack HWM: %d bytes",
                 currentPriority, uxTaskGetStackHighWaterMark(messagingTaskHandle) * sizeof(StackType_t));
    }

    if (otaTaskHandle) {
        UBaseType_t currentPriority = uxTaskPriorityGet(otaTaskHandle);
        eTaskState taskState = eTaskGetState(otaTaskHandle);
        const char *stateStr = (taskState == eReady) ? "Ready" : (taskState == eRunning) ? "Running"
                                                             : (taskState == eBlocked)   ? "Blocked"
                                                             : (taskState == eSuspended) ? "Suspended"
                                                                                         : "Unknown";
        ESP_LOGI(TAG, "[ANALYSIS] OTA Task: Current Priority %d, State: %s, Stack HWM: %d bytes",
                 currentPriority, stateStr, uxTaskGetStackHighWaterMark(otaTaskHandle) * sizeof(StackType_t));
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

    // Performance optimization summary
    uint32_t otaEfficiencyGain = (OTA_UPDATE_INTERVAL_IDLE / 2000) * 100;  // Percentage improvement
    ESP_LOGI(TAG, "[ANALYSIS] OTA Task Efficiency Gain: %u%% (from 2s to %us intervals when idle)",
             otaEfficiencyGain, OTA_UPDATE_INTERVAL_IDLE / 1000);

    ESP_LOGI(TAG, "[ANALYSIS] Core Load Distribution:");
    ESP_LOGI(TAG, "[ANALYSIS]   Core 0: LVGL (Priority %d), Messaging (Priority varies), Audio (Priority varies)",
             LVGL_TASK_PRIORITY_HIGH);
    ESP_LOGI(TAG, "[ANALYSIS]   Core 1: Network (Priority %d), OTA (Priority %d-%d adaptive)",
             NETWORK_TASK_PRIORITY_HIGH, OTA_TASK_PRIORITY_IDLE, OTA_TASK_PRIORITY_CRITICAL);

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

uint32_t getNetworkTaskHighWaterMark(void) {
    if (networkTaskHandle) {
        return uxTaskGetStackHighWaterMark(networkTaskHandle);
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
            TASK_SET_PRIORITY_SAFE(messagingTaskHandle, MESSAGING_TASK_PRIORITY_HIGH, "Messaging task");
            TASK_SET_PRIORITY_SAFE(networkTaskHandle, NETWORK_TASK_PRIORITY_HIGH, "Network task");
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL, "Audio task");
            TASK_SET_PRIORITY_SAFE(otaTaskHandle, OTA_TASK_PRIORITY_IDLE, "OTA task");
            break;

        case TASK_STATE_OTA_ACTIVE:
            // Boost OTA priority, maintain UI responsiveness
            TASK_SET_PRIORITY_SAFE(otaTaskHandle, OTA_TASK_PRIORITY_CRITICAL, "OTA task");
            TASK_SET_PRIORITY_SAFE(lvglTaskHandle, LVGL_TASK_PRIORITY_CRITICAL, "LVGL task");
            TASK_SET_PRIORITY_SAFE(networkTaskHandle, NETWORK_TASK_PRIORITY_HIGH, "Network task");
            TASK_SET_PRIORITY_SAFE(messagingTaskHandle, MESSAGING_TASK_PRIORITY_LOW, "Messaging task");
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_SUSPENDED, "Audio task");
            break;

        case TASK_STATE_HIGH_LOAD:
            // Boost messaging for high message load
            TASK_SET_PRIORITY_SAFE(messagingTaskHandle, MESSAGING_TASK_PRIORITY_HIGH, "Messaging task");
            TASK_SET_PRIORITY_SAFE(lvglTaskHandle, LVGL_TASK_PRIORITY_HIGH, "LVGL task");
            TASK_SET_PRIORITY_SAFE(networkTaskHandle, NETWORK_TASK_PRIORITY_HIGH, "Network task");
            TASK_SET_PRIORITY_SAFE(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL, "Audio task");
            TASK_SET_PRIORITY_SAFE(otaTaskHandle, OTA_TASK_PRIORITY_IDLE, "OTA task");
            break;

        case TASK_STATE_EMERGENCY:
            // Maximum priority for critical operations
            TASK_SET_PRIORITY_SAFE(lvglTaskHandle, LVGL_TASK_PRIORITY_CRITICAL, "LVGL task");
            // Suspend non-critical tasks temporarily
            TASK_SUSPEND_SAFE(audioTaskHandle, "Audio task");
            break;

        default:
            break;
    }
}

void adjustTaskIntervals(void) {
    // Update interval variables based on current state
    switch (taskSystemConfig.currentState) {
        case TASK_STATE_NORMAL:
            currentNetworkInterval = NETWORK_UPDATE_INTERVAL_NORMAL;
            currentMessagingInterval = (taskSystemConfig.messageLoad > 10) ? MESSAGING_UPDATE_INTERVAL_HIGH_LOAD : MESSAGING_UPDATE_INTERVAL_NORMAL;
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;
            break;

        case TASK_STATE_OTA_ACTIVE:
            currentNetworkInterval = NETWORK_UPDATE_INTERVAL_OTA;
            currentMessagingInterval = MESSAGING_UPDATE_INTERVAL_NORMAL;
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_REDUCED;
            break;

        case TASK_STATE_HIGH_LOAD:
            currentNetworkInterval = NETWORK_UPDATE_INTERVAL_NORMAL;
            currentMessagingInterval = MESSAGING_UPDATE_INTERVAL_HIGH_LOAD;
            currentAudioInterval = AUDIO_UPDATE_INTERVAL_NORMAL;
            break;

        default:
            break;
    }

    // OTA-specific interval adjustment
    switch (taskSystemConfig.otaState) {
        case OTA_STATE_IDLE:
            currentOTAInterval = OTA_UPDATE_INTERVAL_IDLE;
            break;
        case OTA_STATE_CHECKING:
            currentOTAInterval = OTA_UPDATE_INTERVAL_CHECKING;
            break;
        case OTA_STATE_DOWNLOADING:
        case OTA_STATE_INSTALLING:
            currentOTAInterval = OTA_UPDATE_INTERVAL_ACTIVE;
            break;
        default:
            currentOTAInterval = OTA_UPDATE_INTERVAL_CHECKING;
            break;
    }

    ESP_LOGD(TAG, "[DYNAMIC] Adjusted intervals - Network: %ums, Messaging: %ums, OTA: %ums, Audio: %ums",
             currentNetworkInterval, currentMessagingInterval, currentOTAInterval, currentAudioInterval);
}

bool enterEmergencyMode(uint32_t durationMs) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGW(TAG, "[EMERGENCY] Entering emergency mode for %u ms", durationMs);
        taskSystemConfig.emergencyMode = true;
        setTaskSystemState(TASK_STATE_EMERGENCY);

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

        // Reduce messaging priority but don't suspend (may need to receive OTA commands)
        if (messagingTaskHandle) {
            vTaskPrioritySet(messagingTaskHandle, MESSAGING_TASK_PRIORITY_LOW);
        }

        // Boost OTA and network priorities
        if (otaTaskHandle) {
            vTaskPrioritySet(otaTaskHandle, OTA_TASK_PRIORITY_CRITICAL);
        }
        if (networkTaskHandle) {
            vTaskPrioritySet(networkTaskHandle, NETWORK_TASK_PRIORITY_HIGH);
        }
    }
}

void configureForOTAInstall(void) {
    ESP_LOGI(TAG, "[OTA] Configuring minimal interruption mode for OTA installation");

    // During installation, minimize all non-critical operations
    if (tasksRunning) {
        // Keep only LVGL for user feedback and OTA task for installation
        if (messagingTaskHandle && eTaskGetState(messagingTaskHandle) != eSuspended) {
            ESP_LOGI(TAG, "[OTA] Suspending Messaging task for OTA installation");
            vTaskSuspend(messagingTaskHandle);
        }
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
// NETWORK-FREE ARCHITECTURE FUNCTIONS
// =============================================================================

bool createOTATasks(void) {
    ESP_LOGI(TAG, "[OTA-MODE] Creating network tasks for OTA operation");

    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (taskSystemConfig.networkTasksActive) {
            ESP_LOGW(TAG, "[OTA-MODE] Network tasks already active");
            xSemaphoreGiveRecursive(taskConfigMutex);
            return true;
        }

        taskSystemConfig.taskMode = TASK_MODE_OTA_ACTIVE;
        xSemaphoreGiveRecursive(taskConfigMutex);
    }

    // Use shared network task creation function
    if (!createNetworkTasks()) {
        ESP_LOGE(TAG, "[OTA-MODE] Failed to create network tasks using shared function");
        return false;
    }

    // Set OTA task to critical priority for OTA operations
    if (otaTaskHandle) {
        vTaskPrioritySet(otaTaskHandle, OTA_TASK_PRIORITY_CRITICAL);
        ESP_LOGI(TAG, "[OTA-MODE] Boosted OTA task priority to critical for OTA operations");
    }

    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        taskSystemConfig.networkTasksActive = true;
        xSemaphoreGiveRecursive(taskConfigMutex);
    }

    ESP_LOGI(TAG, "[OTA-MODE] Network tasks created successfully for OTA");
    return true;
}

void destroyOTATasks(void) {
    ESP_LOGI(TAG, "[NETWORK-FREE] Destroying network tasks and returning to network-free mode");

    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!taskSystemConfig.networkTasksActive) {
            ESP_LOGW(TAG, "[NETWORK-FREE] Network tasks already inactive");
            xSemaphoreGiveRecursive(taskConfigMutex);
            return;
        }

        taskSystemConfig.taskMode = TASK_MODE_NETWORK_FREE;
        taskSystemConfig.networkTasksActive = false;
        xSemaphoreGiveRecursive(taskConfigMutex);
    }

    // Delete Network task
    if (networkTaskHandle) {
        ESP_LOGI(TAG, "[NETWORK-FREE] Deleting Network task");
        vTaskDelete(networkTaskHandle);
        networkTaskHandle = NULL;
    }

    // Delete OTA task
    if (otaTaskHandle) {
        ESP_LOGI(TAG, "[NETWORK-FREE] Deleting OTA task");
        vTaskDelete(otaTaskHandle);
        otaTaskHandle = NULL;
    }

    // Wait for tasks to fully terminate
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "[NETWORK-FREE] Network tasks destroyed, freed ~%d bytes", getFreedNetworkMemory());
}

void switchToNetworkFreeMode(void) {
    ESP_LOGI(TAG, "[NETWORK-FREE] Switching to network-free mode with resource reallocation");

    destroyOTATasks();
    reallocateNetworkResources();

    // Boost remaining tasks with freed resources
    if (audioTaskHandle) {
        vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL + 1);
        ESP_LOGI(TAG, "[NETWORK-FREE] Boosted Audio task priority");
    }

    if (lvglTaskHandle) {
        vTaskPrioritySet(lvglTaskHandle, LVGL_TASK_PRIORITY_HIGH);
        ESP_LOGI(TAG, "[NETWORK-FREE] Maintained LVGL task high priority");
    }

    setTaskSystemState(TASK_STATE_NORMAL);

    ESP_LOGI(TAG, "[NETWORK-FREE] Successfully switched to network-free mode");
}

void switchToOTAMode(void) {
    ESP_LOGI(TAG, "[OTA-MODE] Switching to OTA mode with network task creation");

    restoreNetworkResources();

    if (!createOTATasks()) {
        ESP_LOGE(TAG, "[OTA-MODE] Failed to create OTA tasks");
        return;
    }

    // Reduce audio task priority to make room for network operations
    if (audioTaskHandle) {
        vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL);
    }

    setTaskSystemState(TASK_STATE_OTA_ACTIVE);
    setOTAState(OTA_STATE_CHECKING);

    ESP_LOGI(TAG, "[OTA-MODE] Successfully switched to OTA mode");
}

size_t getFreedNetworkMemory(void) {
    // Calculate memory freed from network tasks
    size_t freedMemory = 0;

    if (taskSystemConfig.taskMode == TASK_MODE_NETWORK_FREE) {
        freedMemory += NETWORK_TASK_STACK_SIZE;  // Network task stack
        freedMemory += OTA_TASK_STACK_SIZE;      // OTA task stack
        freedMemory += 1024;                     // Additional overhead (task control blocks, etc.)
    }

    return freedMemory;
}

void reallocateNetworkResources(void) {
    ESP_LOGI(TAG, "[RESOURCE] Reallocating network resources to UI/audio performance");

    if (taskSystemConfig.taskMode != TASK_MODE_NETWORK_FREE) {
        ESP_LOGW(TAG, "[RESOURCE] Not in network-free mode, skipping reallocation");
        return;
    }

    size_t freedMem = getFreedNetworkMemory();
    ESP_LOGI(TAG, "[RESOURCE] %d bytes available for reallocation from network tasks", freedMem);

    // Boost task priorities with freed resources
    if (audioTaskHandle) {
        vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL + 1);
    }

    if (messagingTaskHandle) {
        vTaskPrioritySet(messagingTaskHandle, MESSAGING_TASK_PRIORITY_HIGH);
    }

    ESP_LOGI(TAG, "[RESOURCE] Resource reallocation completed for network-free operation");
}

void restoreNetworkResources(void) {
    ESP_LOGI(TAG, "[RESOURCE] Restoring network resources for OTA operation");

    // Reset task priorities to normal for network operation
    if (audioTaskHandle) {
        vTaskPrioritySet(audioTaskHandle, AUDIO_TASK_PRIORITY_NORMAL);
    }

    if (messagingTaskHandle) {
        vTaskPrioritySet(messagingTaskHandle, MESSAGING_TASK_PRIORITY_HIGH);
    }

    ESP_LOGI(TAG, "[RESOURCE] Network resources restored for OTA mode");
}

TaskMode getCurrentTaskMode(void) {
    return taskSystemConfig.taskMode;
}

bool setTaskMode(TaskMode mode) {
    if (taskConfigMutex && xSemaphoreTakeRecursive(taskConfigMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        TaskMode oldMode = taskSystemConfig.taskMode;

        if (oldMode == mode) {
            xSemaphoreGiveRecursive(taskConfigMutex);
            return true;  // Already in requested mode
        }

        ESP_LOGI(TAG, "[MODE] Switching task mode: %d -> %d", oldMode, mode);

        switch (mode) {
            case TASK_MODE_NETWORK_FREE:
                xSemaphoreGiveRecursive(taskConfigMutex);
                switchToNetworkFreeMode();
                break;

            case TASK_MODE_OTA_ACTIVE:
                xSemaphoreGiveRecursive(taskConfigMutex);
                switchToOTAMode();
                break;

            default:
                ESP_LOGE(TAG, "[MODE] Unknown task mode: %d", mode);
                xSemaphoreGiveRecursive(taskConfigMutex);
                return false;
        }

        return true;
    }

    ESP_LOGE(TAG, "[MODE] Failed to acquire task config mutex");
    return false;
}

bool isNetworkFree(void) {
    return taskSystemConfig.taskMode == TASK_MODE_NETWORK_FREE && !taskSystemConfig.networkTasksActive;
}

}  // namespace TaskManager
}  // namespace Application
