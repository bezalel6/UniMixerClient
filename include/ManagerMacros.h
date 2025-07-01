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

// =============================================================================
// UI EVENT REGISTRATION MACROS
// =============================================================================

/**
 * Basic event callback registration
 */
#define REGISTER_EVENT_CB(widget, handler, event) \
    lv_obj_add_event_cb(widget, handler, event, NULL)

/**
 * Safe event callback registration with null check and logging
 */
#define REGISTER_EVENT_CB_SAFE(widget, handler, event, description)    \
    do {                                                               \
        if (widget) {                                                  \
            lv_obj_add_event_cb(widget, handler, event, NULL);         \
            ESP_LOGI(TAG, description " registered");                  \
        } else {                                                       \
            ESP_LOGW(TAG, #widget " is null - skipping " description); \
        }                                                              \
    } while (0)

/**
 * Click event registration with automatic clickable flag setting
 */
#define SETUP_CLICK_EVENT(widget, handler, description)                   \
    do {                                                                  \
        if (widget) {                                                     \
            lv_obj_add_flag(widget, LV_OBJ_FLAG_CLICKABLE);               \
            lv_obj_add_event_cb(widget, handler, LV_EVENT_CLICKED, NULL); \
            ESP_LOGD(TAG, description " click handler registered");       \
        } else {                                                          \
            ESP_LOGW(TAG, #widget " is null - skipping " description);    \
        }                                                                 \
    } while (0)

/**
 * Setup multiple click events with same handler
 */
#define SETUP_CLICK_EVENTS(handler, description, ...)                                                                \
    do {                                                                                                             \
        lv_obj_t* widgets[] = {__VA_ARGS__};                                                                         \
        for (size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++) {                                          \
            if (widgets[i]) {                                                                                        \
                lv_obj_add_flag(widgets[i], LV_OBJ_FLAG_CLICKABLE);                                                  \
                lv_obj_add_event_cb(widgets[i], handler, LV_EVENT_CLICKED, NULL);                                    \
            }                                                                                                        \
        }                                                                                                            \
        ESP_LOGD(TAG, description " click handlers registered (%zu widgets)", sizeof(widgets) / sizeof(widgets[0])); \
    } while (0)

/**
 * Setup multiple value change events with same handler
 */
#define SETUP_VALUE_CHANGE_EVENTS(handler, description, ...)                                                                \
    do {                                                                                                                    \
        lv_obj_t* widgets[] = {__VA_ARGS__};                                                                                \
        for (size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++) {                                                 \
            if (widgets[i]) {                                                                                               \
                lv_obj_add_event_cb(widgets[i], handler, LV_EVENT_VALUE_CHANGED, NULL);                                     \
            }                                                                                                               \
        }                                                                                                                   \
        ESP_LOGD(TAG, description " value change handlers registered (%zu widgets)", sizeof(widgets) / sizeof(widgets[0])); \
    } while (0)

/**
 * Setup volume slider with both visual and change handlers
 */
#define SETUP_VOLUME_SLIDER(slider, visual_handler, change_handler)                    \
    do {                                                                               \
        if (slider) {                                                                  \
            lv_obj_add_event_cb(slider, visual_handler, LV_EVENT_VALUE_CHANGED, NULL); \
            lv_obj_add_event_cb(slider, change_handler, LV_EVENT_RELEASED, NULL);      \
            ESP_LOGD(TAG, #slider " volume handlers registered");                      \
        } else {                                                                       \
            ESP_LOGW(TAG, #slider " is null - skipping volume setup");                 \
        }                                                                              \
    } while (0)

/**
 * Setup all volume sliders at once
 */
#define SETUP_ALL_VOLUME_SLIDERS(visual_handler, change_handler)                     \
    do {                                                                             \
        SETUP_VOLUME_SLIDER(ui_primaryVolumeSlider, visual_handler, change_handler); \
        SETUP_VOLUME_SLIDER(ui_singleVolumeSlider, visual_handler, change_handler);  \
        SETUP_VOLUME_SLIDER(ui_balanceVolumeSlider, visual_handler, change_handler); \
        ESP_LOGI(TAG, "All volume sliders configured");                              \
    } while (0)

/**
 * Setup all audio dropdowns at once
 */
#define SETUP_ALL_AUDIO_DROPDOWNS(handler)                                                             \
    do {                                                                                               \
        SETUP_VALUE_CHANGE_EVENTS(handler, "Audio dropdown",                                           \
                                  ui_selectAudioDevice, ui_selectAudioDevice1, ui_selectAudioDevice2); \
        ESP_LOGI(TAG, "All audio dropdowns configured");                                               \
    } while (0)

/**
 * Setup tab events with both tabview and individual button handlers
 */
#define SETUP_TAB_EVENTS(tabview, handler)                                                \
    do {                                                                                  \
        ESP_LOGI(TAG, "Registering tab events on " #tabview ": %p", tabview);             \
        if (tabview) {                                                                    \
            lv_obj_add_event_cb(tabview, handler, LV_EVENT_VALUE_CHANGED, NULL);          \
                                                                                          \
            lv_obj_t* tab_buttons = lv_tabview_get_tab_bar(tabview);                      \
            if (tab_buttons) {                                                            \
                uint32_t tab_count = lv_obj_get_child_count(tab_buttons);                 \
                ESP_LOGI(TAG, "Found %d tab buttons in " #tabview, tab_count);            \
                                                                                          \
                for (uint32_t i = 0; i < tab_count; i++) {                                \
                    lv_obj_t* tab_button = lv_obj_get_child(tab_buttons, i);              \
                    if (tab_button) {                                                     \
                        lv_obj_add_flag(tab_button, LV_OBJ_FLAG_CLICKABLE);               \
                        lv_obj_add_event_cb(tab_button, handler, LV_EVENT_CLICKED, NULL); \
                    }                                                                     \
                }                                                                         \
                ESP_LOGI(TAG, "Tab events configured for %d buttons", tab_count);         \
            }                                                                             \
        } else {                                                                          \
            ESP_LOGW(TAG, #tabview " is null - skipping tab setup");                      \
        }                                                                                 \
    } while (0)

/**
 * Setup file explorer navigation buttons
 * NOTE: FileExplorer has been removed from the project
 */
#define SETUP_FILE_EXPLORER_NAVIGATION()                                           \
    do {                                                                           \
        ESP_LOGI(TAG, "FileExplorer removed - no navigation setup needed");        \
        /* FileExplorer and related handlers have been removed from the project */ \
    } while (0)

/**
 * Register multiple events on the same widget
 */
#define REGISTER_MULTIPLE_EVENTS(widget, handler, ...)                    \
    do {                                                                  \
        lv_event_code_t events[] = {__VA_ARGS__};                         \
        for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) { \
            lv_obj_add_event_cb(widget, handler, events[i], NULL);        \
        }                                                                 \
    } while (0)

/**
 * Register the same handler and event on multiple widgets
 */
#define REGISTER_BULK_EVENTS(handler, event, ...)                           \
    do {                                                                    \
        lv_obj_t* widgets[] = {__VA_ARGS__};                                \
        for (size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); i++) { \
            if (widgets[i]) {                                               \
                REGISTER_EVENT_CB(widgets[i], handler, event);              \
            }                                                               \
        }                                                                   \
    } while (0)

// =============================================================================
// AUDIO MANAGER SPECIFIC MACROS
// =============================================================================

/**
 * Validate device selection for current tab context
 */
#define VALIDATE_DEVICE_SELECTION(device_ptr, tab_name, return_value) \
    do {                                                              \
        if (!device_ptr) {                                            \
            ESP_LOGW(TAG, "No device selected for %s tab", tab_name); \
            return return_value;                                      \
        }                                                             \
    } while (0)

#define VALIDATE_DEVICE_SELECTION_VOID(device_ptr, tab_name)          \
    do {                                                              \
        if (!device_ptr) {                                            \
            ESP_LOGW(TAG, "No device selected for %s tab", tab_name); \
            return;                                                   \
        }                                                             \
    } while (0)

/**
 * Execute operation on device with fallback for default device
 */
#define EXECUTE_DEVICE_OPERATION(device_name, operation, default_fallback) \
    do {                                                                   \
        if (device_name.isEmpty()) {                                       \
            default_fallback;                                              \
        } else {                                                           \
            operation;                                                     \
        }                                                                  \
    } while (0)

/**
 * Notify state change only if device selection actually changed
 */
#define NOTIFY_STATE_CHANGE_IF_DIFFERENT(old_device, new_device, change_type)  \
    do {                                                                       \
        if (old_device != new_device) {                                        \
            String deviceName = new_device ? new_device->processName : "";     \
            notifyStateChange(AudioStateChangeEvent::change_type(deviceName)); \
        }                                                                      \
    } while (0)

/**
 * Validate both balance devices are available
 */
#define VALIDATE_BALANCE_DEVICES(device1, device2, return_value)      \
    do {                                                              \
        if (!device1 || !device2) {                                   \
            ESP_LOGW(TAG, "Balance operation requires both devices"); \
            return return_value;                                      \
        }                                                             \
    } while (0)

#define VALIDATE_BALANCE_DEVICES_VOID(device1, device2)               \
    do {                                                              \
        if (!device1 || !device2) {                                   \
            ESP_LOGW(TAG, "Balance operation requires both devices"); \
            return;                                                   \
        }                                                             \
    } while (0)

/**
 * Update balance device selection with validation
 */
#define UPDATE_BALANCE_SELECTION(device1_name, device2_name)                        \
    do {                                                                            \
        AudioLevel* dev1 = state.findDevice(device1_name);                          \
        AudioLevel* dev2 = state.findDevice(device2_name);                          \
        if (dev1 && dev2) {                                                         \
            state.selectedDevice1 = dev1;                                           \
            state.selectedDevice2 = dev2;                                           \
            ESP_LOGI(TAG, "Updated balance selection: %s, %s",                      \
                     device1_name.c_str(), device2_name.c_str());                   \
        } else {                                                                    \
            ESP_LOGW(TAG, "Failed to update balance selection: devices not found"); \
        }                                                                           \
    } while (0)

/**
 * Distribute volume across balance devices with ratio
 */
#define BALANCE_VOLUME_DISTRIBUTE(volume, device1, device2, balance_ratio) \
    do {                                                                   \
        VALIDATE_BALANCE_DEVICES_VOID(device1, device2);                   \
        int clampedVolume = constrain(volume, 0, 100);                     \
        float ratio = constrain(balance_ratio, -1.0f, 1.0f);               \
        int volume1 = clampedVolume * (1.0f - ratio) * 0.5f;               \
        int volume2 = clampedVolume * (1.0f + ratio) * 0.5f;               \
        device1->volume = constrain(volume1, 0, 100);                      \
        device2->volume = constrain(volume2, 0, 100);                      \
        ESP_LOGI(TAG, "Balance distribute: %d -> dev1:%d, dev2:%d",        \
                 clampedVolume, device1->volume, device2->volume);         \
    } while (0)

// =============================================================================
// LVGL STYLING MACROS - Eliminate repetitive styling
// =============================================================================

/**
 * Set common object size and position in one call
 */
#define LVGL_SET_SIZE_POS(obj, w, h, x, y) \
    do {                                   \
        if (obj) {                         \
            lv_obj_set_size(obj, w, h);    \
            lv_obj_set_pos(obj, x, y);     \
        }                                  \
    } while (0)

/**
 * Set common object size and alignment
 */
#define LVGL_SET_SIZE_ALIGN(obj, w, h, align) \
    do {                                      \
        if (obj) {                            \
            lv_obj_set_size(obj, w, h);       \
            lv_obj_set_align(obj, align);     \
        }                                     \
    } while (0)

/**
 * Apply common button styling with color
 */
#define LVGL_STYLE_BUTTON(btn, bg_color, text_color)                                  \
    do {                                                                              \
        if (btn) {                                                                    \
            lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), LV_PART_MAIN);     \
            lv_obj_set_style_text_color(btn, lv_color_hex(text_color), LV_PART_MAIN); \
            lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);                            \
            lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);                      \
        }                                                                             \
    } while (0)

/**
 * Apply common panel styling
 */
#define LVGL_STYLE_PANEL(panel, bg_opa, border_opa)                       \
    do {                                                                  \
        if (panel) {                                                      \
            lv_obj_set_style_bg_opa(panel, bg_opa, LV_PART_MAIN);         \
            lv_obj_set_style_border_opa(panel, border_opa, LV_PART_MAIN); \
            lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);             \
        }                                                                 \
    } while (0)

