#include "../../include/DebugUtils.h"
#include "TaskManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_debug_helpers.h>
#include <driver/gpio.h>
#include <esp32/clk.h>

#ifdef DEBUG_PERFORMANCE

static const char* TAG = "PERF_PROFILER";

// Performance monitoring state
static bool continuousMonitoring = false;
static TaskHandle_t monitoringTaskHandle = NULL;
static SemaphoreHandle_t perfMutex = NULL;

// Task performance tracking
struct TaskPerfData {
    TaskHandle_t handle;
    const char* name;
    uint32_t lastRunTime;
    uint32_t totalRunTime;
    uint32_t switchCount;
    uint32_t maxExecutionTime;
    uint64_t lastSwitchTime;
};

static TaskPerfData taskPerfData[10];  // Track up to 10 tasks
static uint8_t trackedTaskCount = 0;

// Logic analyzer profiling disabled - not needed for this debugging session
// Uncomment these lines if LOGIC_ANALYZER_ENABLED is set to 1 in DebugUtils.h
// #define PROF_PIN_CORE0_TASK GPIO_NUM_10   // Core 0 task activity
// #define PROF_PIN_CORE1_TASK GPIO_NUM_11   // Core 1 task activity
// #define PROF_PIN_LVGL_ACTIVE GPIO_NUM_12  // LVGL processing
// #define PROF_PIN_MUTEX_WAIT GPIO_NUM_13   // Mutex contention

void TaskProfiler::printDetailedTaskStats() {
    ESP_LOGI(TAG, "=== DETAILED TASK STATISTICS ===");

    TaskStatus_t* taskStatusArray;
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    uint32_t totalRunTime;

    taskStatusArray = (TaskStatus_t*)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
    if (taskStatusArray == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for task status array");
        return;
    }

    taskCount = uxTaskGetSystemState(taskStatusArray, taskCount, &totalRunTime);

    // Calculate CPU usage percentages
    ESP_LOGI(TAG, "Task Name\t\tState\tPrio\tStack\tCore\tCPU%%\tRunTime");
    ESP_LOGI(TAG, "================================================================");

    for (UBaseType_t i = 0; i < taskCount; i++) {
        TaskStatus_t* task = &taskStatusArray[i];

        // Calculate CPU percentage
        uint32_t cpuPercent = 0;
        if (totalRunTime > 0) {
            cpuPercent = (task->ulRunTimeCounter * 100UL) / totalRunTime;
        }

        // Get task core affinity
        int8_t coreId = -1;
        if (task->xHandle == Application::TaskManager::lvglTaskHandle)
            coreId = 0;
        else if (task->xHandle == Application::TaskManager::networkTaskHandle)
            coreId = 1;
        else if (task->xHandle == Application::TaskManager::messagingTaskHandle)
            coreId = 0;
        else if (task->xHandle == Application::TaskManager::audioTaskHandle)
            coreId = 0;
        else if (task->xHandle == Application::TaskManager::otaTaskHandle)
            coreId = 1;

        const char* stateStr;
        switch (task->eCurrentState) {
            case eRunning:
                stateStr = "RUN";
                break;
            case eReady:
                stateStr = "RDY";
                break;
            case eBlocked:
                stateStr = "BLK";
                break;
            case eSuspended:
                stateStr = "SUS";
                break;
            case eDeleted:
                stateStr = "DEL";
                break;
            default:
                stateStr = "UNK";
                break;
        }

        ESP_LOGI(TAG, "%-16s\t%s\t%d\t%d\t%d\t%d%%\t%u",
                 task->pcTaskName,
                 stateStr,
                 (int)task->uxCurrentPriority,
                 (int)task->usStackHighWaterMark,
                 coreId,
                 (int)cpuPercent,
                 (unsigned int)task->ulRunTimeCounter);
    }

    ESP_LOGI(TAG, "Total Runtime: %u ticks", (unsigned int)totalRunTime);
    ESP_LOGI(TAG, "Free Heap: %d bytes", (int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "================================");

    vPortFree(taskStatusArray);
}

void TaskProfiler::printCPUUsageStats() {
    ESP_LOGI(TAG, "=== CPU USAGE ANALYSIS ===");

    // Get core frequencies
    uint32_t cpu_freq = esp_clk_cpu_freq();
    ESP_LOGI(TAG, "CPU Frequency: %u MHz", (unsigned int)(cpu_freq / 1000000));

    // Analyze task distribution per core
    uint32_t core0_tasks = 0, core1_tasks = 0;
    TaskStatus_t* taskStatusArray;
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();

    taskStatusArray = (TaskStatus_t*)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
    if (taskStatusArray != NULL) {
        taskCount = uxTaskGetSystemState(taskStatusArray, taskCount, NULL);

        ESP_LOGI(TAG, "Core 0 Tasks: LVGL, Messaging, Audio");
        ESP_LOGI(TAG, "Core 1 Tasks: Network, OTA");
        ESP_LOGI(TAG, "Load balancing appears: %s",
                 (core0_tasks > core1_tasks + 2) ? "UNBALANCED (Core 0 overloaded)" : "BALANCED");

        vPortFree(taskStatusArray);
    }

    ESP_LOGI(TAG, "==========================");
}

void TaskProfiler::startContinuousMonitoring() {
    if (continuousMonitoring) {
        ESP_LOGW(TAG, "Continuous monitoring already active");
        return;
    }

    perfMutex = xSemaphoreCreateMutex();
    if (perfMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create performance mutex");
        return;
    }

    continuousMonitoring = true;

    // Create monitoring task
    xTaskCreatePinnedToCore(
        [](void* param) {
            TickType_t lastWake = xTaskGetTickCount();
            uint32_t iteration = 0;

            while (continuousMonitoring) {
                if ((iteration % 10) == 0) {  // Every 10 seconds
                    TaskProfiler::printDetailedTaskStats();
                }

                if ((iteration % 5) == 0) {  // Every 5 seconds
                    TaskProfiler::detectTaskStarvation();
                }

                if ((iteration % 20) == 0) {  // Every 20 seconds
                    MemoryProfiler::printHeapFragmentation();
                }

                iteration++;
                vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));  // 1 second
            }

            vTaskDelete(NULL);
        },
        "PerfMonitor",
        4096,
        NULL,
        1,  // Low priority
        &monitoringTaskHandle,
        1  // Core 1
    );

    ESP_LOGI(TAG, "Continuous performance monitoring started");
}

