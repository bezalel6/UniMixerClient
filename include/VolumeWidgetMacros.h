/**
 * Volume Widget Abstraction Macros
 *
 * This header provides macros to abstract between LVGL arc and slider widgets
 * for volume controls. This allows easy switching between widget types by
 * changing a single configuration macro.
 *
 * To switch widget types, change VOLUME_WIDGET_TYPE to either:
 * - VOLUME_WIDGET_TYPE_ARC (default)
 * - VOLUME_WIDGET_TYPE_SLIDER
 */

#pragma once

#include <lvgl.h>
#include <esp_log.h>

// Configuration: Choose which widget type to use for volume controls
// Change this to switch between arc and slider widgets globally
#define VOLUME_WIDGET_TYPE_ARC 1
#define VOLUME_WIDGET_TYPE_SLIDER 2

// Current widget type selection (change this to switch widget types)
#define VOLUME_WIDGET_TYPE VOLUME_WIDGET_TYPE_SLIDER

// Tag for volume widget logging
static const char* VOLUME_WIDGET_TAG = "VolumeWidget";

// Widget-agnostic macros that automatically use the correct API
#if VOLUME_WIDGET_TYPE == VOLUME_WIDGET_TYPE_ARC

// Arc-specific implementations
#define VOLUME_WIDGET_CREATE(parent) lv_arc_create(parent)

#define VOLUME_WIDGET_SET_VALUE(widget, value) \
    do { \
        if (!(widget)) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE failed: NULL widget pointer"); \
        } else if ((value) < 0 || (value) > 100) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE failed: Invalid value %d (must be 0-100)", (int)(value)); \
        } else { \
            lv_arc_set_value(widget, value); \
            ESP_LOGD(VOLUME_WIDGET_TAG, "Arc value set to %d", (int)(value)); \
        } \
    } while(0)

#define VOLUME_WIDGET_GET_VALUE(widget) \
    ((widget) ? lv_arc_get_value(widget) : \
     (ESP_LOGE(VOLUME_WIDGET_TAG, "GET_VALUE failed: NULL widget pointer"), 0))
#define VOLUME_WIDGET_SET_RANGE(widget, min, max) lv_arc_set_range(widget, min, max)
#define VOLUME_WIDGET_SET_MODE(widget, mode) lv_arc_set_mode(widget, mode)

// Arc-specific mode constants
#define VOLUME_WIDGET_MODE_NORMAL LV_ARC_MODE_NORMAL
#define VOLUME_WIDGET_MODE_REVERSE LV_ARC_MODE_REVERSE
#define VOLUME_WIDGET_MODE_SYMMETRICAL LV_ARC_MODE_SYMMETRICAL

// Style setters for arc
#define VOLUME_WIDGET_SET_WIDTH(widget, width) \
    lv_obj_set_style_arc_width(widget, width, LV_PART_MAIN | LV_STATE_DEFAULT)

#define VOLUME_WIDGET_SET_INDICATOR_WIDTH(widget, width) \
    lv_obj_set_style_arc_width(widget, width, LV_PART_INDICATOR | LV_STATE_DEFAULT)

#elif VOLUME_WIDGET_TYPE == VOLUME_WIDGET_TYPE_SLIDER

// Slider-specific implementations
#define VOLUME_WIDGET_CREATE(parent) lv_slider_create(parent)

#define VOLUME_WIDGET_SET_VALUE(widget, value) \
    do { \
        if (!(widget)) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE failed: NULL widget pointer"); \
        } else if ((value) < 0 || (value) > 100) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE failed: Invalid value %d (must be 0-100)", (int)(value)); \
        } else { \
            lv_slider_set_value(widget, value, LV_ANIM_OFF); \
            ESP_LOGD(VOLUME_WIDGET_TAG, "Slider value set to %d", (int)(value)); \
        } \
    } while(0)

#define VOLUME_WIDGET_GET_VALUE(widget) \
    ((widget) ? lv_slider_get_value(widget) : \
     (ESP_LOGE(VOLUME_WIDGET_TAG, "GET_VALUE failed: NULL widget pointer"), 0))
#define VOLUME_WIDGET_SET_RANGE(widget, min, max) lv_slider_set_range(widget, min, max)
#define VOLUME_WIDGET_SET_MODE(widget, mode) lv_slider_set_mode(widget, mode)

// Slider-specific mode constants
#define VOLUME_WIDGET_MODE_NORMAL LV_SLIDER_MODE_NORMAL
#define VOLUME_WIDGET_MODE_REVERSE LV_SLIDER_MODE_SYMMETRICAL
#define VOLUME_WIDGET_MODE_SYMMETRICAL LV_SLIDER_MODE_SYMMETRICAL

// Style setters for slider (map to appropriate slider styles)
#define VOLUME_WIDGET_SET_WIDTH(widget, width) \
    lv_obj_set_height(widget, width)  // For horizontal slider, height controls thickness

#define VOLUME_WIDGET_SET_INDICATOR_WIDTH(widget, width) \
    // Sliders don't have separate indicator width

#else
#error "Invalid VOLUME_WIDGET_TYPE. Must be either VOLUME_WIDGET_TYPE_ARC or VOLUME_WIDGET_TYPE_SLIDER"
#endif

// Common helper macros that work for both widget types
#define VOLUME_WIDGET_SET_VALUE_WITH_ANIM(widget, value)    \
    do {                                                    \
        if (!(widget)) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE_WITH_ANIM failed: NULL widget pointer"); \
        } else if ((value) < 0 || (value) > 100) { \
            ESP_LOGE(VOLUME_WIDGET_TAG, "SET_VALUE_WITH_ANIM failed: Invalid value %d (must be 0-100)", (int)(value)); \
        } else { \
            if (VOLUME_WIDGET_TYPE == VOLUME_WIDGET_TYPE_ARC) { \
                lv_arc_set_value(widget, value);                \
                ESP_LOGD(VOLUME_WIDGET_TAG, "Arc value set to %d (animated)", (int)(value)); \
            } else {                                            \
                lv_slider_set_value(widget, value, LV_ANIM_ON); \
                ESP_LOGD(VOLUME_WIDGET_TAG, "Slider value set to %d (animated)", (int)(value)); \
            }                                                   \
        } \
    } while (0)

// Event types that are common to both widgets
#define VOLUME_WIDGET_EVENT_VALUE_CHANGED LV_EVENT_VALUE_CHANGED
#define VOLUME_WIDGET_EVENT_RELEASED LV_EVENT_RELEASED

// Helper to update a label with the current volume value
#define VOLUME_WIDGET_UPDATE_LABEL(widget, label, prefix, suffix)                                      \
    do {                                                                                               \
        char buf[32];                                                                                  \
        lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)VOLUME_WIDGET_GET_VALUE(widget), suffix); \
        lv_label_set_text(label, buf);                                                                 \
    } while (0)