/**
 * Set common text label styling
 */
#define LVGL_STYLE_LABEL(label, font, color, align)                                \
    do {                                                                           \
        if (label) {                                                               \
            lv_obj_set_style_text_font(label, font, LV_PART_MAIN);                 \
            lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN); \
            lv_obj_set_style_text_align(label, align, LV_PART_MAIN);               \
        }                                                                          \
    } while (0)

/**
 * Apply common input field styling
 */
#define LVGL_STYLE_INPUT_FIELD(field, bg_color, border_color, focus_color)                     \
    do {                                                                                       \
        if (field) {                                                                           \
            lv_obj_set_style_bg_color(field, lv_color_hex(bg_color), LV_PART_MAIN);            \
            lv_obj_set_style_border_width(field, 2, LV_PART_MAIN);                             \
            lv_obj_set_style_border_color(field, lv_color_hex(border_color), LV_PART_MAIN);    \
            lv_obj_set_style_border_color(field, lv_color_hex(focus_color), LV_STATE_FOCUSED); \
            lv_obj_set_style_radius(field, 8, LV_PART_MAIN);                                   \
            lv_obj_set_style_pad_all(field, 12, LV_PART_MAIN);                                 \
        }                                                                                      \
    } while (0)

/**
 * Set common progress bar styling
 */
