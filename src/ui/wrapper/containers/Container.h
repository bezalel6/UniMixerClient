#pragma once

#include "../base/WidgetBase.h"

namespace UI {
namespace Wrapper {

// =============================================================================
// CONTAINER WIDGETS
// =============================================================================

class Container : public WidgetBase<Container> {
   protected:
    lv_flex_flow_t flexFlow = LV_FLEX_FLOW_ROW;
    lv_flex_align_t mainAlign = LV_FLEX_ALIGN_START;
    lv_flex_align_t crossAlign = LV_FLEX_ALIGN_START;
    lv_flex_align_t trackAlign = LV_FLEX_ALIGN_START;

   public:
    Container() = default;
    Container(const std::string& id) { setId(id); }
    virtual ~Container() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Flex layout configuration
    Container& setFlexFlow(lv_flex_flow_t flow) {
        flexFlow = flow;
        SAFE_WIDGET_OP(widget, lv_obj_set_flex_flow(widget, flexFlow));
        return *this;
    }

    Container& setFlexAlign(lv_flex_align_t main, lv_flex_align_t cross, lv_flex_align_t track);

    // Styling presets
    Container& setCardStyle(bool enabled);
    Container& setGlassStyle(bool enabled);
    Container& setShadowStyle(int width, lv_color_t color, int opacity);

    // Getters
    lv_flex_flow_t getFlexFlow() const { return flexFlow; }
    lv_flex_align_t getMainAlign() const { return mainAlign; }
    lv_flex_align_t getCrossAlign() const { return crossAlign; }
    lv_flex_align_t getTrackAlign() const { return trackAlign; }
};

class ScrollContainer : public Container {
   protected:
    lv_dir_t scrollDir = LV_DIR_ALL;

   public:
    ScrollContainer() = default;
    ScrollContainer(const std::string& id) { setId(id); }
    virtual ~ScrollContainer() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);

    // Scroll configuration
    ScrollContainer& setScrollDir(lv_dir_t dir) {
        scrollDir = dir;
        SAFE_WIDGET_OP(widget, lv_obj_set_scroll_dir(widget, scrollDir));
        return *this;
    }

    ScrollContainer& scrollTo(int x, int y, bool animate = true);
    ScrollContainer& scrollToChild(lv_obj_t* child, bool animate = true);

    // Getters
    lv_dir_t getScrollDir() const { return scrollDir; }
};

}  // namespace Wrapper
}  // namespace UI