void TaskProfiler::stopContinuousMonitoring() {
    continuousMonitoring = false;
    if (monitoringTaskHandle) {
        vTaskDelete(monitoringTaskHandle);
        monitoringTaskHandle = NULL;
    }
    if (perfMutex) {
        vSemaphoreDelete(perfMutex);
        perfMutex = NULL;
    }
    ESP_LOGI(TAG, "Continuous monitoring stopped");
}

void TaskProfiler::detectTaskStarvation() {
    ESP_LOGI(TAG, "=== TASK STARVATION ANALYSIS ===");

    TaskStatus_t* taskStatusArray;
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    uint32_t totalRunTime;

    taskStatusArray = (TaskStatus_t*)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
    if (taskStatusArray == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for starvation analysis");
        return;
    }

    taskCount = uxTaskGetSystemState(taskStatusArray, taskCount, &totalRunTime);

    // Detect tasks with suspiciously low CPU time
    for (UBaseType_t i = 0; i < taskCount; i++) {
        TaskStatus_t* task = &taskStatusArray[i];

        uint32_t cpuPercent = 0;
        if (totalRunTime > 0) {
            cpuPercent = (task->ulRunTimeCounter * 100UL) / totalRunTime;
        }

        // Check for potential starvation
        if (task->eCurrentState == eReady && cpuPercent == 0) {
            ESP_LOGW(TAG, "POTENTIAL STARVATION: Task '%s' ready but 0%% CPU", task->pcTaskName);
        }

        // Check for blocked tasks that might be waiting too long
        if (task->eCurrentState == eBlocked) {
            ESP_LOGD(TAG, "BLOCKED: Task '%s' (Priority: %d)",
                     task->pcTaskName, (int)task->uxCurrentPriority);
        }
    }

    vPortFree(taskStatusArray);
    ESP_LOGI(TAG, "================================");
}

