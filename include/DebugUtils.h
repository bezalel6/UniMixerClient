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

// No-op macros when debugging is disabled
#define PERF_TIMER_START(name) \
    do {                       \
    } while (0)
#define PERF_TIMER_END(name, threshold_us) \
    do {                                   \
    } while (0)
// Always available utilities
namespace DebugUtils {
void printSystemInfo();
void printTaskList();
void printFreeMemory();
void enableWatchdog(uint32_t timeoutSeconds);
void feedWatchdog();
}  // namespace DebugUtils
