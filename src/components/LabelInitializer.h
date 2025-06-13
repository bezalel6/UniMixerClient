#ifndef LABEL_INITIALIZER_H
#define LABEL_INITIALIZER_H

#include <lvgl.h>

namespace UI {
namespace Components {

// Label types for consistent initialization
enum LabelType {
    LABEL_TYPE_EMPTY,    // Completely empty
    LABEL_TYPE_DASH,     // Dash placeholder
    LABEL_TYPE_SPACE,    // Single space
    LABEL_TYPE_UNKNOWN,  // Unknown value
    LABEL_TYPE_NONE      // None value
};

class LabelInitializer {
   public:
    LabelInitializer();
    ~LabelInitializer();

    // Generic label initialization
    void initializeLabel(lv_obj_t* label, LabelType type);

    // Specific label type initializations
    void initializeAudioDeviceLabel(lv_obj_t* label);
    void initializeStatusLabel(lv_obj_t* label);
    void initializeIndicatorLabel(lv_obj_t* label);
    void initializeNetworkLabel(lv_obj_t* label);
};

}  // namespace Components
}  // namespace UI

#endif  // LABEL_INITIALIZER_H