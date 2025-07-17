#include "ProgressWidgets.h"
#include <esp_log.h>

static const char* TAG = "ProgressWidgets";

namespace UI {
namespace Wrapper {

// =============================================================================
// PROGRESS BAR IMPLEMENTATIONS
// =============================================================================

bool ProgressBar::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "ProgressBar already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_bar_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create progress bar widget");
        return false;
    }

    // Apply configuration
    lv_bar_set_range(widget, minValue, maxValue);
    lv_bar_set_value(widget, value, LV_ANIM_OFF);

    // Apply styling
    lv_obj_set_style_bg_color(widget, barColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(widget, backgroundColor, LV_PART_MAIN);

    markInitialized();
    ESP_LOGD(TAG, "ProgressBar created successfully: %s", widgetId.c_str());
    return true;
}

void ProgressBar::destroy() {
    WidgetBase<ProgressBar>::destroy();
}

void ProgressBar::update() {
    // ProgressBar doesn't need regular updates
}

ProgressBar& ProgressBar::setValue(int newValue) {
    if (newValue < minValue || newValue > maxValue) {
        ESP_LOGW(TAG, "Value %d out of range [%d, %d]", newValue, minValue, maxValue);
        return *this;
    }

    value = newValue;
    SAFE_WIDGET_OP(widget, lv_bar_set_value(widget, value, LV_ANIM_ON));
    return *this;
}

// =============================================================================
// CIRCULAR PROGRESS IMPLEMENTATIONS
// =============================================================================

bool CircularProgress::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "CircularProgress already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_arc_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create circular progress widget");
        return false;
    }

    // Apply configuration
    lv_arc_set_range(widget, minValue, maxValue);
    lv_arc_set_value(widget, value);
    lv_arc_set_angles(widget, startAngle, endAngle);

    // Apply styling
    lv_obj_set_style_arc_color(widget, arcColor, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(widget, backgroundColor, LV_PART_MAIN);

    markInitialized();
    ESP_LOGD(TAG, "CircularProgress created successfully: %s", widgetId.c_str());
    return true;
}

void CircularProgress::destroy() {
    WidgetBase<CircularProgress>::destroy();
}

void CircularProgress::update() {
    // CircularProgress doesn't need regular updates
}

CircularProgress& CircularProgress::setValue(int newValue) {
    if (newValue < minValue || newValue > maxValue) {
        ESP_LOGW(TAG, "Value %d out of range [%d, %d]", newValue, minValue, maxValue);
        return *this;
    }

    value = newValue;
    SAFE_WIDGET_OP(widget, lv_arc_set_value(widget, value));
    return *this;
}

}  // namespace Wrapper
}  // namespace UI
