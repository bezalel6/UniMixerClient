/**
 * Volume Slider Initialization
 * 
 * This file provides initialization for volume sliders to prevent
 * uninitialized values from being displayed.
 */

#include "AudioUI.h"
#include "VolumeWidgetMacros.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "VolumeSliderInit";

namespace Application {
namespace Audio {

void AudioUI::initializeVolumeSliders() {
    ESP_LOGI(TAG, "Initializing volume sliders with default values");
    
    // Initialize all volume sliders to a safe default value (0)
    // This prevents garbage values from being displayed before real data arrives
    
    if (ui_primaryVolumeSlider) {
        // ui_primaryVolumeSlider now points directly to the slider widget
        VOLUME_WIDGET_SET_RANGE(ui_primaryVolumeSlider, 0, 100);
        VOLUME_WIDGET_SET_VALUE(ui_primaryVolumeSlider, 0);
        ESP_LOGI(TAG, "Primary volume slider initialized");
    }
    
    if (ui_containerSingleVolumeSlider) {
        lv_obj_t* slider = ui_comp_get_child(ui_containerSingleVolumeSlider, UI_COMP_VOLUMESLIDER_PRIMARYVOLUMESLIDER);
        if (slider) {
            VOLUME_WIDGET_SET_RANGE(slider, 0, 100);
            VOLUME_WIDGET_SET_VALUE(slider, 0);
            ESP_LOGI(TAG, "Single volume slider initialized");
        }
    }
    
    if (ui_containerBalanceVolumeSlider) {
        lv_obj_t* slider = ui_comp_get_child(ui_containerBalanceVolumeSlider, UI_COMP_VOLUMESLIDER_PRIMARYVOLUMESLIDER);
        if (slider) {
            VOLUME_WIDGET_SET_RANGE(slider, 0, 100);
            VOLUME_WIDGET_SET_VALUE(slider, 50); // Balance starts at center
            ESP_LOGI(TAG, "Balance volume slider initialized");
        }
    }
    
    // Update labels to match
    if (ui_lblPrimaryVolumeSlider) {
        lv_label_set_text(ui_lblPrimaryVolumeSlider, "0%");
    }
    if (ui_lblSingleVolumeSlider) {
        lv_label_set_text(ui_lblSingleVolumeSlider, "0%");
    }
    if (ui_lblBalanceVolumeSlider) {
        lv_label_set_text(ui_lblBalanceVolumeSlider, "50%");
    }
}

} // namespace Audio
} // namespace Application