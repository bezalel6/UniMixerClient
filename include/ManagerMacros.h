#pragma once

/**
 * @file ManagerMacros.h
 * @brief Common macros for manager classes to reduce boilerplate and ensure consistency
 *
 * This header provides macros for:
 * - Initialization guards and validation
 * - Mutex handling with automatic timeout and logging
 * - Conditional logging based on log levels
 * - Resource cleanup
 * - Parameter validation
 * - Performance measurement
 *
 * All logging respects ESP-IDF's log level configuration.
 *
 * Basic usage:
 *   INIT_GUARD("MyManager", initialized, TAG);                    // Prevent double init
 *   MUTEX_GUARD(myMutex, 5000, TAG, "operation", false);         // Acquire with timeout
 *   VALIDATE_PARAM(ptr, TAG, "pointer_name", false);             // Check null pointers
 *   LOG_WARN_IF(condition, TAG, "Warning: %s", message);         // Conditional logging
 *   CLEANUP_SEMAPHORE(myMutex, TAG, "resource");                 // Safe cleanup
 */

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// =============================================================================
// CONFIGURATION MACROS
// =============================================================================

// Default timeout for mutex operations (ms)
#ifndef MANAGER_DEFAULT_MUTEX_TIMEOUT_MS
#define MANAGER_DEFAULT_MUTEX_TIMEOUT_MS 5000
#endif

// Default timeout for quick mutex operations (ms)
#ifndef MANAGER_QUICK_MUTEX_TIMEOUT_MS
#define MANAGER_QUICK_MUTEX_TIMEOUT_MS 100
#endif

// =============================================================================
// INITIALIZATION GUARD MACROS
// =============================================================================

/**
 * Guard against double initialization
 * @param component_name Human-readable component name for logging
 * @param initialized_flag Boolean variable name that tracks initialization state
 * @param tag LOG tag to use
 * @return Returns true and logs warning if already initialized
 */
#define INIT_GUARD(component_name, initialized_flag, tag)            \
    do {                                                             \
        if (initialized_flag) {                                      \
            ESP_LOGW(tag, "%s already initialized", component_name); \
            return true;                                             \
        }                                                            \
    } while (0)

/**
 * Guard against operations on uninitialized components
 * @param component_name Human-readable component name for logging
 * @param initialized_flag Boolean variable name that tracks initialization state
 * @param tag LOG tag to use
 * @param return_value Value to return if not initialized
 */
#define REQUIRE_INIT(component_name, initialized_flag, tag, return_value)             \
    do {                                                                              \
        if (!initialized_flag) {                                                      \
            ESP_LOGW(tag, "%s not initialized - operation rejected", component_name); \
            return return_value;                                                      \
        }                                                                             \
    } while (0)

/**
 * Guard for void functions against operations on uninitialized components
 */
#define REQUIRE_INIT_VOID(component_name, initialized_flag, tag)                      \
    do {                                                                              \
        if (!initialized_flag) {                                                      \
            ESP_LOGW(tag, "%s not initialized - operation rejected", component_name); \
            return;                                                                   \
        }                                                                             \
    } while (0)

// =============================================================================
// MUTEX HANDLING MACROS
// =============================================================================

/**
 * Acquire mutex with timeout and automatic logging
 * @param mutex_var SemaphoreHandle_t variable name
 * @param timeout_ms Timeout in milliseconds
 * @param tag LOG tag for error logging
 * @param action_name Human-readable action name for logging
 * @param return_value Value to return on failure
 */
