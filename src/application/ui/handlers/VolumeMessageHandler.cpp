#include "VolumeMessageHandler.h"
#include <esp_log.h>
#include <ui/ui.h>

// Volume widget macros
#define VOLUME_WIDGET_SET_VALUE(slider, value) lv_slider_set_value(slider, value, LV_ANIM_OFF)

namespace Application {
namespace UI {
namespace Handlers {

using namespace Application::LVGLMessageHandler;

static const char* TAG = "VolumeMessageHandler";

// Static member definitions
std::unordered_map<uint32_t, VolumeMessageHandler::TabVolumeUpdater> VolumeMessageHandler::tabVolumeUpdaters;
std::unordered_map<int, VolumeMessageHandler::VolumeExtractor> VolumeMessageHandler::volumeExtractors;
bool VolumeMessageHandler::mapsInitialized = false;

void VolumeMessageHandler::initializeMaps() {
    if (mapsInitialized) return;
    
    // Initialize volume extractors
    volumeExtractors = {
        {Application::LVGLMessageHandler::MSG_UPDATE_MASTER_VOLUME, extractMasterVolume},
        {Application::LVGLMessageHandler::MSG_UPDATE_SINGLE_VOLUME, extractSingleVolume},
        {Application::LVGLMessageHandler::MSG_UPDATE_BALANCE_VOLUME, extractBalanceVolume}
    };
    
    // Initialize tab volume updaters
    tabVolumeUpdaters = {
        {0, LVGLMessageHandler::updateMasterVolume},  // Master tab
        {1, LVGLMessageHandler::updateSingleVolume},  // Single tab
        {2, LVGLMessageHandler::updateBalanceVolume}  // Balance tab
    };
    
    mapsInitialized = true;
}

void VolumeMessageHandler::registerHandler() {
    initializeMaps();
    // Registration will be handled by MessageHandlerRegistry
}

void VolumeMessageHandler::handleMasterVolume(const LVGLMessage_t* msg) {
    updateVolumeSlider(ui_primaryVolumeSlider, msg);
}

void VolumeMessageHandler::handleSingleVolume(const LVGLMessage_t* msg) {
    updateVolumeSlider(ui_singleVolumeSlider, msg);
}

void VolumeMessageHandler::handleBalanceVolume(const LVGLMessage_t* msg) {
    updateVolumeSlider(ui_balanceVolumeSlider, msg);
}

int VolumeMessageHandler::extractMasterVolume(const LVGLMessage_t* msg) {
    return msg->data.master_volume.volume;
}

int VolumeMessageHandler::extractSingleVolume(const LVGLMessage_t* msg) {
    return msg->data.single_volume.volume;
}

int VolumeMessageHandler::extractBalanceVolume(const LVGLMessage_t* msg) {
    return msg->data.balance_volume.volume;
}

void VolumeMessageHandler::updateVolumeSlider(lv_obj_t* slider, const LVGLMessage_t* msg) {
    if (!slider) return;
    
    auto extractor = volumeExtractors.find(msg->type);
    if (extractor != volumeExtractors.end()) {
        int volume = extractor->second(msg);
        VOLUME_WIDGET_SET_VALUE(slider, volume);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

bool VolumeMessageHandler::updateCurrentTabVolume(int volume) {
    // Get the currently active tab from the UI
    if (ui_tabsModeSwitch) {
        uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);
        
        auto tabUpdater = tabVolumeUpdaters.find(activeTab);
        if (tabUpdater != tabVolumeUpdaters.end()) {
            return tabUpdater->second(volume);
        } else {
            ESP_LOGW(TAG, "Unknown active tab: %d, defaulting to Master volume", activeTab);
            return LVGLMessageHandler::updateMasterVolume(volume);  // Default to Master tab
        }
    } else {
        ESP_LOGW(TAG, "Tab view not available, defaulting to Master volume");
        return LVGLMessageHandler::updateMasterVolume(volume);  // Default to Master tab
    }
}

} // namespace Handlers
} // namespace UI
} // namespace Application