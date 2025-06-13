#include "LabelInitializer.h"
#include "../include/UIConstants.h"
#include "../display/DisplayManager.h"

namespace UI {
namespace Components {

LabelInitializer::LabelInitializer() {}

LabelInitializer::~LabelInitializer() {}

void LabelInitializer::initializeLabel(lv_obj_t* label, LabelType type) {
    if (!label) return;

    switch (type) {
        case LABEL_TYPE_EMPTY:
            Display::initializeLabelEmpty(label);
            break;
        case LABEL_TYPE_DASH:
            Display::initializeLabelDash(label);
            break;
        case LABEL_TYPE_SPACE:
            Display::initializeLabelSpace(label);
            break;
        case LABEL_TYPE_UNKNOWN:
            Display::initializeLabelUnknown(label);
            break;
        case LABEL_TYPE_NONE:
            Display::initializeLabelNone(label);
            break;
        default:
            Display::initializeLabelEmpty(label);
            break;
    }
}

void LabelInitializer::initializeAudioDeviceLabel(lv_obj_t* label) {
    initializeLabel(label, LABEL_TYPE_DASH);
}

void LabelInitializer::initializeStatusLabel(lv_obj_t* label) {
    initializeLabel(label, LABEL_TYPE_DASH);
}

void LabelInitializer::initializeIndicatorLabel(lv_obj_t* label) {
    initializeLabel(label, LABEL_TYPE_EMPTY);
}

void LabelInitializer::initializeNetworkLabel(lv_obj_t* label) {
    initializeLabel(label, LABEL_TYPE_DASH);
}

}  // namespace Components
}  // namespace UI