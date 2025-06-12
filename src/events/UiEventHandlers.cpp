#include "UiEventHandlers.h"
#include <ui/ui.h>
#include <esp32_smartdisplay.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include "../messaging/MessageBus.h"
#include "../hardware/DeviceManager.h"
#include "../application/AudioStatusManager.h"

static const char* TAG = "UIEventHandlers";

// Helper function to get event name for debugging
static const char* getEventName(lv_event_code_t code) {
    switch (code) {
        case LV_EVENT_PRESSED:
            return "LV_EVENT_PRESSED";
        case LV_EVENT_PRESSING:
            return "LV_EVENT_PRESSING";
        case LV_EVENT_PRESS_LOST:
            return "LV_EVENT_PRESS_LOST";
        case LV_EVENT_SHORT_CLICKED:
            return "LV_EVENT_SHORT_CLICKED";
        case LV_EVENT_LONG_PRESSED:
            return "LV_EVENT_LONG_PRESSED";
        case LV_EVENT_LONG_PRESSED_REPEAT:
            return "LV_EVENT_LONG_PRESSED_REPEAT";
        case LV_EVENT_CLICKED:
            return "LV_EVENT_CLICKED";
        case LV_EVENT_RELEASED:
            return "LV_EVENT_RELEASED";
        case LV_EVENT_SCROLL_BEGIN:
            return "LV_EVENT_SCROLL_BEGIN";
        case LV_EVENT_SCROLL_END:
            return "LV_EVENT_SCROLL_END";
        case LV_EVENT_SCROLL:
            return "LV_EVENT_SCROLL";
        case LV_EVENT_GESTURE:
            return "LV_EVENT_GESTURE";
        case LV_EVENT_KEY:
            return "LV_EVENT_KEY";
        case LV_EVENT_FOCUSED:
            return "LV_EVENT_FOCUSED";
        case LV_EVENT_DEFOCUSED:
            return "LV_EVENT_DEFOCUSED";
        case LV_EVENT_LEAVE:
            return "LV_EVENT_LEAVE";
        case LV_EVENT_HIT_TEST:
            return "LV_EVENT_HIT_TEST";
        case LV_EVENT_COVER_CHECK:
            return "LV_EVENT_COVER_CHECK";
        case LV_EVENT_REFR_EXT_DRAW_SIZE:
            return "LV_EVENT_REFR_EXT_DRAW_SIZE";
        case LV_EVENT_DRAW_MAIN_BEGIN:
            return "LV_EVENT_DRAW_MAIN_BEGIN";
        case LV_EVENT_DRAW_MAIN:
            return "LV_EVENT_DRAW_MAIN";
        case LV_EVENT_DRAW_MAIN_END:
            return "LV_EVENT_DRAW_MAIN_END";
        case LV_EVENT_DRAW_POST_BEGIN:
            return "LV_EVENT_DRAW_POST_BEGIN";
        case LV_EVENT_DRAW_POST:
            return "LV_EVENT_DRAW_POST";
        case LV_EVENT_DRAW_POST_END:
            return "LV_EVENT_DRAW_POST_END";
        case LV_EVENT_VALUE_CHANGED:
            return "LV_EVENT_VALUE_CHANGED";
        case LV_EVENT_INSERT:
            return "LV_EVENT_INSERT";
        case LV_EVENT_REFRESH:
            return "LV_EVENT_REFRESH";
        case LV_EVENT_READY:
            return "LV_EVENT_READY";
        case LV_EVENT_CANCEL:
            return "LV_EVENT_CANCEL";
        case LV_EVENT_DELETE:
            return "LV_EVENT_DELETE";
        case LV_EVENT_CHILD_CHANGED:
            return "LV_EVENT_CHILD_CHANGED";
        case LV_EVENT_SIZE_CHANGED:
            return "LV_EVENT_SIZE_CHANGED";
        case LV_EVENT_STYLE_CHANGED:
            return "LV_EVENT_STYLE_CHANGED";
        case LV_EVENT_GET_SELF_SIZE:
            return "LV_EVENT_GET_SELF_SIZE";
        default:
            return "UNKNOWN_EVENT";
    }
}

namespace Events {
namespace UI {

// Button click handler that publishes audio status request
void btnRequestDataClickedHandler(lv_event_t* e) {
    ON_EVENT(LV_EVENT_CLICKED);

    UI_LOG(TAG, "Button clicked - requesting audio status");

    // Use AudioStatusManager to publish the request
    Application::Audio::StatusManager::publishAudioStatusRequest();
}

// Audio device dropdown selection change handler
void audioDeviceDropdownChangedHandler(lv_event_t* e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_VALUE_CHANGED, dropdown);

    // Get the selected audio device name
    String selectedDevice = Application::Audio::StatusManager::getSelectedAudioDevice(dropdown);

    UI_LOG(TAG, "Audio device dropdown changed: %s", selectedDevice.c_str());

    // Update the selected device in the AudioStatusManager
    Application::Audio::StatusManager::setSelectedDevice(selectedDevice);
    auto status = Application::Audio::StatusManager::getCurrentAudioStatus();
}

// Volume arc change handler
void volumeArcChangedHandler(lv_event_t* e) {
    ON_EVENT(LV_EVENT_VALUE_CHANGED);

    // Check if events are suppressed to prevent infinite loops
    if (Application::Audio::StatusManager::isSuppressingArcEvents()) {
        return;
    }

    lv_obj_t* arc = GET_UI_WIDGET();

    // Get the arc value
    int volume = lv_arc_get_value(arc);

    UI_LOG(TAG, "Volume arc changed: %d", volume);

    // Set the volume for the selected device
    Application::Audio::StatusManager::setSelectedDeviceVolume(volume);
}

// Tab switch event handler
void tabSwitchHandler(lv_event_t* e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_CLICKED, target);

    UI_LOG(TAG, "Tab event received: %s (%d) on target: %p", getEventName(code), code, target);

    uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);
    Application::Audio::StatusManager::setCurrentTab(static_cast<TabState>(activeTab));
}

// Get current tab state
TabState getCurrentTab(void) {
    return Application::Audio::StatusManager::getCurrentTab();
}

// Set current tab state
void setCurrentTab(TabState tab) {
    Application::Audio::StatusManager::setCurrentTab(tab);
}

// Get tab name string
const char* getTabName(TabState tab) {
    return Application::Audio::StatusManager::getTabName(tab);
}

}  // namespace UI
}  // namespace Events