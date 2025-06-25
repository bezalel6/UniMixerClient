#pragma once

#include "MessagingConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>

// Debug mode toggle macros
#if MESSAGING_DESERIALIZATION_DEBUG_MODE
#define DEBUG_MODE_ENABLED true
#define DEBUG_LOG(msg) ESP_LOGI("DEBUG", "[DEBUG MODE] " msg)
#define DEBUG_LOG_F(fmt, ...) ESP_LOGI("DEBUG", "[DEBUG MODE] " fmt, __VA_ARGS__)
#else
#define DEBUG_MODE_ENABLED false
#define DEBUG_LOG(msg)
#define DEBUG_LOG_F(fmt, ...)
#endif

// UI Debug logging macro
#define LOG_TO_UI(ui_element, message)                                    \
    if (ui_element != nullptr && message != nullptr) {                    \
        String timestamp = String("[") + String(millis()) + String("] "); \
        String logEntry = timestamp + message + "\n";                     \
        lv_textarea_add_text(ui_element, logEntry.c_str());               \
        const char* current_text = lv_textarea_get_text(ui_element);      \
        if (strlen(current_text) > 5000) {                                \
            String truncated = String(current_text).substring(1500);      \
            lv_textarea_set_text(ui_element, truncated.c_str());          \
        }                                                                 \
        /* Auto-scroll to bottom after adding text */                     \
        lv_textarea_set_cursor_pos(ui_element, LV_TEXTAREA_CURSOR_LAST);  \
    }

// Enhanced logging levels
#define LOG_SERIAL_RX(msg) ESP_LOGI("SerialRX", "RX: %s", msg)
#define LOG_SERIAL_TX(msg) ESP_LOGI("SerialTX", "TX: %s", msg)
#define LOG_JSON_PARSE_OK(keys) ESP_LOGI("JSON", "Parse OK - %d keys", keys)
#define LOG_JSON_PARSE_ERROR(error) ESP_LOGW("JSON", "Parse Error: %s", error)

// Runtime debug mode toggle (for cases where compile-time isn't enough)
extern bool runtime_debug_mode_enabled;

// Helper functions for debug mode control
inline void EnableDebugMode() { runtime_debug_mode_enabled = true; }
inline void DisableDebugMode() { runtime_debug_mode_enabled = false; }
inline bool IsDebugModeEnabled() {
    return DEBUG_MODE_ENABLED || runtime_debug_mode_enabled;
}

#ifdef DEBUG_PERFORMANCE

// Logic analyzer GPIO profiling control (set to 1 to enable GPIO signals)
#define LOGIC_ANALYZER_ENABLED 0

// Performance profiling macros (renamed to avoid LWIP conflicts)
#define PERF_TIMER_START(name)              \
    static uint64_t _perf_start_##name = 0; \
    _perf_start_##name = esp_timer_get_time()

#define PERF_TIMER_END(name, threshold_us)                                                                   \
    do {                                                                                                     \
        uint64_t _perf_duration_##name = esp_timer_get_time() - _perf_start_##name;                          \
        if (_perf_duration_##name > threshold_us) {                                                          \
            ESP_LOGW("PERF", #name " took %llu us (threshold: %d us)", _perf_duration_##name, threshold_us); \
        }                                                                                                    \
    } while (0)

// Task monitoring utilities
class TaskProfiler {
   public:
    static void printDetailedTaskStats();
    static void printCPUUsageStats();
    static void printMemoryStats();
    static void printStackUsage();
    static void startContinuousMonitoring();
    static void stopContinuousMonitoring();

    // Real-time task analysis
    static void analyzeTaskSwitching();
    static void detectTaskStarvation();
    static void measureInterruptLatency();

    // Performance bottleneck detection
    static void detectMutexContention();
    static void analyzeQueuePerformance();
    static void measureLVGLPerformance();
};

// Memory debugging utilities
class MemoryProfiler {
   public:
    static void printHeapFragmentation();
    static void trackAllocation(void* ptr, size_t size, const char* location);
    static void trackDeallocation(void* ptr, const char* location);
    static void printAllocationReport();
    static void detectMemoryLeaks();
};

// ESP-PROG specific debugging utilities
class ESPProgDebugger {
   public:
    static void setupBreakpoints();
    static void configureSampling();
    static void startTracing();
    static void stopTracing();
    static void dumpTraceData();

    // Real-time analysis
    static void enableCoreProfilingPins();
    static void setupTaskSwitchTracing();
    static void configurePerformanceCounters();
};

// Timing utilities for precise measurements
class PrecisionTimer {
   private:
    uint64_t startTime;
    const char* name;
    uint32_t threshold;

   public:
    PrecisionTimer(const char* timerName, uint32_t thresholdUs = 1000);
    ~PrecisionTimer();

    uint64_t getElapsedUs() const;
    void checkpoint(const char* checkpointName);
};

// Thread-safe performance counters (TODO: Implementation needed)
// class PerformanceCounters {
//    private:
//     static SemaphoreHandle_t counterMutex;
//
//    public:
//     static void init();
//     static void incrementCounter(const char* name);
//     static void setGauge(const char* name, uint32_t value);
//     static void printCounters();
//     static void resetCounters();
// };

#else
// No-op macros when debugging is disabled
#define PERF_TIMER_START(name) \
    do {                       \
    } while (0)
#define PERF_TIMER_END(name, threshold_us) \
    do {                                   \
    } while (0)
#endif

// Always available utilities
namespace DebugUtils {
void printSystemInfo();
void printTaskList();
void printFreeMemory();
void enableWatchdog(uint32_t timeoutSeconds);
void feedWatchdog();
}  // namespace DebugUtils
