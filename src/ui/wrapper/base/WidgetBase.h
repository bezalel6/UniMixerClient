#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <type_traits>

namespace UI {
namespace Wrapper {

// =============================================================================
// MACROS AND TEMPLATES FOR ROBUST WRAPPER SYSTEM
// =============================================================================

// Macro for fluent setter methods
#define FLUENT_SETTER(CLASS, METHOD, TYPE, FIELD) \
    CLASS& METHOD(TYPE value) {                   \
        FIELD = value;                            \
        return *this;                             \
    }

// Macro for LVGL event binding
#define LVGL_EVENT_BIND(widget, event, handler) \
    lv_obj_add_event_cb(widget, [](lv_event_t* e) { handler(e); }, event, nullptr)

// Macro for safe widget operations
#define SAFE_WIDGET_OP(widget, op) \
    if (widget) {                  \
        op;                        \
    }

// Template for type-safe property setting
template <typename T>
class Property {
    T value;
    std::function<void(T)> setter;

   public:
    Property(T defaultValue, std::function<void(T)> setterFunc)
        : value(defaultValue), setter(setterFunc) {}

    Property& set(const T& v) {
        value = v;
        if (setter) setter(v);
        return *this;
    }
    const T& get() const { return value; }
    operator T() const { return value; }
};

// Template for event callback registration
template <typename CallbackType>
class EventHandler {
    CallbackType callback;
    lv_event_code_t eventCode;

   public:
    EventHandler(lv_event_code_t code) : eventCode(code) {}

    EventHandler& setCallback(CallbackType cb) {
        callback = cb;
        return *this;
    }

    void attach(lv_obj_t* widget) {
        if (widget && callback) {
            lv_obj_add_event_cb(widget, [](lv_event_t* e) {
                    auto handler = static_cast<EventHandler*>(lv_event_get_user_data(e));
                    if (handler && handler->callback) {
                        handler->callback();
                    } }, eventCode, this);
        }
    }
};

// Template for property-based styling
template <typename WidgetType>
class StyleManager {
    WidgetType* widget;
    std::map<std::string, Property<int>> intProperties;
    std::map<std::string, Property<lv_color_t>> colorProperties;

   public:
    StyleManager(WidgetType* w) : widget(w) {}

    template <typename T>
    StyleManager& addProperty(const std::string& name, T defaultValue, std::function<void(T)> setter) {
        if constexpr (std::is_same_v<T, int>) {
            intProperties[name] = Property<int>(defaultValue, setter);
        } else if constexpr (std::is_same_v<T, lv_color_t>) {
            colorProperties[name] = Property<lv_color_t>(defaultValue, setter);
        }
        return *this;
    }

    template <typename T>
    Property<T>& getProperty(const std::string& name) {
        if constexpr (std::is_same_v<T, int>) {
            return intProperties[name];
        } else if constexpr (std::is_same_v<T, lv_color_t>) {
            return colorProperties[name];
        }
    }
};

// Base class for all widget wrappers using CRTP
template <typename Derived>
class WidgetBase {
   protected:
    lv_obj_t* widget = nullptr;
    lv_obj_t* parent = nullptr;
    bool isInitialized = false;
    std::string widgetId;

   public:
    WidgetBase() = default;
    explicit WidgetBase(const std::string& id) { setId(id); }
    virtual ~WidgetBase() = default;

    // Core lifecycle methods
    virtual bool init(lv_obj_t* parentObj = nullptr) = 0;
    virtual void destroy() {
        if (widget) {
            lv_obj_del(widget);
            widget = nullptr;
            isInitialized = false;
        }
    }
    virtual void update() = 0;

    // Common widget operations with proper return types
    Derived& show() {
        SAFE_WIDGET_OP(widget, lv_obj_clear_flag(widget, LV_OBJ_FLAG_HIDDEN));
        return static_cast<Derived&>(*this);
    }

    Derived& hide() {
        SAFE_WIDGET_OP(widget, lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN));
        return static_cast<Derived&>(*this);
    }

    Derived& setVisible(bool visible) {
        visible ? show() : hide();
        return static_cast<Derived&>(*this);
    }

    bool isVisible() const {
        return widget && !lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN);
    }

    // Position and size with validation
    Derived& setPosition(int x, int y) {
        // Position can be negative for relative positioning in LVGL
        // Only validate for extremely large negative values that indicate errors
        if (x < -10000 || y < -10000) {
            ESP_LOGW("LVGLWrapper", "Position validation failed: %d, %d", x, y);
            return static_cast<Derived&>(*this);
        }
        SAFE_WIDGET_OP(widget, lv_obj_set_pos(widget, x, y));
        return static_cast<Derived&>(*this);
    }