#define MUTEX_GUARD(mutex_var, timeout_ms, tag, action_name, return_value)                        \
    if (!mutex_var || xSemaphoreTake(mutex_var, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {           \
        ESP_LOGW(tag, "Failed to acquire mutex for %s (timeout: %dms)", action_name, timeout_ms); \
        return return_value;                                                                      \
    }

/**
 * Acquire mutex with timeout for void functions
 */
#define MUTEX_GUARD_VOID(mutex_var, timeout_ms, tag, action_name)                                 \
    if (!mutex_var || xSemaphoreTake(mutex_var, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {           \
        ESP_LOGW(tag, "Failed to acquire mutex for %s (timeout: %dms)", action_name, timeout_ms); \
        return;                                                                                   \
    }

/**
 * Quick mutex guard with default short timeout for non-blocking operations
 */
#define MUTEX_QUICK_GUARD(mutex_var, tag, action_name, return_value) \
    MUTEX_GUARD(mutex_var, MANAGER_QUICK_MUTEX_TIMEOUT_MS, tag, action_name, return_value)

#define MUTEX_QUICK_GUARD_VOID(mutex_var, tag, action_name) \
    MUTEX_GUARD_VOID(mutex_var, MANAGER_QUICK_MUTEX_TIMEOUT_MS, tag, action_name)

/**
 * Release mutex with logging on success
 */
#define MUTEX_RELEASE(mutex_var, tag, action_name)               \
    do {                                                         \
        if (mutex_var) {                                         \
            xSemaphoreGive(mutex_var);                           \
            ESP_LOGV(tag, "Released mutex for %s", action_name); \
        }                                                        \
    } while (0)

/**
 * Create a scoped mutex guard that automatically releases on scope exit
 * Usage: SCOPED_MUTEX_GUARD(myMutex, 1000, "myTag", "operation");
 */
#define SCOPED_MUTEX_GUARD(mutex_var, timeout_ms, tag, action_name)                         \
    struct MutexGuard_##__LINE__ {                                                          \
        SemaphoreHandle_t m;                                                                \
        const char* t;                                                                      \
        const char* a;                                                                      \
        bool acquired;                                                                      \
        MutexGuard_##__LINE__(SemaphoreHandle_t mutex, const char* tag, const char* action) \
            : m(mutex), t(tag), a(action), acquired(false) {                                \
            if (m && xSemaphoreTake(m, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {              \
                acquired = true;                                                            \
                ESP_LOGV(t, "Acquired scoped mutex for %s", a);                             \
            } else {                                                                        \
                ESP_LOGW(t, "Failed to acquire scoped mutex for %s", a);                    \
            }                                                                               \
        }                                                                                   \
        ~MutexGuard_##__LINE__() {                                                          \
            if (acquired && m) {                                                            \
                xSemaphoreGive(m);                                                          \
                ESP_LOGV(t, "Released scoped mutex for %s", a);                             \
            }                                                                               \
        }                                                                                   \
        bool isAcquired() const { return acquired; }                                        \
    } mutexGuard_##__LINE__(mutex_var, tag, action_name);                                   \
    if (!mutexGuard_##__LINE__.isAcquired())

// =============================================================================
// CONDITIONAL LOGGING MACROS
// =============================================================================

/**
 * Log error only if condition is true
 */
#define LOG_ERROR_IF(condition, tag, format, ...) \
    do {                                          \
        if (condition) {                          \
            ESP_LOGE(tag, format, ##__VA_ARGS__); \
        }                                         \
    } while (0)

/**
 * Log warning only if condition is true
 */
#define LOG_WARN_IF(condition, tag, format, ...)  \
    do {                                          \
        if (condition) {                          \
            ESP_LOGW(tag, format, ##__VA_ARGS__); \
        }                                         \
    } while (0)

/**
 * Log info only if condition is true
 */
#define LOG_INFO_IF(condition, tag, format, ...)  \
    do {                                          \
        if (condition) {                          \
            ESP_LOGI(tag, format, ##__VA_ARGS__); \
        }                                         \
    } while (0)

/**
 * Log debug only if condition is true
 */
#define LOG_DEBUG_IF(condition, tag, format, ...) \
    do {                                          \
        if (condition) {                          \
            ESP_LOGD(tag, format, ##__VA_ARGS__); \
        }                                         \
    } while (0)

// =============================================================================
// SINGLETON PATTERN MACROS
// =============================================================================

/**
 * Standard singleton getInstance implementation
 * @param ClassName The class name for the singleton
 */
#define SINGLETON_INSTANCE(ClassName)     \
    ClassName& ClassName::getInstance() { \
        static ClassName instance;        \
        return instance;                  \
    }

/**
 * Singleton getInstance declaration for header files
 */
#define SINGLETON_DECLARATION(ClassName) \
    static ClassName& getInstance();

// =============================================================================
// RESOURCE CLEANUP MACROS
// =============================================================================

/**
 * Safe cleanup of semaphore/mutex with logging
 */
#define CLEANUP_SEMAPHORE(semaphore_var, tag, resource_name)         \
    do {                                                             \
        if (semaphore_var) {                                         \
            vSemaphoreDelete(semaphore_var);                         \
            semaphore_var = nullptr;                                 \
            ESP_LOGD(tag, "Cleaned up %s semaphore", resource_name); \
        }                                                            \
    } while (0)

/**
 * Safe cleanup of allocated memory with logging
 */
#define CLEANUP_MEMORY(ptr_var, tag, resource_name)               \
    do {                                                          \
        if (ptr_var) {                                            \
            free(ptr_var);                                        \
            ptr_var = nullptr;                                    \
            ESP_LOGV(tag, "Cleaned up %s memory", resource_name); \
        }                                                         \
    } while (0)

// =============================================================================
// STATUS AND VALIDATION MACROS
// =============================================================================

/**
 * Validate pointer parameter and return with error if null
 */
#define VALIDATE_PARAM(param, tag, param_name, return_value)            \
    do {                                                                \
        if (!param) {                                                   \
            ESP_LOGW(tag, "Invalid parameter: %s is null", param_name); \
            return return_value;                                        \
        }                                                               \
    } while (0)

#define VALIDATE_PARAM_VOID(param, tag, param_name)                     \
    do {                                                                \
        if (!param) {                                                   \
            ESP_LOGW(tag, "Invalid parameter: %s is null", param_name); \
            return;                                                     \
        }                                                               \
    } while (0)

/**
 * Check if system/service is healthy before proceeding
 */
#define REQUIRE_HEALTHY(health_check, tag, service_name, return_value)             \
    do {                                                                           \
        if (!(health_check)) {                                                     \
            ESP_LOGW(tag, "%s is not healthy - operation rejected", service_name); \
            return return_value;                                                   \
        }                                                                          \
    } while (0)

#define REQUIRE_HEALTHY_VOID(health_check, tag, service_name)                      \
    do {                                                                           \
        if (!(health_check)) {                                                     \
            ESP_LOGW(tag, "%s is not healthy - operation rejected", service_name); \
            return;                                                                \
        }                                                                          \
    } while (0)

// =============================================================================
// TIMING AND PERFORMANCE MACROS
// =============================================================================

/**
 * Simple performance measurement macro
 * Usage: PERF_MEASURE("operation_name", tag) { - code to measure - }
 */
#define PERF_MEASURE(operation_name, tag)                     \
    for (unsigned long _start_time = millis(), _measured = 0; \
         !_measured;                                          \
         _measured = 1, ESP_LOGD(tag, "%s took %lu ms", operation_name, millis() - _start_time))

/**
 * Performance measurement with threshold warning
 */
#define PERF_MEASURE_WARN(operation_name, tag, threshold_ms)                                                                  \
    for (unsigned long _start_time = millis(), _measured = 0;                                                                 \
         !_measured;                                                                                                          \
         _measured = 1, ({                                                                                                    \
             unsigned long _duration = millis() - _start_time;                                                                \
             if (_duration > threshold_ms) {                                                                                  \
                 ESP_LOGW(tag, "%s took %lu ms (threshold: %lu ms)", operation_name, _duration, (unsigned long)threshold_ms); \
             } else {                                                                                                         \
                 ESP_LOGD(tag, "%s took %lu ms", operation_name, _duration);                                                  \
             }                                                                                                                \
         }))