#define LVGL_STYLE_PROGRESS_BAR(bar, bg_color, indicator_color)                               \
    do {                                                                                      \
        if (bar) {                                                                            \
            lv_obj_set_style_bg_color(bar, lv_color_hex(bg_color), LV_PART_MAIN);             \
            lv_obj_set_style_bg_color(bar, lv_color_hex(indicator_color), LV_PART_INDICATOR); \
            lv_obj_set_style_radius(bar, 10, LV_PART_MAIN);                                   \
            lv_obj_set_style_radius(bar, 10, LV_PART_INDICATOR);                              \
        }                                                                                     \
    } while (0)

/**
 * Create flex container with common alignment
 */
#define LVGL_SETUP_FLEX_CONTAINER(container, flow, main_place, cross_place, track_place) \
    do {                                                                                 \
        if (container) {                                                                 \
            lv_obj_set_flex_flow(container, flow);                                       \
            lv_obj_set_flex_align(container, main_place, cross_place, track_place);      \
        }                                                                                \
    } while (0)

// =============================================================================
// MULTI-DEVICE PATTERN MACROS
// =============================================================================

/**
 * Iterate over multiple devices with operation
 */
#define FOR_EACH_DEVICE(device_array, count, operation) \
    do {                                                \
        for (size_t i = 0; i < count; i++) {            \
            if (device_array[i]) {                      \
                operation(device_array[i], i);          \
            }                                           \
        }                                               \
    } while (0)