    Derived& setSize(int width, int height) {
        // Allow LVGL special size constants (LV_SIZE_CONTENT, LV_PCT, etc.)
        // These are typically negative values or special constants
        bool validWidth = (width > 0) || (width == LV_SIZE_CONTENT) || (width <= LV_PCT(100) && width >= LV_PCT(0));
        bool validHeight = (height > 0) || (height == LV_SIZE_CONTENT) || (height <= LV_PCT(100) && height >= LV_PCT(0));

        if (!validWidth || !validHeight) {
            ESP_LOGW("LVGLWrapper", "Size validation failed: %d, %d", width, height);
            return static_cast<Derived&>(*this);
        }

        ESP_LOGD("LVGLWrapper", "Setting size: %d x %d for widget %s", width, height, widgetId.c_str());
        SAFE_WIDGET_OP(widget, lv_obj_set_size(widget, width, height));
        return static_cast<Derived&>(*this);
    }

    Derived& setAlign(lv_align_t align) {
        SAFE_WIDGET_OP(widget, lv_obj_align(widget, align, 0, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& center() {
        SAFE_WIDGET_OP(widget, lv_obj_center(widget));
        return static_cast<Derived&>(*this);
    }

    // Style helpers with type safety
    Derived& setBackgroundColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_color(widget, color, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setTextColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_text_color(widget, color, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setBorderColor(lv_color_t color) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_border_color(widget, color, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setRadius(int radius) {
        // Radius should be non-negative, but allow LVGL special constants
        if (radius < 0 && radius != LV_RADIUS_CIRCLE) {
            ESP_LOGW("LVGLWrapper", "Radius validation failed: %d", radius);
            return static_cast<Derived&>(*this);
        }
        SAFE_WIDGET_OP(widget, lv_obj_set_style_radius(widget, radius, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setPadding(int padding) {
        // Padding should be non-negative
        if (padding < 0) {
            ESP_LOGW("LVGLWrapper", "Padding validation failed: %d", padding);
            return static_cast<Derived&>(*this);
        }
        SAFE_WIDGET_OP(widget, lv_obj_set_style_pad_all(widget, padding, 0));
        return static_cast<Derived&>(*this);
    }

    // Additional methods needed by BSODHandler
    Derived& setWidth(lv_coord_t width) {
        // Allow LVGL special size constants for width
        bool validWidth = (width > 0) || (width == LV_SIZE_CONTENT) || (width <= LV_PCT(100) && width >= LV_PCT(0));
        if (!validWidth) {
            ESP_LOGW("LVGLWrapper", "Width validation failed: %d", width);
            return static_cast<Derived&>(*this);
        }
        SAFE_WIDGET_OP(widget, lv_obj_set_width(widget, width));
        return static_cast<Derived&>(*this);
    }

    Derived& setHeight(lv_coord_t height) {
        // Allow LVGL special size constants for height
        bool validHeight = (height > 0) || (height == LV_SIZE_CONTENT) || (height <= LV_PCT(100) && height >= LV_PCT(0));
        if (!validHeight) {
            ESP_LOGW("LVGLWrapper", "Height validation failed: %d", height);
            return static_cast<Derived&>(*this);
        }
        SAFE_WIDGET_OP(widget, lv_obj_set_height(widget, height));
        return static_cast<Derived&>(*this);
    }

    Derived& setFont(const lv_font_t* font) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_text_font(widget, font, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setBackgroundOpacity(lv_opa_t opacity) {
        SAFE_WIDGET_OP(widget, lv_obj_set_style_bg_opa(widget, opacity, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setMargin(int margin) {
        // Margin can be negative for overlap effects in LVGL
        SAFE_WIDGET_OP(widget, lv_obj_set_style_margin_all(widget, margin, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setMarginBottom(int margin) {
        // Margin can be negative for overlap effects in LVGL
        SAFE_WIDGET_OP(widget, lv_obj_set_style_margin_bottom(widget, margin, 0));
        return static_cast<Derived&>(*this);
    }

    Derived& setMarginTop(int margin) {
        // Margin can be negative for overlap effects in LVGL
        SAFE_WIDGET_OP(widget, lv_obj_set_style_margin_top(widget, margin, 0));
        return static_cast<Derived&>(*this);
    }

    // Getters
    lv_obj_t* getWidget() const { return widget; }
    lv_obj_t* getParent() const { return parent; }
    bool isReady() const { return isInitialized && widget != nullptr; }
    const std::string& getId() const { return widgetId; }

   protected:
    void setId(const std::string& id) { widgetId = id; }
    void markInitialized() { isInitialized = true; }
};

}  // namespace Wrapper
}  // namespace UI
