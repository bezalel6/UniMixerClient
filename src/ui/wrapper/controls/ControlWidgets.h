#pragma once

#include "../base/WidgetBase.h"
#include <functional>
#include <vector>

namespace UI {
namespace Wrapper {

// =============================================================================
// CONTROL WIDGETS
// =============================================================================

class Slider : public WidgetBase<Slider> {
   protected:
    int value = 0;
    int minValue = 0;
    int maxValue = 100;
    std::function<void(int)> onChangeCallback;

   public:
    Slider() = default;
    Slider(const std::string& id) { setId(id); }
    virtual ~Slider() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Value manipulation
    Slider& setValue(int newValue);
    Slider& setRange(int min, int max) {
        minValue = min;
        maxValue = max;
        SAFE_WIDGET_OP(widget, lv_slider_set_range(widget, minValue, maxValue));
        return *this;
    }

    // Configuration
    Slider& setOnChange(std::function<void(int)> callback) {
        onChangeCallback = callback;
        return *this;
    }

    Slider& setKnobColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, color, LV_PART_KNOB));
        return *this;
    }

    Slider& setTrackColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, color, LV_PART_MAIN));
        return *this;
    }

    Slider& setIndicatorColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, color, LV_PART_INDICATOR));
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

class List : public WidgetBase<List> {
   protected:
    std::vector<std::string> items;
    int selectedIndex = -1;
    std::function<void(int)> onSelectCallback;

   public:
    List() = default;
    List(const std::string& id) { setId(id); }
    virtual ~List() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Item manipulation
    List& addItem(const std::string& item);
    List& addItems(const std::vector<std::string>& newItems);
    List& removeItem(int index);
    List& clearItems();
    List& setSelectedIndex(int index);

    // Configuration
    List& setOnSelect(std::function<void(int)> callback) {
        onSelectCallback = callback;
        return *this;
    }

    List& setItemHeight(int height) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_height(widget, height, LV_PART_MAIN));
        return *this;
    }

    List& setItemPadding(int padding) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_pad_all(widget, padding, LV_PART_MAIN));
        return *this;
    }

    // Getters
    const std::vector<std::string>& getItems() const { return items; }
    int getSelectedIndex() const { return selectedIndex; }
    std::string getSelectedItem() const {
        return (selectedIndex >= 0 && selectedIndex < (int)items.size()) ? items[selectedIndex] : "";
    }
    size_t getItemCount() const { return items.size(); }
};

}  // namespace Wrapper
}  // namespace UI
