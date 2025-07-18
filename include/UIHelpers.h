#pragma once

#include <lvgl.h>

/**
 * Simple UI Helper Functions
 * 
 * Lightweight alternatives to the deprecated LVGLWrapper.
 * These are simple inline functions with zero overhead.
 * For complex UI, use SquareLine Studio.
 */

namespace UI {

// Card-styled container
inline lv_obj_t* createCard(lv_obj_t* parent, int width = LV_SIZE_CONTENT, int height = LV_SIZE_CONTENT) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    
    // Card styling
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    
    return card;
}

// Styled label with common presets
inline lv_obj_t* createLabel(lv_obj_t* parent, const char* text, const lv_font_t* font = nullptr) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    
    return label;
}

// Heading label
inline lv_obj_t* createHeading(lv_obj_t* parent, const char* text) {
    return createLabel(parent, text, &lv_font_montserrat_24);
}

// Body text label
inline lv_obj_t* createBodyText(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = createLabel(parent, text, &lv_font_montserrat_14);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

// Caption label
inline lv_obj_t* createCaption(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = createLabel(parent, text, &lv_font_montserrat_12);
    lv_obj_set_style_text_color(label, lv_color_hex(0x666666), 0);
    return label;
}

// Flex container
inline lv_obj_t* createFlexContainer(lv_obj_t* parent, lv_flex_flow_t flow = LV_FLEX_FLOW_COLUMN) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, flow);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    return container;
}

// Button with text
inline lv_obj_t* createButton(lv_obj_t* parent, const char* text, lv_event_cb_t event_cb = nullptr, void* user_data = nullptr) {
    lv_obj_t* btn = lv_btn_create(parent);
    
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    if (event_cb) {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, user_data);
    }
    
    return btn;
}

// Progress bar
inline lv_obj_t* createProgressBar(lv_obj_t* parent, int min = 0, int max = 100, int value = 0) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_bar_set_range(bar, min, max);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    return bar;
}

// Slider
inline lv_obj_t* createSlider(lv_obj_t* parent, int min = 0, int max = 100, int value = 50) {
    lv_obj_t* slider = lv_slider_create(parent);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    return slider;
}

// Arc (circular slider)
inline lv_obj_t* createArc(lv_obj_t* parent, int min = 0, int max = 100, int value = 50) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_arc_set_range(arc, min, max);
    lv_arc_set_value(arc, value);
    return arc;
}

// Switch
inline lv_obj_t* createSwitch(lv_obj_t* parent, bool checked = false) {
    lv_obj_t* sw = lv_switch_create(parent);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    return sw;
}

// Checkbox
inline lv_obj_t* createCheckbox(lv_obj_t* parent, const char* text, bool checked = false) {
    lv_obj_t* cb = lv_checkbox_create(parent);
    lv_checkbox_set_text(cb, text);
    if (checked) {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    }
    return cb;
}

// Apply common styles
inline void applyCardStyle(lv_obj_t* obj) {
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 10, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
}

inline void applyGlassStyle(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_20, 0);
}

} // namespace UI