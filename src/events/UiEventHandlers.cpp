#include "UiEventHandlers.h"
#include <ui/ui.h>
#include <esp32_smartdisplay.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include "../messaging/MessageBus.h"
#include "../hardware/DeviceManager.h"
#include "../application/AudioStatusManager.h"

static const char* TAG = "UIEventHandlers";

namespace Events {
namespace UI {

// Button click handler that publishes audio status request
void btnRequestDataClickedHandler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button clicked - requesting audio status");

        // Use AudioStatusManager to publish the request
        Application::Audio::StatusManager::publishAudioStatusRequest();
    }
}

// Audio device dropdown selection change handler
void audioDeviceDropdownChangedHandler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(e);

        // Get the selected audio device name
        String selectedDevice = Application::Audio::StatusManager::getSelectedAudioDevice(dropdown);

        ESP_LOGI(TAG, "Audio device dropdown changed: %s", selectedDevice.c_str());

        // Update the selected device in the AudioStatusManager
        Application::Audio::StatusManager::setSelectedDevice(selectedDevice);
    }
}

// Volume arc change handler
void volumeArcChangedHandler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        // Check if events are suppressed to prevent infinite loops
        if (Application::Audio::StatusManager::isSuppressingArcEvents()) {
            return;
        }

        lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);

        // Get the arc value
        int volume = lv_arc_get_value(arc);

        ESP_LOGI(TAG, "Volume arc changed: %d", volume);

        // Set the volume for the selected device
        Application::Audio::StatusManager::setSelectedDeviceVolume(volume);
    }
}

}  // namespace UI
}  // namespace Events