/**
 * Validate multiple devices are available
 */
#define VALIDATE_MULTIPLE_DEVICES(device_array, count, return_value) \
    do {                                                             \
        for (size_t i = 0; i < count; i++) {                         \
            if (!device_array[i]) {                                  \
                ESP_LOGW(TAG, "Device %zu not available", i);        \
                return return_value;                                 \
            }                                                        \
        }                                                            \
    } while (0)

/**
 * Apply operation to multiple resources with error handling
 */
#define MULTI_RESOURCE_OPERATION(resource_array, count, operation, error_handler) \
    do {                                                                          \
        bool allSucceeded = true;                                                 \
        for (size_t i = 0; i < count; i++) {                                      \
            if (resource_array[i] && !operation(resource_array[i])) {             \
                ESP_LOGW(TAG, "Operation failed for resource %zu", i);            \
                allSucceeded = false;                                             \
            }                                                                     \
        }                                                                         \
        if (!allSucceeded) {                                                      \
            error_handler;                                                        \
        }                                                                         \
    } while (0)

// =============================================================================
// MANAGER PATTERN MACROS
// =============================================================================

/**
 * Complete manager initialization pattern
 */
#define MANAGER_INIT_PATTERN(manager_name, init_flag, init_code) \
    bool init() {                                                \
        INIT_GUARD(manager_name, init_flag, TAG);                \
        ESP_LOGI(TAG, "Initializing " manager_name);             \
        init_code                                                \
            init_flag = true;                                    \
        ESP_LOGI(TAG, manager_name " initialized successfully"); \
        return true;                                             \
    }

/**
 * Complete manager deinitialization pattern
 */
#define MANAGER_DEINIT_PATTERN(manager_name, init_flag, deinit_code) \
    void deinit() {                                                  \
        if (!init_flag) return;                                      \
        ESP_LOGI(TAG, "Deinitializing " manager_name);               \
        deinit_code                                                  \
            init_flag = false;                                       \
    }

/**
 * Standard manager operation with init check
 */
#define MANAGER_OPERATION(operation_name, init_flag, return_value, code) \
    REQUIRE_INIT(operation_name, init_flag, TAG, return_value);          \
    code

#define MANAGER_OPERATION_VOID(operation_name, init_flag, code) \
    REQUIRE_INIT_VOID(operation_name, init_flag, TAG);          \
    code

// =============================================================================
// COMPACT BRACKET STYLE ENFORCEMENT MACROS
// =============================================================================

/**
 * Conditional execution with compact bracket style
 */
#define IF_THEN(condition, code) \
    if (condition) {             \
        code                     \
    }

#define IF_ELSE(condition, if_code, else_code) \
    if (condition) {                           \
        if_code                                \
    } else {                                   \
        else_code                              \
    }

/**
 * For loop with compact bracket style
 */
#define FOR_EACH(init, condition, increment, code) \
    for (init; condition; increment) {             \
        code                                       \
    }
