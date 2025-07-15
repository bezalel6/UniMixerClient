#pragma once

#include <esp_log.h>

/**
 * BSOD (Blue Screen of Death) Handler for ESP32
 * 
 * Provides dead-simple critical failure handling with zero setup required.
 * Just include this header and use the macros.
 * 
 * Usage:
 *   INIT_CRITICAL(Hardware::SD::init(), "SD Card not detected");
 *   ASSERT_CRITICAL(ptr != nullptr, "Memory allocation failed");
 *   CRITICAL_FAILURE("System watchdog timeout");
 */

namespace BSODHandler {
    // Core function to display BSOD screen
    void show(const char* message, const char* file = nullptr, int line = 0);
    
    // Initialize BSOD capability (called early in boot)
    bool init();
    
    // Check if BSOD system is ready
    bool isReady();
}

// Critical assertion - shows BSOD if condition is false
#define ASSERT_CRITICAL(condition, message) \
    do { \
        if (!(condition)) { \
            ESP_LOGE("CRITICAL", "Assertion failed: %s", #condition); \
            BSODHandler::show(message, __FILE__, __LINE__); \
        } \
    } while(0)

// Direct critical failure trigger
#define CRITICAL_FAILURE(message) \
    do { \
        ESP_LOGE("CRITICAL", "Critical failure triggered"); \
        BSODHandler::show(message, __FILE__, __LINE__); \
    } while(0)

// Critical initialization wrapper
#define INIT_CRITICAL(expr, failure_msg) \
    do { \
        ESP_LOGI("BOOT", "Critical init: %s", #expr); \
        if (!(expr)) { \
            ESP_LOGE("CRITICAL", "Init failed: %s", #expr); \
            BSODHandler::show(failure_msg, __FILE__, __LINE__); \
        } \
    } while(0)

// Optional initialization (logs warning but continues)
#define INIT_OPTIONAL(expr, component_name) \
    do { \
        ESP_LOGI("BOOT", "Optional init: %s", component_name); \
        if (!(expr)) { \
            ESP_LOGW("BOOT", "%s initialization failed - continuing without it", component_name); \
        } else { \
            ESP_LOGI("BOOT", "%s initialized successfully", component_name); \
        } \
    } while(0)

// Hook for ESP panic handler
extern "C" void bsod_panic_handler(const char* reason);