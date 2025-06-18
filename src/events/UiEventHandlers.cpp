#include "UiEventHandlers.h"
#include "../application/AudioController.h"
#include "../application/LVGLMessageHandler.h"
#include "../hardware/DeviceManager.h"
#include "../messaging/MessageBus.h"
#include <ArduinoJson.h>
#include <esp32_smartdisplay.h>
#include <esp_log.h>
#include <ui/ui.h>

// Helper function to get event name for debugging
static const char *getEventName(lv_event_code_t code) {
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
void btnRequestDataClickedHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_CLICKED);

    UI_LOG("UIEventHandlers", "Button clicked - requesting audio status");

    // Use AudioController to publish the request
    Application::Audio::AudioController::getInstance().publishAudioStatusRequest();
}

// Audio device dropdown selection change handler
void audioDeviceDropdownChangedHandler(lv_event_t *e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_VALUE_CHANGED, dropdown);

    // Check if dropdown events are suppressed to prevent infinite loops
    if (Application::Audio::AudioController::getInstance().isSuppressingDropdownEvents()) {
        ESP_LOGD(TAG, "Suppressing dropdown event");
        return;
    }

    // Get the selected audio device name using the new method
    String selectedText =
        Application::Audio::AudioController::getInstance().getDropdownSelection(dropdown);
    ESP_LOGI("UIEventHandlers", "Dropdown changed to: %s", selectedText.c_str());

    // Update the selection using the clean centralized interface
    Application::Audio::AudioController::getInstance().setDropdownSelection(dropdown,
                                                                            selectedText);
}

// Volume arc visual handler - updates labels in real-time during dragging
void volumeArcVisualHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_VALUE_CHANGED);

    lv_obj_t *arc = GET_UI_WIDGET();
    int volume = lv_arc_get_value(arc);

    // Update the corresponding label immediately for visual feedback
    char volumeText[16];
    snprintf(volumeText, sizeof(volumeText), "%d%%", volume);

    // Determine which label to update based on which arc was changed
    if (arc == ui_primaryVolumeSlider && ui_lblPrimaryVolumeSlider) {
        lv_label_set_text(ui_lblPrimaryVolumeSlider, volumeText);
    } else if (arc == ui_singleVolumeSlider && ui_lblSingleVolumeSlider) {
        lv_label_set_text(ui_lblSingleVolumeSlider, volumeText);
    } else if (arc == ui_balanceVolumeSlider && ui_lblBalanceVolumeSlider) {
        lv_label_set_text(ui_lblBalanceVolumeSlider, volumeText);
    }

    UI_LOG("UIEventHandlers", "Volume arc visual update: %d", volume);
}

// Volume arc change handler - only processes actual volume changes on release
void volumeArcChangedHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_RELEASED);

    // Check if events are suppressed to prevent infinite loops
    if (Application::Audio::AudioController::getInstance().isSuppressingArcEvents()) {
        ESP_LOGD(TAG, "Suppressing arc event during value change");
        return;
    }

    lv_obj_t *arc = GET_UI_WIDGET();

    // Get the arc value
    int volume = lv_arc_get_value(arc);

    UI_LOG("UIEventHandlers", "Volume arc released - setting volume: %d", volume);

    // Set the volume for the selected device
    Application::Audio::AudioController::getInstance().setSelectedDeviceVolume(volume);
}

// Tab switch event handler
void tabSwitchHandler(lv_event_t *e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_CLICKED, target);

    UI_LOG("UIEventHandlers", "Tab event received: %s (%d) on target: %p",
           getEventName(code), code, target);

    uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);
    Application::Audio::AudioController::getInstance().setCurrentTab(
        static_cast<TabState>(activeTab));
}

// Get current tab state
TabState getCurrentTab(void) {
    return Application::Audio::AudioController::getInstance().getCurrentTab();
}

// Set current tab state
void setCurrentTab(TabState tab) {
    Application::Audio::AudioController::getInstance().setCurrentTab(tab);
}

// Get tab name string
const char *getTabName(TabState tab) {
    return Application::Audio::AudioController::getInstance().getTabName(tab);
}

// State overview long-press handler
void stateOverviewLongPressHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_LONG_PRESSED);

    UI_LOG("UIEventHandlers", "Long press detected - showing state overview");

    // Show state overview overlay
    Application::LVGLMessageHandler::showStateOverview();
}

}  // namespace UI
}  // namespace Events