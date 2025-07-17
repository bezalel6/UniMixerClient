#pragma once

#include "../base/WidgetBase.h"

namespace UI {
namespace Wrapper {

// =============================================================================
// PROGRESS WIDGETS
// =============================================================================

class ProgressBar : public WidgetBase<ProgressBar> {
   protected:
    int value = 0;
    int minValue = 0;
    int maxValue = 100;
    lv_color_t barColor = lv_color_hex(0x007AFF);
    lv_color_t backgroundColor = lv_color_hex(0xE5E5EA);

   public:
    ProgressBar() = default;
    ProgressBar(const std::string& id) { setId(id); }
    virtual ~ProgressBar() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Value manipulation
    ProgressBar& setValue(int newValue);
    ProgressBar& setRange(int min, int max) {
        minValue = min;
        maxValue = max;
        SAFE_WIDGET_OP(widget, lv_bar_set_range(widget, minValue, maxValue));
        return *this;
    }

    // Styling
    ProgressBar& setBarColor(lv_color_t color) {
        barColor = color;
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, barColor, LV_PART_INDICATOR));
        return *this;
    }

    ProgressBar& setBackgroundColor(lv_color_t color) {
        backgroundColor = color;
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, backgroundColor, LV_PART_MAIN));
        return *this;
    }

    ProgressBar& setRadius(int radius) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_radius(widget, radius, LV_PART_MAIN));
        SAFE_WIDGET_OP(widget, lv_obj_set_style_radius(widget, radius, LV_PART_INDICATOR));
        return *this;
    }

    // Getters
    int getValue() const { return value; }
    int getMinValue() const { return minValue; }
    int getMaxValue() const { return maxValue; }
    float getPercentage() const {
        return maxValue > minValue ? (float)(value - minValue) / (maxValue - minValue) * 100.0f : 0.0f;
    }
};

class CircularProgress : public WidgetBase<CircularProgress> {
   protected:
    int value = 0;
    int minValue = 0;
    int maxValue = 100;
    int startAngle = 0;
    int endAngle = 360;
    lv_color_t arcColor = lv_color_hex(0x007AFF);
    lv_color_t backgroundColor = lv_color_hex(0xE5E5EA);

   public:
    CircularProgress() = default;
    CircularProgress(const std::string& id) { setId(id); }
    virtual ~CircularProgress() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Value manipulation
    CircularProgress& setValue(int newValue);
    CircularProgress& setRange(int min, int max) {
        minValue = min;
        maxValue = max;
        SAFE_WIDGET_OP(widget, lv_arc_set_range(widget, minValue, maxValue));
        return *this;
    }

    CircularProgress& setAngles(int start, int end) {
        startAngle = start;
        endAngle = end;
        SAFE_WIDGET_OP(widget, lv_arc_set_angles(widget, startAngle, endAngle));
        return *this;
    }

    // Styling
    CircularProgress& setArcColor(lv_color_t color) {
        arcColor = color;
        SAFE_WIDGET_OP(widget, lv_obj_set_style_arc_color(widget, arcColor, LV_PART_INDICATOR));
        return *this;
    }

    CircularProgress& setBackgroundColor(lv_color_t color) {
        backgroundColor = color;
        SAFE_WIDGET_OP(widget, lv_obj_set_style_arc_color(widget, backgroundColor, LV_PART_MAIN));
        return *this;
    }

    CircularProgress& setArcWidth(int width) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_arc_width(widget, width, LV_PART_MAIN));
        SAFE_WIDGET_OP(widget, lv_obj_set_style_arc_width(widget, width, LV_PART_INDICATOR));
        return *this;
    }

    // Getters
    int getValue() const { return value; }
    int getMinValue() const { return minValue; }
    int getMaxValue() const { return maxValue; }
    float getPercentage() const {
        return maxValue > minValue ? (float)(value - minValue) / (maxValue - minValue) * 100.0f : 0.0f;
    }
};

}  // namespace Wrapper
}  // namespace UI