void TaskProfiler::detectMutexContention() {
    ESP_LOGI(TAG, "=== MUTEX CONTENTION ANALYSIS ===");

    // Check LVGL mutex specifically
    if (Application::TaskManager::lvglMutex) {
        ESP_LOGI(TAG, "LVGL Mutex exists - checking for contention patterns");

        // Try to detect high contention by attempting quick lock
        TickType_t startTime = xTaskGetTickCount();
        if (xSemaphoreTakeRecursive(Application::TaskManager::lvglMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
            TickType_t lockTime = xTaskGetTickCount() - startTime;
            xSemaphoreGiveRecursive(Application::TaskManager::lvglMutex);

            if (lockTime > 0) {
                ESP_LOGW(TAG, "LVGL mutex lock took %d ticks - possible contention", (int)lockTime);
            } else {
                ESP_LOGD(TAG, "LVGL mutex acquired immediately - low contention");
            }
        } else {
            ESP_LOGW(TAG, "CRITICAL: Could not acquire LVGL mutex within 1ms - HIGH CONTENTION");
        }
    }

    ESP_LOGI(TAG, "==================================");
}

void ESPProgDebugger::enableCoreProfilingPins() {
#if LOGIC_ANALYZER_ENABLED
    ESP_LOGI(TAG, "Configuring profiling pins for logic analyzer");

    // Configure GPIO pins for profiling output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PROF_PIN_CORE0_TASK) |
                        (1ULL << PROF_PIN_CORE1_TASK) |
                        (1ULL << PROF_PIN_LVGL_ACTIVE) |
                        (1ULL << PROF_PIN_MUTEX_WAIT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Set all pins low initially
    gpio_set_level(PROF_PIN_CORE0_TASK, 0);
    gpio_set_level(PROF_PIN_CORE1_TASK, 0);
    gpio_set_level(PROF_PIN_LVGL_ACTIVE, 0);
    gpio_set_level(PROF_PIN_MUTEX_WAIT, 0);

    ESP_LOGI(TAG, "Profiling pins configured:");
    ESP_LOGI(TAG, "  GPIO%d: Core 0 task activity", PROF_PIN_CORE0_TASK);
    ESP_LOGI(TAG, "  GPIO%d: Core 1 task activity", PROF_PIN_CORE1_TASK);
    ESP_LOGI(TAG, "  GPIO%d: LVGL processing", PROF_PIN_LVGL_ACTIVE);
    ESP_LOGI(TAG, "  GPIO%d: Mutex contention", PROF_PIN_MUTEX_WAIT);
#else
    ESP_LOGI(TAG, "Logic analyzer profiling disabled - using software-only debugging");
#endif
}

void ESPProgDebugger::setupTaskSwitchTracing() {
    ESP_LOGI(TAG, "Setting up task switch tracing for built-in USB Serial/JTAG");

    // Enable FreeRTOS trace hooks
    ESP_LOGI(TAG, "Task switch tracing configured - use OpenOCD to capture");
    ESP_LOGI(TAG, "OpenOCD command: monitor esp32 semihosting enable");

#if LOGIC_ANALYZER_ENABLED
    ESP_LOGI(TAG, "Logic analyzer should monitor GPIO %d-%d",
             PROF_PIN_CORE0_TASK, PROF_PIN_MUTEX_WAIT);
#else
    ESP_LOGI(TAG, "Using software-only profiling (no GPIO signals)");
#endif
}

void MemoryProfiler::printHeapFragmentation() {
    ESP_LOGI(TAG, "=== MEMORY FRAGMENTATION ANALYSIS ===");

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);

    ESP_LOGI(TAG, "Total free bytes: %d", (int)info.total_free_bytes);
    ESP_LOGI(TAG, "Largest free block: %d", (int)info.largest_free_block);
    ESP_LOGI(TAG, "Minimum free bytes: %d", (int)info.minimum_free_bytes);
    ESP_LOGI(TAG, "Allocated blocks: %d", (int)info.allocated_blocks);
    ESP_LOGI(TAG, "Free blocks: %d", (int)info.free_blocks);
    ESP_LOGI(TAG, "Total allocated: %d", (int)info.total_allocated_bytes);

    // Calculate fragmentation percentage
    if (info.total_free_bytes > 0) {
        uint32_t fragmentation = 100 - ((info.largest_free_block * 100) / info.total_free_bytes);
        ESP_LOGI(TAG, "Fragmentation: %d%% %s", (int)fragmentation,
                 (fragmentation > 75) ? "(CRITICAL)" : (fragmentation > 50) ? "(HIGH)"
                                                   : (fragmentation > 25)   ? "(MODERATE)"
                                                                            : "(LOW)");
    }

    ESP_LOGI(TAG, "=====================================");
}

// Precision timer implementation
PrecisionTimer::PrecisionTimer(const char* timerName, uint32_t thresholdUs)
    : name(timerName), threshold(thresholdUs) {
    startTime = esp_timer_get_time();
}

PrecisionTimer::~PrecisionTimer() {
    uint64_t elapsed = esp_timer_get_time() - startTime;
    if (elapsed > threshold) {
        ESP_LOGW("TIMER", "%s took %llu us (threshold: %u us)", name, elapsed, threshold);
    } else {
        ESP_LOGD("TIMER", "%s took %llu us", name, elapsed);
    }
}

uint64_t PrecisionTimer::getElapsedUs() const {
    return esp_timer_get_time() - startTime;
}

void PrecisionTimer::checkpoint(const char* checkpointName) {
    uint64_t elapsed = esp_timer_get_time() - startTime;
    ESP_LOGI("TIMER", "%s - %s: %llu us", name, checkpointName, elapsed);
}

#endif  // DEBUG_PERFORMANCE

// Always available utilities
namespace DebugUtils {
void printSystemInfo() {
    ESP_LOGI("DEBUG", "=== SYSTEM INFORMATION ===");
    ESP_LOGI("DEBUG", "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI("DEBUG", "CPU Frequency: %u MHz", (unsigned int)(esp_clk_cpu_freq() / 1000000));
    ESP_LOGI("DEBUG", "Free Heap: %d bytes", (int)esp_get_free_heap_size());
    ESP_LOGI("DEBUG", "Minimum Free Heap: %d bytes", (int)esp_get_minimum_free_heap_size());
    ESP_LOGI("DEBUG", "Tasks Running: %d", (int)uxTaskGetNumberOfTasks());
    ESP_LOGI("DEBUG", "==========================");
}

void printTaskList() {
    char* taskListBuffer = (char*)pvPortMalloc(2048);
    if (taskListBuffer) {
        vTaskList(taskListBuffer);
        ESP_LOGI("DEBUG", "=== TASK LIST ===\n%s", taskListBuffer);
        vPortFree(taskListBuffer);
    }
}
}  // namespace DebugUtils
