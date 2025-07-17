#include "InputWidgets.h"
#include <esp_log.h>

static const char* TAG = "InputWidgets";

namespace UI {
namespace Wrapper {

// =============================================================================
// NUMBER INPUT IMPLEMENTATIONS
// =============================================================================

bool NumberInput::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "NumberInput already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_textarea_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create number input widget");
        return false;
    }

    // Apply configuration
    lv_textarea_set_text(widget, value.c_str());
    lv_textarea_set_max_length(widget, 10);
    lv_textarea_set_one_line(widget, true);
    lv_textarea_set_accepted_chars(widget, "0123456789");

    // Set range
    lv_textarea_set_placeholder_text(widget, "Enter number");

    // Add event callback
    lv_obj_add_event_cb(widget, [](lv_event_t* e) {
        auto input = static_cast<NumberInput*>(lv_event_get_user_data(e));
        if (input && input->onChangeCallback) {
            try {
                int val = std::stoi(input->getValue());
                input->onChangeCallback(val);
            } catch (...) {
                // Invalid number, ignore
            }
        } }, LV_EVENT_VALUE_CHANGED, this);

    markInitialized();
    ESP_LOGD(TAG, "NumberInput created successfully: %s", widgetId.c_str());
    return true;
}

void NumberInput::destroy() {
    WidgetBase<NumberInput>::destroy();
}

void NumberInput::update() {
    // NumberInput doesn't need regular updates
}

NumberInput& NumberInput::setValue(int newValue) {
    if (newValue < minValue || newValue > maxValue) {
        ESP_LOGW(TAG, "Value %d out of range [%d, %d]", newValue, minValue, maxValue);
        return *this;
    }

    value = std::to_string(newValue);
    SAFE_WIDGET_OP(widget, lv_textarea_set_text(widget, value.c_str()));
    return *this;
}

NumberInput& NumberInput::setValue(const std::string& newValue) {
    try {
        int val = std::stoi(newValue);
        if (val >= minValue && val <= maxValue) {
            value = newValue;
            SAFE_WIDGET_OP(widget, lv_textarea_set_text(widget, value.c_str()));
        }
    } catch (...) {
        ESP_LOGW(TAG, "Invalid number format: %s", newValue.c_str());
    }
    return *this;
}

NumberInput& NumberInput::increment() {
    try {
        int current = std::stoi(value);
        setValue(current + step);
    } catch (...) {
        setValue(minValue);
    }
    return *this;
}

NumberInput& NumberInput::decrement() {
    try {
        int current = std::stoi(value);
        setValue(current - step);
    } catch (...) {
        setValue(minValue);
    }
    return *this;
}

// =============================================================================
// TOGGLE BUTTON IMPLEMENTATIONS
// =============================================================================

bool ToggleButton::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "ToggleButton already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_btn_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create toggle button widget");
        return false;
    }

    // Create label for the button
    lv_obj_t* label = lv_label_create(widget);
    lv_label_set_text(label, text.c_str());
    lv_obj_center(label);

    // Set initial state
    setToggled(isToggled);

    // Add event callback
    lv_obj_add_event_cb(widget, [](lv_event_t* e) {
        auto btn = static_cast<ToggleButton*>(lv_event_get_user_data(e));
        if (btn) {
            btn->toggle();
            if (btn->onToggleCallback) {
                btn->onToggleCallback(btn->getToggled());
            }
        } }, LV_EVENT_CLICKED, this);

    markInitialized();
    ESP_LOGD(TAG, "ToggleButton created successfully: %s", widgetId.c_str());
    return true;
}

void ToggleButton::destroy() {
    WidgetBase<ToggleButton>::destroy();
}

void ToggleButton::update() {
    // ToggleButton doesn't need regular updates
}

ToggleButton& ToggleButton::setToggled(bool toggled) {
    isToggled = toggled;

    if (!widget) return *this;

    if (isToggled) {
        lv_obj_add_state(widget, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(widget, lv_color_hex(0x007AFF), 0);
    } else {
        lv_obj_clear_state(widget, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(widget, lv_color_hex(0xE5E5EA), 0);
    }

    return *this;
}

ToggleButton& ToggleButton::toggle() {
    return setToggled(!isToggled);
}

ToggleButton& ToggleButton::setText(const std::string& newText) {
    text = newText;
    if (widget) {
        lv_obj_t* label = lv_obj_get_child(widget, 0);
        if (label) {
            lv_label_set_text(label, text.c_str());
        }
    }
    return *this;
}

}  // namespace Wrapper
}  // namespace UI
