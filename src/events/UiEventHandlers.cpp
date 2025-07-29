#include "UiEventHandlers.h"
#include "../application/audio/AudioManager.h"
#include "../application/audio/AudioUI.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../application/debug/LogoDebugView.h"
#include "../messaging/Message.h"
#include "../messaging/SimplifiedSerialEngine.h"
#include "VolumeWidgetMacros.h"

#include "../hardware/DeviceManager.h"

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

static const char *TAG = "UIEventHandlers";

// Volume debouncing variables (REMOVED - no longer needed)
static volatile bool volumeChangeRequested = false;
static volatile int requestedVolumeValue = -1;

// Includes for FreeRTOS (kept for other potential usage)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Button click handler that publishes audio status request
void btnRequestDataClickedHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_CLICKED);

    UI_LOG("UIEventHandlers",
           "Button clicked - triggering reactive data refresh");

    // Use AudioManager to publish the request
    Application::Audio::AudioManager &audioManager =
        Application::Audio::AudioManager::getInstance();
    audioManager.publishStatusRequest();

    // Also trigger smart auto-selection with current data if available
    // This helps users who click the button expecting immediate smart behavior
    if (audioManager.hasDevices()) {
        ESP_LOGI(TAG, "Devices available - triggering smart auto-selection");
        audioManager.performSmartAutoSelection();

        // Trigger UI refresh to ensure everything is up to date
        Application::Audio::AudioUI::getInstance().refreshAllUI();
    }
}

// Audio device dropdown selection change handler
void audioDeviceDropdownChangedHandler(lv_event_t *e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_VALUE_CHANGED, dropdown);

    // Get the selected audio device name using the new method
    String selectedText =
        Application::Audio::AudioUI::getInstance().getDropdownSelection(dropdown);
    ESP_LOGE("UIEventHandlers",
             "Dropdown changed to: %s - triggering reactive updates",
             selectedText.c_str());

    // Update the selection using the clean centralized interface
    Application::Audio::AudioUI::getInstance().onDeviceDropdownChanged(
        dropdown, selectedText);

    // For Balance tab: if user selected device1 but device2 is empty,
    // intelligently auto-select device2
    Application::Audio::AudioManager &audioManager =
        Application::Audio::AudioManager::getInstance();
    if (audioManager.getCurrentTab() == Events::UI::TabState::BALANCE) {
        if (dropdown == ui_selectAudioDevice1 &&
            !audioManager.getState().selectedDevice2) {
            ESP_LOGI(TAG, "Balance device1 selected - auto-selecting device2");
            audioManager.performSmartAutoSelection();
        } else if (dropdown == ui_selectAudioDevice2 &&
                   !audioManager.getState().selectedDevice1) {
            ESP_LOGI(TAG, "Balance device2 selected - auto-selecting device1");
            audioManager.performSmartAutoSelection();
        }
    }

    // Note: Status will be updated when the server processes the selection change
}

//// Volume arc visual handler - updates labels in real-time during dragging
// NO LONGER NEEDED. labels are updating along with the widgets using lvgl events defined in squareline studio
void volumeArcVisualHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_VALUE_CHANGED);

    lv_obj_t *arc = GET_UI_WIDGET();
    int volume = VOLUME_WIDGET_GET_VALUE(arc);

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

    ESP_LOGD(TAG, "Volume arc visual update: %d", volume);
}

// Volume arc change handler - only processes actual volume changes on release
void volumeArcChangedHandler(lv_event_t *e) {
    ON_EVENT(LV_EVENT_RELEASED);

    lv_obj_t *arc = GET_UI_WIDGET();

    // Get the arc value
    int volume = VOLUME_WIDGET_GET_VALUE(arc);

    UI_LOG("UIEventHandlers", "Volume arc released - processing volume: %d",
           volume);
    auto &audioManager = Application::Audio::AudioManager::getInstance();
    String processName;  // can be left empty for default device
    if (audioManager.getCurrentTab() != TabState::MASTER) {
        processName = audioManager.getCurrentDevice();
    }

    Messaging::Message::createVolumeChange(processName, volume, "").send();
}

// Tab switch event handler
void tabSwitchHandler(lv_event_t *e) {
    ON_EVENT_GET_WIDGET(LV_EVENT_CLICKED, target);

    UI_LOG("UIEventHandlers", "Tab event received: %s (%d) on target: %p",
           getEventName(code), code, target);

    uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);
    Application::Audio::AudioUI::getInstance().onTabChanged(
        static_cast<TabState>(activeTab));
}

// Get current tab state
TabState getCurrentTab(void) {
    return Application::Audio::AudioManager::getInstance().getCurrentTab();
}

// Set current tab state
void setCurrentTab(TabState tab) {
    Application::Audio::AudioManager::getInstance().setCurrentTab(tab);
}

// Get tab name string
const char *getTabName(TabState tab) {
    return Application::Audio::AudioManager::getInstance().getTabName(tab);
}

// State overview long-press handler
void openSettings(lv_event_t *e) {
    ON_EVENT(LV_EVENT_CLICKED);

    UI_LOG("UIEventHandlers",
           "Settings button detected - showing state overview");

    // Show state overview overlay
    Application::LVGLMessageHandler::showStateOverview();
}

// Logo Debug View handler
void openLogoDebug(lv_event_t *e) {
    ON_EVENT(LV_EVENT_CLICKED);

    UI_LOG("UIEventHandlers",
           "Logo Debug button clicked - showing logo debug view");

    // Show logo debug view
    Application::Debug::LogoDebugView::getInstance().show();
}

// Cleanup function - simplified since messaging queue is handled by
// SerialEngine
void cleanupVolumeDebouncing() {
    // Reset volume change flags
    volumeChangeRequested = false;
    requestedVolumeValue = -1;

    // All message queue cleanup is now handled by SerialEngine
    ESP_LOGI(TAG, "Volume debouncing cleaned up");
}

}  // namespace UI
}  // namespace Events
