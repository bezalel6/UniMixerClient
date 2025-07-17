#include "ControlWidgets.h"
#include <esp_log.h>

static const char* TAG = "ControlWidgets";

namespace UI {
namespace Wrapper {

// =============================================================================
// SLIDER IMPLEMENTATIONS
// =============================================================================

bool Slider::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "Slider already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_slider_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create slider widget");
        return false;
    }

    // Apply configuration
    lv_slider_set_range(widget, minValue, maxValue);
    lv_slider_set_value(widget, value, LV_ANIM_OFF);

    // Add event callback
    lv_obj_add_event_cb(widget, [](lv_event_t* e) {
        auto slider = static_cast<Slider*>(lv_event_get_user_data(e));
        if (slider && slider->onChangeCallback) {
            int val = lv_slider_get_value(slider->widget);
            slider->value = val;
            slider->onChangeCallback(val);
        } }, LV_EVENT_VALUE_CHANGED, this);

    markInitialized();
    ESP_LOGD(TAG, "Slider created successfully: %s", widgetId.c_str());
    return true;
}

void Slider::destroy() {
    WidgetBase<Slider>::destroy();
}

void Slider::update() {
    // Slider doesn't need regular updates
}

Slider& Slider::setValue(int newValue) {
    if (newValue < minValue || newValue > maxValue) {
        ESP_LOGW(TAG, "Value %d out of range [%d, %d]", newValue, minValue, maxValue);
        return *this;
    }

    value = newValue;
    SAFE_WIDGET_OP(widget, lv_slider_set_value(widget, value, LV_ANIM_ON));
    return *this;
}

// =============================================================================
// LIST IMPLEMENTATIONS
// =============================================================================

bool List::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "List already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_list_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create list widget");
        return false;
    }

    // Add event callback
    lv_obj_add_event_cb(widget, [](lv_event_t* e) {
        auto list = static_cast<List*>(lv_event_get_user_data(e));
        if (list && list->onSelectCallback) {
            // Get the selected item index
            lv_obj_t* selected = static_cast<lv_obj_t*>(lv_event_get_target(e));
            if (selected) {
                // Find the index of the selected item
                for (size_t i = 0; i < list->items.size(); i++) {
                    if (lv_obj_get_child(list->widget, i) == selected) {
                        list->selectedIndex = i;
                        list->onSelectCallback(i);
                        break;
                    }
                }
            }
        } }, LV_EVENT_CLICKED, this);

    markInitialized();
    ESP_LOGD(TAG, "List created successfully: %s", widgetId.c_str());
    return true;
}

void List::destroy() {
    WidgetBase<List>::destroy();
}

void List::update() {
    // List doesn't need regular updates
}

List& List::addItem(const std::string& item) {
    items.push_back(item);
    if (widget) {
        lv_obj_t* btn = lv_list_add_btn(widget, LV_SYMBOL_FILE, item.c_str());
        if (btn) {
            lv_obj_set_user_data(btn, this);
        }
    }
    return *this;
}

List& List::addItems(const std::vector<std::string>& newItems) {
    for (const auto& item : newItems) {
        addItem(item);
    }
    return *this;
}

List& List::removeItem(int index) {
    if (index < 0 || index >= (int)items.size()) {
        ESP_LOGW(TAG, "Invalid index: %d", index);
        return *this;
    }

    if (widget) {
        lv_obj_t* child = lv_obj_get_child(widget, index);
        if (child) {
            lv_obj_del(child);
        }
    }

    items.erase(items.begin() + index);

    // Adjust selected index
    if (selectedIndex >= index) {
        selectedIndex = std::max(0, selectedIndex - 1);
    }

    return *this;
}

List& List::clearItems() {
    items.clear();
    selectedIndex = -1;

    if (widget) {
        lv_obj_clean(widget);
    }

    return *this;
}

List& List::setSelectedIndex(int index) {
    if (index < -1 || index >= (int)items.size()) {
        ESP_LOGW(TAG, "Invalid index: %d", index);
        return *this;
    }

    selectedIndex = index;

    if (widget && index >= 0) {
        lv_obj_t* child = lv_obj_get_child(widget, index);
        if (child) {
            lv_obj_add_state(child, LV_STATE_FOCUSED);
        }
    }

    return *this;
}

}  // namespace Wrapper
}  // namespace UI
