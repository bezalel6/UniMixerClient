#include "Container.h"
#include <esp_log.h>

static const char* TAG = "Container";

namespace UI {
namespace Wrapper {

// =============================================================================
// CONTAINER IMPLEMENTATIONS
// =============================================================================

bool Container::init(lv_obj_t* parentObj) {
    if (isInitialized) return true;

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_obj_create(parent);

    if (!widget) return false;

    // Apply default container settings
    lv_obj_set_layout(widget, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(widget, flexFlow);
    lv_obj_set_flex_align(widget, mainAlign, crossAlign, trackAlign);

    markInitialized();
    return true;
}

void Container::destroy() {
    WidgetBase::destroy();
}

void Container::update() {
    if (!isReady()) return;

    lv_obj_set_flex_flow(widget, flexFlow);
    lv_obj_set_flex_align(widget, mainAlign, crossAlign, trackAlign);
}

Container& Container::setFlexAlign(lv_flex_align_t main, lv_flex_align_t cross, lv_flex_align_t track) {
    mainAlign = main;
    crossAlign = cross;
    trackAlign = track;

    SAFE_WIDGET_OP(widget, lv_obj_set_flex_align(widget, mainAlign, crossAlign, trackAlign));
    return *this;
}

Container& Container::setCardStyle(bool enabled) {
    if (!isReady()) return *this;

    if (enabled) {
        lv_obj_set_style_radius(widget, 5, 0);
        lv_obj_set_style_bg_opa(widget, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(widget, lv_color_hex(0x333333), 0);
        lv_obj_set_style_shadow_width(widget, 15, 0);
        lv_obj_set_style_shadow_color(widget, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(widget, LV_OPA_30, 0);
    } else {
        lv_obj_set_style_radius(widget, 0, 0);
        lv_obj_set_style_bg_opa(widget, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(widget, 0, 0);
    }

    return *this;
}

Container& Container::setGlassStyle(bool enabled) {
    if (!isReady()) return *this;

    if (enabled) {
        lv_obj_set_style_bg_opa(widget, LV_OPA_20, 0);
        lv_obj_set_style_radius(widget, 5, 0);
        lv_obj_set_style_border_width(widget, 1, 0);
        lv_obj_set_style_border_color(widget, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(widget, LV_OPA_20, 0);
    } else {
        lv_obj_set_style_bg_opa(widget, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(widget, 0, 0);
        lv_obj_set_style_border_width(widget, 0, 0);
    }

    return *this;
}

Container& Container::setShadowStyle(int width, lv_color_t color, int opacity) {
    if (!isReady()) return *this;

    lv_obj_set_style_shadow_width(widget, width, 0);
    lv_obj_set_style_shadow_color(widget, color, 0);
    lv_obj_set_style_shadow_opa(widget, opacity, 0);

    return *this;
}

// =============================================================================
// SCROLL CONTAINER IMPLEMENTATIONS
// =============================================================================

bool ScrollContainer::init(lv_obj_t* parentObj) {
    if (!Container::init(parentObj)) return false;

    lv_obj_set_scrollbar_mode(widget, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(widget, scrollDir);

    return true;
}

ScrollContainer& ScrollContainer::scrollTo(int x, int y, bool animate) {
    if (!isReady()) return *this;

    lv_obj_scroll_to(widget, x, y, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    return *this;
}

ScrollContainer& ScrollContainer::scrollToChild(lv_obj_t* child, bool animate) {
    if (!isReady() || !child) return *this;

    lv_obj_scroll_to_view(child, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    return *this;
}

}  // namespace Wrapper
}  // namespace UI
