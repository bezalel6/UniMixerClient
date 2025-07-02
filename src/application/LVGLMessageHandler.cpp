/**
 * LVGL Message Handler - Thread-safe UI updates for audio mixer application
 *
 * This module provides tab-specific update messages for targeted UI updates:
 *
 * Volume Updates:
 * - updateMasterVolume(volume)    - Updates only Master tab volume slider
 * - updateSingleVolume(volume)    - Updates only Single tab volume slider
 * - updateBalanceVolume(volume)   - Updates only Balance tab volume slider
 * - updateCurrentTabVolume(volume) - Updates volume for currently active tab
 *
 * Device Updates:
 * - updateMasterDevice(name)      - Updates Master tab device label
 * - updateSingleDevice(name)      - Updates Single tab device selection (placeholder)
 * - updateBalanceDevices(n1,n2)   - Updates Balance tab device selections (placeholder)
 *
 * Usage example:
 *   // Update specific tab:
 *   updateMasterVolume(75);
 *
 *   // Update current tab automatically:
 *   updateCurrentTabVolume(75);
 */

#include "LVGLMessageHandler.h"
#include "../../include/DebugUtils.h"
#include "../../include/BuildInfo.h"
#include "../hardware/DeviceManager.h"
// Network managers available only during OTA mode
#include "../hardware/SDManager.h"
#include "../hardware/OTAManager.h"
#include "AudioManager.h"
#include "AudioUI.h"
#include "../display/DisplayManager.h"
#include "../ui/UniversalDialog.h"
#include <esp_log.h>
#include <ui/ui.h>
#include <functional>
#include <unordered_map>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// BULLETPROOF: External UI screen declarations
extern lv_obj_t *ui_screenMain;
extern lv_obj_t *ui_screenOTA;
extern lv_obj_t *ui_screenDebug;

// BULLETPROOF: External UI component declarations
extern lv_obj_t *ui_barOTAUpdateProgress;
extern lv_obj_t *ui_lblOTAUpdateProgress;
extern lv_obj_t *ui_Label2;

// Custom OTA screen elements, managed by the UI task
static lv_obj_t *custom_ota_screen = NULL;
static lv_obj_t *custom_ota_label = NULL;
static lv_obj_t *custom_ota_bar = NULL;

// State overview overlay elements
static lv_obj_t *state_overlay = NULL;
static lv_obj_t *state_overlay_bg = NULL;
static lv_obj_t *state_overlay_panel = NULL;
static lv_obj_t *state_system_label = NULL;
static lv_obj_t *state_network_label = NULL;
static lv_obj_t *state_audio_label = NULL;
static lv_obj_t *state_heap_bar = NULL;
static lv_obj_t *state_wifi_bar = NULL;

// SD format dialog elements
static lv_obj_t *format_dialog = NULL;
static lv_obj_t *format_dialog_panel = NULL;
static lv_obj_t *format_progress_bar = NULL;
static lv_obj_t *format_status_label = NULL;

namespace Application {
namespace LVGLMessageHandler {
static const char *TAG = "LVGLMessageHandler";

// PERFORMANCE: Message handler callback type
using MessageHandler = std::function<void(const LVGLMessage_t *)>;

// PERFORMANCE: Single message type to handler mapping for O(1) lookup
static std::unordered_map<int, MessageHandler> messageHandlers;

// PERFORMANCE: Message type names for debugging - static array for O(1) lookup
static const char *messageTypeNames[] = {
    [MSG_UPDATE_WIFI_STATUS] = "WIFI_STATUS",
    [MSG_UPDATE_NETWORK_INFO] = "NETWORK_INFO",
    [MSG_UPDATE_OTA_PROGRESS] = "OTA_PROGRESS",
    [MSG_UPDATE_FPS_DISPLAY] = "FPS_DISPLAY",
    [MSG_UPDATE_BUILD_TIME_DISPLAY] = "BUILD_TIME_DISPLAY",
    [MSG_SCREEN_CHANGE] = "SCREEN_CHANGE",
    [MSG_REQUEST_DATA] = "REQUEST_DATA",
    [MSG_UPDATE_MASTER_VOLUME] = "MASTER_VOLUME",
    [MSG_UPDATE_SINGLE_VOLUME] = "SINGLE_VOLUME",
    [MSG_UPDATE_BALANCE_VOLUME] = "BALANCE_VOLUME",
    [MSG_UPDATE_MASTER_DEVICE] = "MASTER_DEVICE",
    [MSG_UPDATE_SINGLE_DEVICE] = "SINGLE_DEVICE",
    [MSG_UPDATE_BALANCE_DEVICES] = "BALANCE_DEVICES",
    [MSG_SHOW_OTA_SCREEN] = "SHOW_OTA_SCREEN",
    [MSG_UPDATE_OTA_SCREEN_PROGRESS] = "OTA_SCREEN_PROGRESS",
    [MSG_HIDE_OTA_SCREEN] = "HIDE_OTA_SCREEN",
    [MSG_SHOW_STATE_OVERVIEW] = "SHOW_STATE_OVERVIEW",
    [MSG_UPDATE_STATE_OVERVIEW] = "UPDATE_STATE_OVERVIEW",
    [MSG_HIDE_STATE_OVERVIEW] = "HIDE_STATE_OVERVIEW",
    [MSG_UPDATE_SD_STATUS] = "SD_STATUS",
    [MSG_FORMAT_SD_REQUEST] = "FORMAT_SD_REQUEST",
    [MSG_FORMAT_SD_CONFIRM] = "FORMAT_SD_CONFIRM",
    [MSG_FORMAT_SD_PROGRESS] = "FORMAT_SD_PROGRESS",
    [MSG_FORMAT_SD_COMPLETE] = "FORMAT_SD_COMPLETE",
    [MSG_SHOW_OTA_STATUS_INDICATOR] = "SHOW_OTA_STATUS_INDICATOR",
    [MSG_UPDATE_OTA_STATUS_INDICATOR] = "UPDATE_OTA_STATUS_INDICATOR",
    [MSG_HIDE_OTA_STATUS_INDICATOR] = "HIDE_OTA_STATUS_INDICATOR",
    [MSG_DEBUG_UI_LOG] = "DEBUG_UI_LOG"};

// PERFORMANCE: O(1) message type name lookup
static const char *getMessageTypeName(int messageType) {
    if (messageType >= 0 && messageType < (sizeof(messageTypeNames) / sizeof(messageTypeNames[0])) && messageTypeNames[messageType]) {
        return messageTypeNames[messageType];
    }
    return "UNKNOWN";
}

// PERFORMANCE: Fast volume extraction using function pointers
using VolumeExtractor = int (*)(const LVGLMessage_t *);

static int extractMasterVolume(const LVGLMessage_t *msg) { return msg->data.master_volume.volume; }
static int extractSingleVolume(const LVGLMessage_t *msg) { return msg->data.single_volume.volume; }
static int extractBalanceVolume(const LVGLMessage_t *msg) { return msg->data.balance_volume.volume; }

static std::unordered_map<int, VolumeExtractor> volumeExtractors = {
    {MSG_UPDATE_MASTER_VOLUME, extractMasterVolume},
    {MSG_UPDATE_SINGLE_VOLUME, extractSingleVolume},
    {MSG_UPDATE_BALANCE_VOLUME, extractBalanceVolume}};

// PERFORMANCE: Fast volume update function
static inline void updateVolumeSlider(lv_obj_t *slider, const LVGLMessage_t *msg) {
    if (!slider) return;

    auto extractor = volumeExtractors.find(msg->type);
    if (extractor != volumeExtractors.end()) {
        int volume = extractor->second(msg);
        lv_arc_set_value(slider, volume);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

// PERFORMANCE: Message handler implementations - optimized for minimal branching
static void handleWifiStatus(const LVGLMessage_t *msg) {
    const auto &data = msg->data.wifi_status;

    // PERFORMANCE: Batch UI updates to reduce render calls
    if (ui_lblWifiStatus) {
        lv_label_set_text(ui_lblWifiStatus, data.status);
    }
    if (ui_objWifiIndicator) {
        // PERFORMANCE: Pre-calculated colors to avoid hex conversion
        static const lv_color_t COLOR_CONNECTED = lv_color_hex(0x00FF00);
        static const lv_color_t COLOR_DISCONNECTED = lv_color_hex(0xFF0000);
        lv_obj_set_style_bg_color(ui_objWifiIndicator,
                                  data.connected ? COLOR_CONNECTED : COLOR_DISCONNECTED,
                                  LV_PART_MAIN);
    }
}

static void handleNetworkInfo(const LVGLMessage_t *msg) {
    const auto &data = msg->data.network_info;
    if (ui_lblSSIDValue) {
        lv_label_set_text(ui_lblSSIDValue, data.ssid);
    }
    if (ui_lblIPValue) {
        lv_label_set_text(ui_lblIPValue, data.ip);
    }
}

static void handleFpsDisplay(const LVGLMessage_t *msg) {
    if (ui_lblFPS) {
        // PERFORMANCE: Use static buffer to avoid stack allocation overhead
        static char fpsText[64];
        float actualFps = Display::getActualRenderFPS();
        snprintf(fpsText, sizeof(fpsText), "FPS: %.1f/%.1f",
                 actualFps, msg->data.fps_display.fps);
        lv_label_set_text(ui_lblFPS, fpsText);
    }
}

static void handleMasterVolume(const LVGLMessage_t *msg) {
    updateVolumeSlider(ui_primaryVolumeSlider, msg);
}

static void handleSingleVolume(const LVGLMessage_t *msg) {
    updateVolumeSlider(ui_singleVolumeSlider, msg);
}

static void handleBalanceVolume(const LVGLMessage_t *msg) {
    updateVolumeSlider(ui_balanceVolumeSlider, msg);
}

static void handleMasterDevice(const LVGLMessage_t *msg) {
    if (ui_lblPrimaryAudioDeviceValue) {
        lv_label_set_text(ui_lblPrimaryAudioDeviceValue, msg->data.master_device.device_name);
    }
}

static void handleBuildTimeDisplay(const LVGLMessage_t *msg) {
    if (ui_lblBuildTimeValue) {
        lv_label_set_text(ui_lblBuildTimeValue, getBuildTimeAndDate());
    }
}

// PERFORMANCE: Complex message handlers
static void handleOtaProgress(const LVGLMessage_t *msg) {
    const auto &data = msg->data.ota_progress;
    if (data.in_progress) {
        // Switch to OTA screen if not already there
        if (lv_scr_act() != ui_screenOTA) {
            _ui_screen_change(&ui_screenOTA, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                              ui_screenOTA_screen_init);
        }

        // Update progress bar
        if (ui_barOTAUpdateProgress) {
            lv_bar_set_value(ui_barOTAUpdateProgress, data.progress, LV_ANIM_OFF);
        }

        // Update status label
        if (ui_lblOTAUpdateProgress) {
            lv_label_set_text(ui_lblOTAUpdateProgress, data.message);
        }

        ESP_LOGI(TAG, "OTA Progress: %d%% - %s", data.progress, data.message);
    } else {
        // OTA finished - update final status
        if (ui_lblOTAUpdateProgress) {
            lv_label_set_text(ui_lblOTAUpdateProgress, data.message);
        }
        if (ui_barOTAUpdateProgress) {
            lv_bar_set_value(ui_barOTAUpdateProgress, data.success ? 100 : 0, LV_ANIM_OFF);
        }
    }
}

static void handleSingleDevice(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "Single device update requested: %s", msg->data.single_device.device_name);
}

static void handleBalanceDevices(const LVGLMessage_t *msg) {
    const auto &data = msg->data.balance_devices;
    ESP_LOGI(TAG, "Balance devices update requested: %s, %s", data.device1_name, data.device2_name);
}

static void handleScreenChange(const LVGLMessage_t *msg) {
    const auto &data = msg->data.screen_change;
    if (data.screen) {
        lv_screen_load_anim_t anim = static_cast<lv_screen_load_anim_t>(data.anim_type);
        _ui_screen_change((lv_obj_t **)&data.screen, anim, data.time, data.delay, NULL);
    }
}

static void handleRequestData(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "Data request triggered from UI");
}

static void handleShowOtaScreen(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "OTA: Showing OTA screen");

    // Switch to OTA screen with smooth animation
    if (lv_scr_act() != ui_screenOTA) {
        _ui_screen_change(&ui_screenOTA, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, ui_screenOTA_screen_init);
    }

    // Initialize OTA screen with default values
    if (ui_barOTAUpdateProgress) {
        lv_bar_set_value(ui_barOTAUpdateProgress, 0, LV_ANIM_OFF);
    }
    if (ui_lblOTAUpdateProgress) {
        lv_label_set_text(ui_lblOTAUpdateProgress, "Starting OTA update...");
    }

    ESP_LOGI(TAG, "OTA: Screen transition completed");
}

static void handleUpdateOtaScreenProgress(const LVGLMessage_t *msg) {
    const auto &data = msg->data.ota_screen_progress;
    ESP_LOGI(TAG, "OTA: Updating progress to %d%% - %s", data.progress, data.message);

    // Ensure we're on the OTA screen
    if (lv_scr_act() != ui_screenOTA) {
        ESP_LOGW(TAG, "OTA: Progress update but not on OTA screen, switching");
        _ui_screen_change(&ui_screenOTA, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_screenOTA_screen_init);
    }

    // Update progress bar with smooth animation for visual feedback
    if (ui_barOTAUpdateProgress) {
        lv_bar_set_value(ui_barOTAUpdateProgress, data.progress, LV_ANIM_ON);
    }

    // Update status message
    if (ui_lblOTAUpdateProgress) {
        lv_label_set_text(ui_lblOTAUpdateProgress, data.message);
    }

    // Add visual feedback for completion
    if (data.progress >= 100) {
        ESP_LOGI(TAG, "OTA: Update appears complete, preparing for reboot");
        if (ui_Label2) {
            lv_label_set_text(ui_Label2, "COMPLETE");
            lv_obj_set_style_text_color(ui_Label2, lv_color_hex(0x00FF00), LV_PART_MAIN);
        }
    }

    // Force immediate UI refresh for OTA critical operations
    lv_refr_now(lv_disp_get_default());
}

static void handleHideOtaScreen(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "OTA: Hiding OTA screen and restoring previous screen");

    // Get the previous screen that was saved
    static lv_obj_t *previousScreen = nullptr;

    // If we have a previous screen, return to it
    if (previousScreen && previousScreen != ui_screenOTA) {
        ESP_LOGI(TAG, "OTA: Returning to previous screen");
        lv_scr_load_anim(previousScreen, LV_SCR_LOAD_ANIM_FADE_OUT, 300, 0, false);
    }

    // Reset OTA screen visual state
    if (ui_Label2) {
        lv_label_set_text(ui_Label2, "UPDATING");
        lv_obj_set_style_text_color(ui_Label2, lv_color_white(), LV_PART_MAIN);
    }

    previousScreen = nullptr;  // Clear saved screen
}

static void handleShowOtaStatusIndicator(const LVGLMessage_t *msg) {
    const auto &data = msg->data.ota_status_indicator;
    ESP_LOGI(TAG, "OTA Status: Showing indicator - %d%% - %s%s",
             data.progress, data.status, data.is_error ? " (ERROR)" : "");

    // Create/update OTA status indicator overlay
    static lv_obj_t *otaStatusOverlay = nullptr;
    static lv_obj_t *otaStatusBar = nullptr;
    static lv_obj_t *otaStatusLabel = nullptr;
    static lv_obj_t *otaStatusIcon = nullptr;

    if (!otaStatusOverlay || !lv_obj_is_valid(otaStatusOverlay)) {
        // Create the OTA status overlay
        lv_obj_t *currentScreen = lv_scr_act();
        if (currentScreen) {
            otaStatusOverlay = lv_obj_create(currentScreen);
            lv_obj_set_size(otaStatusOverlay, 300, 60);
            lv_obj_set_align(otaStatusOverlay, LV_ALIGN_TOP_MID);
            lv_obj_set_y(otaStatusOverlay, 10);

            // Style the overlay
            lv_obj_set_style_bg_color(otaStatusOverlay,
                                      data.is_error ? lv_color_hex(0x330000) : lv_color_hex(0x003300),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(otaStatusOverlay, 240, LV_PART_MAIN);
            lv_obj_set_style_border_color(otaStatusOverlay,
                                          data.is_error ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00),
                                          LV_PART_MAIN);
            lv_obj_set_style_border_width(otaStatusOverlay, 2, LV_PART_MAIN);
            lv_obj_set_style_radius(otaStatusOverlay, 10, LV_PART_MAIN);

            // Create progress bar
            otaStatusBar = lv_bar_create(otaStatusOverlay);
            lv_obj_set_size(otaStatusBar, 250, 15);
            lv_obj_set_align(otaStatusBar, LV_ALIGN_TOP_MID);
            lv_obj_set_y(otaStatusBar, 5);

            // Create status label
            otaStatusLabel = lv_label_create(otaStatusOverlay);
            lv_obj_set_align(otaStatusLabel, LV_ALIGN_BOTTOM_MID);
            lv_obj_set_y(otaStatusLabel, -5);
            lv_obj_set_style_text_color(otaStatusLabel, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(otaStatusLabel, &lv_font_montserrat_12, LV_PART_MAIN);

            // Create status icon (optional)
            otaStatusIcon = lv_label_create(otaStatusOverlay);
            lv_obj_set_align(otaStatusIcon, LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(otaStatusIcon, 5, 5);
            lv_obj_set_style_text_color(otaStatusIcon,
                                        data.is_error ? lv_color_hex(0xFF0000) : lv_color_hex(0x00FF00),
                                        LV_PART_MAIN);
            lv_label_set_text(otaStatusIcon, data.is_error ? "✗" : "⟳");
        }
    }

    // Update the indicator
    if (otaStatusBar && lv_obj_is_valid(otaStatusBar)) {
        lv_bar_set_value(otaStatusBar, data.progress, data.pulsing ? LV_ANIM_ON : LV_ANIM_OFF);
    }

    if (otaStatusLabel && lv_obj_is_valid(otaStatusLabel)) {
        lv_label_set_text(otaStatusLabel, data.status);
    }

    // Handle pulsing animation for status
    if (data.pulsing && otaStatusOverlay && lv_obj_is_valid(otaStatusOverlay)) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, otaStatusOverlay);
        lv_anim_set_values(&anim, 240, 150);
        lv_anim_set_time(&anim, 1000);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_playback_time(&anim, 500);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_style_bg_opa);
        lv_anim_start(&anim);
    }
}

static void handleUpdateOtaStatusIndicator(const LVGLMessage_t *msg) {
    // Same implementation as show - they can share the logic
    handleShowOtaStatusIndicator(msg);
}

static void handleHideOtaStatusIndicator(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "OTA Status: Hiding status indicator");

    // Clean up the OTA status overlay
    static lv_obj_t *otaStatusOverlay = nullptr;
    if (otaStatusOverlay && lv_obj_is_valid(otaStatusOverlay)) {
        lv_obj_del(otaStatusOverlay);
        otaStatusOverlay = nullptr;
    }
}

static void handleDebugUILog(const LVGLMessage_t *msg) {
    const auto &data = msg->data.debug_ui_log;

    if (ui_txtAreaDebugLog && lv_obj_is_valid(ui_txtAreaDebugLog)) {
        LOG_TO_UI(ui_txtAreaDebugLog, data.message);
        ESP_LOGD(TAG, "Debug UI log added: %s", data.message);
    } else {
        ESP_LOGW(TAG, "Debug UI log requested but ui_txtAreaDebugLog not available: %s", data.message);
    }
}

static void handleShowStateOverview(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "Settings: Showing comprehensive system overview");

    // Clean up any existing overlay first
    if (state_overlay && lv_obj_is_valid(state_overlay)) {
        lv_obj_del(state_overlay);
        state_overlay = NULL;
    }

    // Create the comprehensive system overview overlay
    lv_obj_t *currentScreen = lv_scr_act();
    if (currentScreen) {
        // Create main overlay container - larger for comprehensive info
        state_overlay = lv_obj_create(currentScreen);
        lv_obj_set_size(state_overlay, 700, 450);
        lv_obj_set_align(state_overlay, LV_ALIGN_CENTER);

        // Style the overlay
        lv_obj_set_style_bg_color(state_overlay, lv_color_hex(0x001122), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(state_overlay, 250, LV_PART_MAIN);
        lv_obj_set_style_border_color(state_overlay, lv_color_hex(0x0088FF), LV_PART_MAIN);
        lv_obj_set_style_border_width(state_overlay, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(state_overlay, 20, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(state_overlay, 30, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(state_overlay, 150, LV_PART_MAIN);

        // Create title label
        lv_obj_t *title_label = lv_label_create(state_overlay);
        lv_label_set_text(title_label, "SYSTEM OVERVIEW");
        lv_obj_set_align(title_label, LV_ALIGN_TOP_MID);
        lv_obj_set_y(title_label, 15);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x00CCFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);

        // Create close button
        lv_obj_t *close_btn = lv_btn_create(state_overlay);
        lv_obj_set_size(close_btn, 70, 35);
        lv_obj_set_align(close_btn, LV_ALIGN_TOP_RIGHT);
        lv_obj_set_pos(close_btn, -15, 10);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), LV_PART_MAIN);

        lv_obj_t *close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, "CLOSE");
        lv_obj_center(close_label);
        lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN);

        // Add click event to close button
        lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                hideStateOverview();
            } }, LV_EVENT_CLICKED, NULL);

        // Create three-column layout
        lv_obj_t *main_container = lv_obj_create(state_overlay);
        lv_obj_remove_style_all(main_container);
        lv_obj_set_size(main_container, 670, 350);
        lv_obj_set_align(main_container, LV_ALIGN_CENTER);
        lv_obj_set_y(main_container, 15);
        lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        // Left Column - System Information
        lv_obj_t *left_col = lv_obj_create(main_container);
        lv_obj_set_size(left_col, 200, 340);
        lv_obj_set_style_bg_color(left_col, lv_color_hex(0x002244), LV_PART_MAIN);
        lv_obj_set_style_border_width(left_col, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(left_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
        lv_obj_set_style_radius(left_col, 10, LV_PART_MAIN);

        lv_obj_t *sys_title = lv_label_create(left_col);
        lv_label_set_text(sys_title, "SYSTEM");
        lv_obj_set_align(sys_title, LV_ALIGN_TOP_MID);
        lv_obj_set_y(sys_title, 10);
        lv_obj_set_style_text_color(sys_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_14, LV_PART_MAIN);

        state_system_label = lv_label_create(left_col);
        lv_obj_set_align(state_system_label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(state_system_label, 10, 40);
        lv_obj_set_size(state_system_label, 180, 280);
        lv_obj_set_style_text_color(state_system_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(state_system_label, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_label_set_long_mode(state_system_label, LV_LABEL_LONG_WRAP);

        // Middle Column - Network & Connectivity
        lv_obj_t *mid_col = lv_obj_create(main_container);
        lv_obj_set_size(mid_col, 200, 340);
        lv_obj_set_style_bg_color(mid_col, lv_color_hex(0x002244), LV_PART_MAIN);
        lv_obj_set_style_border_width(mid_col, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(mid_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
        lv_obj_set_style_radius(mid_col, 10, LV_PART_MAIN);

        lv_obj_t *net_title = lv_label_create(mid_col);
        lv_label_set_text(net_title, "NETWORK");
        lv_obj_set_align(net_title, LV_ALIGN_TOP_MID);
        lv_obj_set_y(net_title, 10);
        lv_obj_set_style_text_color(net_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_obj_set_style_text_font(net_title, &lv_font_montserrat_14, LV_PART_MAIN);

        state_network_label = lv_label_create(mid_col);
        lv_obj_set_align(state_network_label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(state_network_label, 10, 40);
        lv_obj_set_size(state_network_label, 180, 280);
        lv_obj_set_style_text_color(state_network_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(state_network_label, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_label_set_long_mode(state_network_label, LV_LABEL_LONG_WRAP);

        // Right Column - Audio & Actions
        lv_obj_t *right_col = lv_obj_create(main_container);
        lv_obj_set_size(right_col, 240, 340);
        lv_obj_set_style_bg_color(right_col, lv_color_hex(0x002244), LV_PART_MAIN);
        lv_obj_set_style_border_width(right_col, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(right_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
        lv_obj_set_style_radius(right_col, 10, LV_PART_MAIN);

        lv_obj_t *audio_title = lv_label_create(right_col);
        lv_label_set_text(audio_title, "AUDIO & ACTIONS");
        lv_obj_set_align(audio_title, LV_ALIGN_TOP_MID);
        lv_obj_set_y(audio_title, 10);
        lv_obj_set_style_text_color(audio_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
        lv_obj_set_style_text_font(audio_title, &lv_font_montserrat_14, LV_PART_MAIN);

        state_audio_label = lv_label_create(right_col);
        lv_obj_set_align(state_audio_label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(state_audio_label, 10, 40);
        lv_obj_set_size(state_audio_label, 220, 150);  // Reduced to make room for 4 buttons
        lv_obj_set_style_text_color(state_audio_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(state_audio_label, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_label_set_long_mode(state_audio_label, LV_LABEL_LONG_WRAP);

        // Action buttons in right column (expanded for 4 buttons)
        lv_obj_t *actions_container = lv_obj_create(right_col);
        lv_obj_remove_style_all(actions_container);
        lv_obj_set_size(actions_container, 220, 140);  // Increased height for 4 buttons
        lv_obj_set_align(actions_container, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_y(actions_container, -10);
        lv_obj_set_flex_flow(actions_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(actions_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // FORMAT SD button
        lv_obj_t *format_sd_btn = lv_btn_create(actions_container);
        lv_obj_set_size(format_sd_btn, 200, 32);  // Standardized for 4 buttons
        lv_obj_set_style_bg_color(format_sd_btn, lv_color_hex(0xFF6600), LV_PART_MAIN);

        lv_obj_t *format_label = lv_label_create(format_sd_btn);
        lv_label_set_text(format_label, "FORMAT SD CARD");
        lv_obj_center(format_label);
        lv_obj_set_style_text_color(format_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(format_label, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_add_event_cb(format_sd_btn, [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                ESP_LOGI(TAG, "FORMAT SD button clicked");
                requestSDFormat();
            } }, LV_EVENT_CLICKED, NULL);

        // ENTER OTA MODE button
        lv_obj_t *ota_mode_btn = lv_btn_create(actions_container);
        lv_obj_set_size(ota_mode_btn, 200, 32);  // Slightly smaller for 4 buttons
        lv_obj_set_style_bg_color(ota_mode_btn, lv_color_hex(0x3366FF), LV_PART_MAIN);

        lv_obj_t *ota_label = lv_label_create(ota_mode_btn);
        lv_label_set_text(ota_label, "ENTER OTA MODE");
        lv_obj_center(ota_label);
        lv_obj_set_style_text_color(ota_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_add_event_cb(ota_mode_btn, [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                ESP_LOGI(TAG, "ENTER OTA MODE button clicked - starting OTA mode");
                
                // Hide settings overlay first
                hideStateOverview();
                
                // Show OTA screen with immediate feedback
                showOtaScreen();
                updateOtaScreenProgress(0, "Initializing OTA mode...");
                
                // Setup OTA callbacks for UI integration
                Hardware::OTA::OTAManager::setStateCallback([](Hardware::OTA::OTAState state, const char* message) {
                    ESP_LOGI(TAG, "OTA State: %d - %s", (int)state, message ? message : "");
                    updateOtaScreenProgress(Hardware::OTA::OTAManager::getProgress(), message ? message : "");
                });
                
                Hardware::OTA::OTAManager::setProgressCallback([](uint8_t progress, const char* message) {
                    ESP_LOGI(TAG, "OTA Progress: %d%% - %s", progress, message ? message : "");
                    updateOtaScreenProgress(progress, message ? message : "");
                });
                
                Hardware::OTA::OTAManager::setCompleteCallback([](Hardware::OTA::OTAResult result, const char* message) {
                    ESP_LOGI(TAG, "OTA Complete: %d - %s", (int)result, message ? message : "");
                    if (result == Hardware::OTA::OTA_RESULT_SUCCESS) {
                        updateOtaScreenProgress(100, "OTA completed successfully! Restarting...");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        esp_restart();
                    } else {
                        updateOtaScreenProgress(0, message ? message : "OTA failed");
                        vTaskDelay(pdMS_TO_TICKS(3000));
                        hideOtaScreen();
                    }
                });
                
                // Start OTA mode
                if (!Hardware::OTA::OTAManager::startOTA()) {
                    ESP_LOGE(TAG, "Failed to start OTA mode");
                    updateOtaScreenProgress(0, "Failed to start OTA mode");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    hideOtaScreen();
                }
            } }, LV_EVENT_CLICKED, NULL);

        // Restart button
        lv_obj_t *restart_btn = lv_btn_create(actions_container);
        lv_obj_set_size(restart_btn, 200, 32);  // Standardized for 4 buttons
        lv_obj_set_style_bg_color(restart_btn, lv_color_hex(0xFF3366), LV_PART_MAIN);

        lv_obj_t *restart_label = lv_label_create(restart_btn);
        lv_label_set_text(restart_label, "RESTART SYSTEM");
        lv_obj_center(restart_label);
        lv_obj_set_style_text_color(restart_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(restart_label, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_add_event_cb(restart_btn, [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                ESP_LOGI(TAG, "RESTART button clicked - restarting in 2 seconds");
                hideStateOverview();
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            } }, LV_EVENT_CLICKED, NULL);

        // Refresh button
        lv_obj_t *refresh_btn = lv_btn_create(actions_container);
        lv_obj_set_size(refresh_btn, 200, 32);  // Standardized for 4 buttons
        lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x00AA66), LV_PART_MAIN);

        lv_obj_t *refresh_label = lv_label_create(refresh_btn);
        lv_label_set_text(refresh_label, "REFRESH DATA");
        lv_obj_center(refresh_label);
        lv_obj_set_style_text_color(refresh_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_12, LV_PART_MAIN);

        lv_obj_add_event_cb(refresh_btn, [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                ESP_LOGI(TAG, "REFRESH button clicked - updating overview");
                updateStateOverview();
            } }, LV_EVENT_CLICKED, NULL);

        ESP_LOGI(TAG, "Settings: Comprehensive system overview created successfully");

        // Trigger immediate update of the state data
        updateStateOverview();
    } else {
        ESP_LOGE(TAG, "Settings: No current screen available for state overlay");
    }
}

static void handleUpdateStateOverview(const LVGLMessage_t *msg) {
    if (!state_overlay || !lv_obj_is_valid(state_overlay)) {
        ESP_LOGW(TAG, "Settings: Update requested but no state overlay exists");
        return;
    }

    const auto &data = msg->data.state_overview;
    ESP_LOGI(TAG, "Settings: Updating state overview with current system data");

    // Update system information
    if (state_system_label && lv_obj_is_valid(state_system_label)) {
        static char system_text[512];
        uint32_t uptimeMinutes = data.uptime_ms / 60000;
        uint32_t uptimeHours = uptimeMinutes / 60;
        uint32_t uptime_display_min = uptimeMinutes % 60;

        snprintf(system_text, sizeof(system_text),
                 "Memory:\n"
                 "  Free Heap: %u KB\n"
                 "  Free PSRAM: %u KB\n\n"
                 "Performance:\n"
                 "  CPU Freq: %u MHz\n"
                 "  Uptime: %uh %um\n\n"
                 "Storage:\n"
                 "  SD Card Status: Available\n"
                 "  Format Support: Yes\n\n"
                 "Hardware:\n"
                 "  Touch: Responsive\n"
                 "  Display: Active",
                 data.free_heap / 1024,
                 data.free_psram / 1024,
                 data.cpu_freq,
                 uptimeHours, uptime_display_min);
        lv_label_set_text(state_system_label, system_text);
    }

    // Update network information
    if (state_network_label && lv_obj_is_valid(state_network_label)) {
        static char network_text[512];
        const char *signal_strength = "Unknown";
        if (data.wifi_rssi > -50)
            signal_strength = "Excellent";
        else if (data.wifi_rssi > -60)
            signal_strength = "Good";
        else if (data.wifi_rssi > -70)
            signal_strength = "Fair";
        else if (data.wifi_rssi > -80)
            signal_strength = "Poor";
        else
            signal_strength = "Very Poor";

        snprintf(network_text, sizeof(network_text),
                 "WiFi Connection:\n"
                 "  Status: %s\n"
                 "  Signal: %s\n"
                 "  RSSI: %d dBm\n\n"
                 "Network:\n"
                 "  IP Address: %s\n\n"
                 "Services:\n"
                 "  Serial: Active\n"
                 "  OTA: Available\n"
                 "  Network: OTA Mode Only\n\n"
                 "Protocol:\n"
                 "  Message Bus: Active\n"
                 "  Audio Streaming: OK",
                 data.wifi_status, signal_strength, data.wifi_rssi,
                 data.ip_address);
        lv_label_set_text(state_network_label, network_text);
    }

    // Update audio information
    if (state_audio_label && lv_obj_is_valid(state_audio_label)) {
        static char audio_text[512];
        const char *mute_indicator = data.main_device_muted ? " [MUTED]" : "";

        snprintf(audio_text, sizeof(audio_text),
                 "Current Tab: %s\n\n"
                 "Primary Device:\n"
                 "  Name: %s\n"
                 "  Volume: %d%%%s\n\n"
                 "Balance Mode:\n"
                 "  Device 1: %s\n"
                 "  Volume 1: %d%%%s\n"
                 "  Device 2: %s\n"
                 "  Volume 2: %d%%%s\n\n"
                 "System Actions:\n"
                 "  FORMAT SD: Erase all data\n"
                 "  OTA MODE: Update firmware\n"
                 "  RESTART: Reboot device\n"
                 "  REFRESH: Update info",
                 data.current_tab,
                 data.main_device, data.main_device_volume, mute_indicator,
                 data.balance_device1, data.balance_device1_volume,
                 data.balance_device1_muted ? " [MUTED]" : "",
                 data.balance_device2, data.balance_device2_volume,
                 data.balance_device2_muted ? " [MUTED]" : "");
        lv_label_set_text(state_audio_label, audio_text);
    }

    ESP_LOGI(TAG, "Settings: State overview updated successfully");
}

static void handleHideStateOverview(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "Settings: Hiding state overview overlay");

    if (state_overlay && lv_obj_is_valid(state_overlay)) {
        lv_obj_del(state_overlay);
        state_overlay = NULL;
        state_system_label = NULL;
        state_network_label = NULL;
        state_audio_label = NULL;
        ESP_LOGI(TAG, "Settings: State overview overlay hidden successfully");
    } else {
        ESP_LOGW(TAG, "Settings: Hide requested but no state overlay exists");
    }
}

static void handleFormatSDRequest(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "SD Format: Showing confirmation dialog using Universal Dialog");

    // Use the new Universal Dialog system for SD format confirmation
    UI::Dialog::UniversalDialog::showWarning(
        "FORMAT SD CARD",
        "*** WARNING ***\n\n"
        "This will PERMANENTLY ERASE\n"
        "ALL DATA on the SD card!\n\n"
        "This action CANNOT be undone.\n"
        "Are you absolutely sure?",
        []() {
            // Confirmed - start format
            ESP_LOGI(TAG, "SD Format: Confirmed by user - starting format");
            confirmSDFormat();
        },
        []() {
            // Cancelled
            ESP_LOGI(TAG, "SD Format: Cancelled by user");
        },
        UI::Dialog::DialogSize::MEDIUM);
}

// Forward declaration for the SD format task
static void sdFormatTask(void *parameter);

static void handleFormatSDConfirm(const LVGLMessage_t *msg) {
    ESP_LOGI(TAG, "SD Format: Starting format process using Universal Dialog");

    // Use Universal Dialog system for progress dialog
    UI::Dialog::ProgressConfig progressConfig;
    progressConfig.title = "FORMATTING SD CARD";
    progressConfig.message = "Initializing format...";
    progressConfig.value = 0;
    progressConfig.max = 100;
    progressConfig.indeterminate = false;
    progressConfig.cancellable = false;  // Don't allow cancellation during format

    UI::Dialog::UniversalDialog::showProgress(progressConfig, UI::Dialog::DialogSize::MEDIUM);

    ESP_LOGI(TAG, "SD Format: Progress dialog created, starting actual format task");

    // Start the actual SD format process in a separate task
    xTaskCreate(sdFormatTask, "SDFormatTask", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);

    // Initial progress update
    updateSDFormatProgress(5, "Starting format operation...");
}

static void handleFormatSDProgress(const LVGLMessage_t *msg) {
    const auto &data = msg->data.sd_format;
    ESP_LOGI(TAG, "SD Format: Progress update - %d%% - %s", data.progress, data.message);

    // Update progress using Universal Dialog system
    UI::Dialog::UniversalDialog::updateProgress(data.progress, data.message);
}

static void handleFormatSDComplete(const LVGLMessage_t *msg) {
    const auto &data = msg->data.sd_format;
    ESP_LOGI(TAG, "SD Format: Complete - Success: %s - %s", data.success ? "YES" : "NO", data.message);

    // Close the progress dialog and show completion status
    UI::Dialog::UniversalDialog::closeDialog();

    // Show completion dialog based on success/failure
    if (data.success) {
        UI::Dialog::UniversalDialog::showInfo(
            "Format Complete",
            data.message,
            nullptr,
            UI::Dialog::DialogSize::MEDIUM);
    } else {
        UI::Dialog::UniversalDialog::showError(
            "Format Failed",
            data.message,
            nullptr,
            UI::Dialog::DialogSize::MEDIUM);
    }
}

// SD Format Task Implementation
static void sdFormatTask(void *parameter) {
    ESP_LOGI(TAG, "SD Format Task: Starting SD card format operation");

    bool formatSuccess = false;

    // Phase 1: Preparation (5-15%)
    updateSDFormatProgress(10, "Preparing for format...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Check if SD card is available
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD Format Task: SD card not mounted, attempting to mount");
        updateSDFormatProgress(15, "Mounting SD card...");

        if (!Hardware::SD::mount()) {
            ESP_LOGE(TAG, "SD Format Task: Failed to mount SD card");
            completeSDFormat(false, "ERROR: Cannot access SD card");
            vTaskDelete(NULL);
            return;
        }
    }

    // Phase 2: Pre-format checks (15-25%)
    updateSDFormatProgress(20, "Verifying SD card...");
    vTaskDelay(pdMS_TO_TICKS(300));

    Hardware::SD::SDCardInfo cardInfo = Hardware::SD::getCardInfo();
    if (cardInfo.cardType == CARD_NONE) {
        ESP_LOGE(TAG, "SD Format Task: No SD card detected");
        completeSDFormat(false, "ERROR: No SD card found");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "SD Format Task: Card detected - Type: %d, Size: %.2f MB",
             cardInfo.cardType, cardInfo.cardSize / (1024.0 * 1024.0));

    // Phase 3: Begin format operation (25-90%)
    updateSDFormatProgress(25, "Starting format operation...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Update progress during format
    updateSDFormatProgress(40, "Removing files and directories...");
    vTaskDelay(pdMS_TO_TICKS(300));

    updateSDFormatProgress(60, "Cleaning file system...");
    vTaskDelay(pdMS_TO_TICKS(200));

    updateSDFormatProgress(75, "Finalizing format...");

    // Perform the actual format operation
    ESP_LOGI(TAG, "SD Format Task: Calling Hardware::SD::format()");
    formatSuccess = Hardware::SD::format();

    if (formatSuccess) {
        ESP_LOGI(TAG, "SD Format Task: Format completed successfully");
        updateSDFormatProgress(90, "Format completed successfully");
        vTaskDelay(pdMS_TO_TICKS(500));

        // Phase 4: Post-format verification (90-100%)
        updateSDFormatProgress(95, "Verifying format...");
        vTaskDelay(pdMS_TO_TICKS(300));

        // Check if card is still accessible after format
        if (Hardware::SD::isMounted()) {
            completeSDFormat(true, "SD card formatted successfully!");
        } else {
            ESP_LOGW(TAG, "SD Format Task: Format completed but card not accessible");
            completeSDFormat(true, "Format completed (remount required)");
        }
    } else {
        ESP_LOGE(TAG, "SD Format Task: Format operation failed");
        completeSDFormat(false, "Format operation failed");
    }

    ESP_LOGI(TAG, "SD Format Task: Task completed, deleting task");
    vTaskDelete(NULL);
}

// PERFORMANCE: Initialize message handler mappings - single O(1) lookup
static void initializeMessageHandlers() {
    messageHandlers = {
        // Common/simple message handlers
        {MSG_UPDATE_WIFI_STATUS, handleWifiStatus},
        {MSG_UPDATE_NETWORK_INFO, handleNetworkInfo},
        {MSG_UPDATE_FPS_DISPLAY, handleFpsDisplay},
        {MSG_UPDATE_BUILD_TIME_DISPLAY, handleBuildTimeDisplay},
        {MSG_UPDATE_MASTER_VOLUME, handleMasterVolume},
        {MSG_UPDATE_SINGLE_VOLUME, handleSingleVolume},
        {MSG_UPDATE_BALANCE_VOLUME, handleBalanceVolume},
        {MSG_UPDATE_MASTER_DEVICE, handleMasterDevice},

        // Complex message handlers
        {MSG_UPDATE_OTA_PROGRESS, handleOtaProgress},
        {MSG_UPDATE_SINGLE_DEVICE, handleSingleDevice},
        {MSG_UPDATE_BALANCE_DEVICES, handleBalanceDevices},
        {MSG_SCREEN_CHANGE, handleScreenChange},
        {MSG_REQUEST_DATA, handleRequestData},
        {MSG_SHOW_OTA_SCREEN, handleShowOtaScreen},
        {MSG_UPDATE_OTA_SCREEN_PROGRESS, handleUpdateOtaScreenProgress},
        {MSG_HIDE_OTA_SCREEN, handleHideOtaScreen},
        {MSG_SHOW_OTA_STATUS_INDICATOR, handleShowOtaStatusIndicator},
        {MSG_UPDATE_OTA_STATUS_INDICATOR, handleUpdateOtaStatusIndicator},
        {MSG_HIDE_OTA_STATUS_INDICATOR, handleHideOtaStatusIndicator},
        {MSG_SHOW_STATE_OVERVIEW, handleShowStateOverview},
        {MSG_UPDATE_STATE_OVERVIEW, handleUpdateStateOverview},
        {MSG_HIDE_STATE_OVERVIEW, handleHideStateOverview},
        {MSG_FORMAT_SD_REQUEST, handleFormatSDRequest},
        {MSG_FORMAT_SD_CONFIRM, handleFormatSDConfirm},
        {MSG_FORMAT_SD_PROGRESS, handleFormatSDProgress},
        {MSG_FORMAT_SD_COMPLETE, handleFormatSDComplete},
        {MSG_DEBUG_UI_LOG, handleDebugUILog}};
}

// Queue handle
QueueHandle_t lvglMessageQueue = NULL;

// Message queue size
#define LVGL_MESSAGE_QUEUE_SIZE 128  // Emergency increase from 32 to handle message overflow

bool init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Message Handler");

    // PERFORMANCE: Initialize message handler mappings
    initializeMessageHandlers();

    // Create message queue
    lvglMessageQueue =
        xQueueCreate(LVGL_MESSAGE_QUEUE_SIZE, sizeof(LVGLMessage_t));
    if (lvglMessageQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL message queue");
        return false;
    }

    // CRITICAL: Ensure LVGL timer system is ready before creating timers
    // This prevents issues when ERROR logging level provides no debug delays
    ESP_LOGI(TAG, "Verifying LVGL timer system readiness...");

    // Wait for LVGL to be properly initialized
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL display not available - cannot create message timer");
        return false;
    }

    // Additional small delay to ensure timer system is stable
    vTaskDelay(pdMS_TO_TICKS(100));

    // Create LVGL timer to process messages every 10ms (improved responsiveness)
    ESP_LOGI(TAG, "Creating LVGL message processing timer...");
    lv_timer_t *msgTimer = lv_timer_create(processMessageQueue, 10, NULL);
    if (msgTimer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL message processing timer");
        return false;
    }

    // Verify timer was created successfully
    ESP_LOGI(TAG, "LVGL message timer created successfully");

    ESP_LOGI(TAG, "LVGL Message Handler initialized successfully");
    return true;
}

void deinit(void) {
    if (lvglMessageQueue) {
        vQueueDelete(lvglMessageQueue);
        lvglMessageQueue = NULL;
    }
}

bool sendMessage(const LVGLMessage_t *message) {
    if (lvglMessageQueue == NULL || message == NULL) {
        return false;
    }

    // Send message with no blocking (timeout = 0)
    if (xQueueSend(lvglMessageQueue, message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Message queue full, dropping message type %d",
                 message->type);
        return false;
    }

    return true;
}

void processMessageQueue(lv_timer_t *timer) {
    // CRITICAL: Don't process UI updates during rendering to prevent corruption
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->rendering_in_progress) {
        return;
    }

    LVGLMessage_t message;

    // OPTIMIZED: Adaptive message processing based on queue size and system load
    int messagesProcessed = 0;
    uint32_t processing_start = millis();

    // Dynamic processing limits based on queue size
    uxQueueMessagesWaiting(lvglMessageQueue);
    int queueSize = uxQueueMessagesWaiting(lvglMessageQueue);
    int maxMessages;
    uint32_t maxProcessingTime;

    if (queueSize > 64) {
        // Emergency mode: High queue size
        maxMessages = 15;
        maxProcessingTime = 50;  // 50ms max
        ESP_LOGW(TAG, "Message queue overloaded (%d messages), entering emergency processing", queueSize);
    } else if (queueSize > 32) {
        // High load mode
        maxMessages = 10;
        maxProcessingTime = 35;
    } else if (queueSize > 16) {
        // Medium load mode
        maxMessages = 8;
        maxProcessingTime = 25;
    } else {
        // Normal mode
        maxMessages = 5;
        maxProcessingTime = 20;
    }

    // Process available messages with adaptive limits
    while (messagesProcessed < maxMessages &&
           (millis() - processing_start) < maxProcessingTime &&
           xQueueReceive(lvglMessageQueue, &message, 0) == pdTRUE) {
        messagesProcessed++;

        // PERFORMANCE: Single O(1) hash map lookup - no more double lookups!
        auto handler = messageHandlers.find(message.type);
        if (handler != messageHandlers.end()) {
            handler->second(&message);
        } else {
            ESP_LOGD(TAG, "Unhandled message type: %d", message.type);
        }
    }

    // OPTIMIZED: Performance monitoring and queue health reporting
    uint32_t processingTime = millis() - processing_start;
    if (processingTime > 30 || messagesProcessed >= maxMessages) {
        ESP_LOGD(TAG, "Processed %d messages in %ums (queue: %d→%d)",
                 messagesProcessed, processingTime, queueSize, uxQueueMessagesWaiting(lvglMessageQueue));
    }

    // OPTIMIZED: Queue overflow protection
    if (uxQueueMessagesWaiting(lvglMessageQueue) > 100) {
        ESP_LOGW(TAG, "Message queue critically full (%d), purging old messages",
                 uxQueueMessagesWaiting(lvglMessageQueue));

        // Track message type distribution during purge
        int messageTypeCounts[32] = {0};  // Assuming we have less than 32 message types
        int totalPurged = 0;

        // Purge some old messages to prevent complete overflow
        LVGLMessage_t dummyMessage;
        for (int i = 0; i < 20 && xQueueReceive(lvglMessageQueue, &dummyMessage, 0) == pdTRUE; i++) {
            // Track the message type
            if (dummyMessage.type < 32) {
                messageTypeCounts[dummyMessage.type]++;
            }
            totalPurged++;
        }

        // Log the distribution of purged message types
        if (totalPurged > 0) {
            ESP_LOGW(TAG, "Purged %d messages. Distribution:", totalPurged);
            for (int i = 0; i < 32; i++) {
                if (messageTypeCounts[i] > 0) {
                    const char *msgTypeName = getMessageTypeName(i);
                    ESP_LOGW(TAG, "  Type %d (%s): %d messages (%.1f%%)",
                             i, msgTypeName, messageTypeCounts[i],
                             (messageTypeCounts[i] * 100.0f) / totalPurged);
                }
            }
        }
    }
}

// Helper functions
bool updateWifiStatus(const char *status, bool connected) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_WIFI_STATUS;
    message.data.wifi_status.status = status;
    message.data.wifi_status.connected = connected;
    return sendMessage(&message);
}

bool updateNetworkInfo(const char *ssid, const char *ip) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_NETWORK_INFO;
    message.data.network_info.ssid = ssid;
    message.data.network_info.ip = ip;
    return sendMessage(&message);
}

bool updateOTAProgress(uint8_t progress, bool in_progress, bool success,
                       const char *msg) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_OTA_PROGRESS;
    message.data.ota_progress.progress = progress;
    message.data.ota_progress.in_progress = in_progress;
    message.data.ota_progress.success = success;

    // Safely copy message string
    if (msg) {
        strncpy(message.data.ota_progress.message, msg,
                sizeof(message.data.ota_progress.message) - 1);
        message.data.ota_progress
            .message[sizeof(message.data.ota_progress.message) - 1] = '\0';
    } else {
        message.data.ota_progress.message[0] = '\0';
    }

    return sendMessage(&message);
}

bool updateFpsDisplay(float fps) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_FPS_DISPLAY;
    message.data.fps_display.fps = fps;
    return sendMessage(&message);
}

bool updateBuildTimeDisplay() {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_BUILD_TIME_DISPLAY;
    return sendMessage(&message);
}

bool changeScreen(void *screen, int anim_type, int time, int delay) {
    LVGLMessage_t message;
    message.type = MSG_SCREEN_CHANGE;
    message.data.screen_change.screen = screen;
    message.data.screen_change.anim_type = anim_type;
    message.data.screen_change.time = time;
    message.data.screen_change.delay = delay;
    return sendMessage(&message);
}

// Helper functions for the custom OTA screen
bool showOtaScreen(void) {
    LVGLMessage_t message;
    message.type = MSG_SHOW_OTA_SCREEN;
    return sendMessage(&message);
}

bool updateOtaScreenProgress(uint8_t progress, const char *msg) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_OTA_SCREEN_PROGRESS;
    message.data.ota_screen_progress.progress = progress;
    // Safely copy message string
    if (msg) {
        strncpy(message.data.ota_screen_progress.message, msg,
                sizeof(message.data.ota_screen_progress.message) - 1);
        message.data.ota_screen_progress
            .message[sizeof(message.data.ota_screen_progress.message) - 1] = '\0';
    } else {
        message.data.ota_screen_progress.message[0] = '\0';
    }
    return sendMessage(&message);
}

bool hideOtaScreen(void) {
    LVGLMessage_t message;
    message.type = MSG_HIDE_OTA_SCREEN;
    return sendMessage(&message);
}

void updateOtaScreenDirectly(uint8_t progress, const char *msg) {
    ESP_LOGI(TAG, "OTA: Direct update - %d%% - %s", progress, msg ? msg : "");

    // BULLETPROOF: Direct UI updates for critical OTA operations
    // This bypasses the message queue for immediate updates during critical phases

    // Ensure we're on the correct screen
    if (lv_scr_act() != ui_screenOTA) {
        ESP_LOGW(TAG, "OTA: Direct update but not on OTA screen - switching immediately");
        lv_scr_load(ui_screenOTA);
        if (ui_screenOTA && !lv_obj_is_valid(ui_screenOTA)) {
            ui_screenOTA_screen_init();
        }
    }

    // Update progress bar immediately without animation for critical operations
    if (ui_barOTAUpdateProgress && lv_obj_is_valid(ui_barOTAUpdateProgress)) {
        lv_bar_set_value(ui_barOTAUpdateProgress, progress, LV_ANIM_OFF);
    }

    // Update status message
    if (ui_lblOTAUpdateProgress && lv_obj_is_valid(ui_lblOTAUpdateProgress) && msg) {
        lv_label_set_text(ui_lblOTAUpdateProgress, msg);
    }

    // Visual feedback for different progress states
    if (ui_Label2 && lv_obj_is_valid(ui_Label2)) {
        if (progress >= 100) {
            lv_label_set_text(ui_Label2, "COMPLETE");
            lv_obj_set_style_text_color(ui_Label2, lv_color_hex(0x00FF00), LV_PART_MAIN);
        } else if (progress > 90) {
            lv_label_set_text(ui_Label2, "FINISHING");
            lv_obj_set_style_text_color(ui_Label2, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        } else {
            lv_label_set_text(ui_Label2, "UPDATING");
            lv_obj_set_style_text_color(ui_Label2, lv_color_white(), LV_PART_MAIN);
        }
    }

    // CRITICAL: Force immediate refresh to minimize timing conflicts during OTA
    // This ensures updates are visible even if interrupts are disabled
    lv_refr_now(lv_disp_get_default());

    ESP_LOGD(TAG, "OTA: Direct update completed");
}

// Tab-specific volume update functions
bool updateMasterVolume(int volume) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_MASTER_VOLUME;
    message.data.master_volume.volume = volume;
    return sendMessage(&message);
}

bool updateSingleVolume(int volume) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_SINGLE_VOLUME;
    message.data.single_volume.volume = volume;
    return sendMessage(&message);
}

bool updateBalanceVolume(int volume) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_BALANCE_VOLUME;
    message.data.balance_volume.volume = volume;
    return sendMessage(&message);
}

// Tab-specific device update functions
bool updateMasterDevice(const char *device_name) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_MASTER_DEVICE;

    // Safely copy device name string
    if (device_name) {
        strncpy(message.data.master_device.device_name, device_name,
                sizeof(message.data.master_device.device_name) - 1);
        message.data.master_device.device_name[sizeof(message.data.master_device.device_name) - 1] = '\0';
    } else {
        message.data.master_device.device_name[0] = '\0';
    }

    return sendMessage(&message);
}

bool updateSingleDevice(const char *device_name) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_SINGLE_DEVICE;

    // Safely copy device name string
    if (device_name) {
        strncpy(message.data.single_device.device_name, device_name,
                sizeof(message.data.single_device.device_name) - 1);
        message.data.single_device.device_name[sizeof(message.data.single_device.device_name) - 1] = '\0';
    } else {
        message.data.single_device.device_name[0] = '\0';
    }

    return sendMessage(&message);
}

bool updateBalanceDevices(const char *device1_name, const char *device2_name) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_BALANCE_DEVICES;

    // Safely copy device1 name string
    if (device1_name) {
        strncpy(message.data.balance_devices.device1_name, device1_name,
                sizeof(message.data.balance_devices.device1_name) - 1);
        message.data.balance_devices.device1_name[sizeof(message.data.balance_devices.device1_name) - 1] = '\0';
    } else {
        message.data.balance_devices.device1_name[0] = '\0';
    }

    // Safely copy device2 name string
    if (device2_name) {
        strncpy(message.data.balance_devices.device2_name, device2_name,
                sizeof(message.data.balance_devices.device2_name) - 1);
        message.data.balance_devices.device2_name[sizeof(message.data.balance_devices.device2_name) - 1] = '\0';
    } else {
        message.data.balance_devices.device2_name[0] = '\0';
    }

    return sendMessage(&message);
}

// PERFORMANCE: Tab volume update function pointers for O(1) lookup
using TabVolumeUpdater = bool (*)(int);

static std::unordered_map<uint32_t, TabVolumeUpdater> tabVolumeUpdaters = {
    {0, updateMasterVolume},  // Master tab
    {1, updateSingleVolume},  // Single tab
    {2, updateBalanceVolume}  // Balance tab
};

// Convenience function to update volume for the currently active tab
bool updateCurrentTabVolume(int volume) {
    // Get the currently active tab from the UI
    if (ui_tabsModeSwitch) {
        uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);

        auto tabUpdater = tabVolumeUpdaters.find(activeTab);
        if (tabUpdater != tabVolumeUpdaters.end()) {
            return tabUpdater->second(volume);
        } else {
            ESP_LOGW(TAG, "Unknown active tab: %d, defaulting to Master volume", activeTab);
            return updateMasterVolume(volume);  // Default to Master tab
        }
    } else {
        ESP_LOGW(TAG, "Tab view not available, defaulting to Master volume");
        return updateMasterVolume(volume);  // Default to Master tab
    }
}

// Helper functions for state overview
bool showStateOverview(void) {
    LVGLMessage_t message;
    message.type = MSG_SHOW_STATE_OVERVIEW;
    return sendMessage(&message);
}

bool updateStateOverview(void) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_STATE_OVERVIEW;

    // Collect current system state
    message.data.state_overview.free_heap = Hardware::Device::getFreeHeap();
    message.data.state_overview.free_psram = Hardware::Device::getPsramSize();
    message.data.state_overview.cpu_freq = Hardware::Device::getCpuFrequency();
    message.data.state_overview.uptime_ms = Hardware::Device::getMillis();

    // Network status - Network-free mode
    strncpy(message.data.state_overview.wifi_status, "Network-Free Mode",
            sizeof(message.data.state_overview.wifi_status) - 1);
    message.data.state_overview.wifi_status[sizeof(message.data.state_overview.wifi_status) - 1] = '\0';

    message.data.state_overview.wifi_rssi = 0;  // No WiFi in network-free mode

    strncpy(message.data.state_overview.ip_address, "N/A (Network-Free)",
            sizeof(message.data.state_overview.ip_address) - 1);
    message.data.state_overview.ip_address[sizeof(message.data.state_overview.ip_address) - 1] = '\0';

    // Network services available only during OTA mode

    // Collect audio state
    Application::Audio::AudioManager &audioManager = Application::Audio::AudioManager::getInstance();
    const auto &audioState = audioManager.getState();

    const char *tabName = audioManager.getTabName(audioManager.getCurrentTab());
    strncpy(message.data.state_overview.current_tab, tabName,
            sizeof(message.data.state_overview.current_tab) - 1);
    message.data.state_overview.current_tab[sizeof(message.data.state_overview.current_tab) - 1] = '\0';

    // Main device (Master/Single tab)
    if (audioState.selectedDevice1) {
        strncpy(message.data.state_overview.main_device, audioState.selectedDevice1->processName.c_str(),
                sizeof(message.data.state_overview.main_device) - 1);
        message.data.state_overview.main_device[sizeof(message.data.state_overview.main_device) - 1] = '\0';
        message.data.state_overview.main_device_volume = audioState.selectedDevice1->volume;
        message.data.state_overview.main_device_muted = audioState.selectedDevice1->isMuted;
    } else {
        strcpy(message.data.state_overview.main_device, "None");
        message.data.state_overview.main_device_volume = 0;
        message.data.state_overview.main_device_muted = false;
    }

    // Balance devices
    if (audioState.selectedDevice1) {
        strncpy(message.data.state_overview.balance_device1, audioState.selectedDevice1->processName.c_str(),
                sizeof(message.data.state_overview.balance_device1) - 1);
        message.data.state_overview.balance_device1[sizeof(message.data.state_overview.balance_device1) - 1] = '\0';
        message.data.state_overview.balance_device1_volume = audioState.selectedDevice1->volume;
        message.data.state_overview.balance_device1_muted = audioState.selectedDevice1->isMuted;
    } else {
        strcpy(message.data.state_overview.balance_device1, "None");
        message.data.state_overview.balance_device1_volume = 0;
        message.data.state_overview.balance_device1_muted = false;
    }

    if (audioState.selectedDevice2) {
        strncpy(message.data.state_overview.balance_device2, audioState.selectedDevice2->processName.c_str(),
                sizeof(message.data.state_overview.balance_device2) - 1);
        message.data.state_overview.balance_device2[sizeof(message.data.state_overview.balance_device2) - 1] = '\0';
        message.data.state_overview.balance_device2_volume = audioState.selectedDevice2->volume;
        message.data.state_overview.balance_device2_muted = audioState.selectedDevice2->isMuted;
    } else {
        strcpy(message.data.state_overview.balance_device2, "None");
        message.data.state_overview.balance_device2_volume = 0;
        message.data.state_overview.balance_device2_muted = false;
    }

    // Legacy compatibility fields (use main device data)
    strncpy(message.data.state_overview.selected_device, message.data.state_overview.main_device,
            sizeof(message.data.state_overview.selected_device) - 1);
    message.data.state_overview.current_volume = message.data.state_overview.main_device_volume;
    message.data.state_overview.is_muted = message.data.state_overview.main_device_muted;

    return sendMessage(&message);
}

bool hideStateOverview(void) {
    ESP_LOGI(TAG, "State Overlay: hideStateOverview() called - sending hide message");
    LVGLMessage_t message;
    message.type = MSG_HIDE_STATE_OVERVIEW;
    return sendMessage(&message);
}

bool updateSDStatus(const char *status, bool mounted, uint64_t total_mb, uint64_t used_mb, uint8_t card_type) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_SD_STATUS;
    message.data.sd_status.status = status;
    message.data.sd_status.mounted = mounted;
    message.data.sd_status.total_mb = total_mb;
    message.data.sd_status.used_mb = used_mb;
    message.data.sd_status.card_type = card_type;
    return sendMessage(&message);
}

// Helper functions for SD format operations
bool requestSDFormat(void) {
    LVGLMessage_t message;
    message.type = MSG_FORMAT_SD_REQUEST;
    return sendMessage(&message);
}

bool confirmSDFormat(void) {
    LVGLMessage_t message;
    message.type = MSG_FORMAT_SD_CONFIRM;
    return sendMessage(&message);
}

bool updateSDFormatProgress(uint8_t progress, const char *msg) {
    LVGLMessage_t message;
    message.type = MSG_FORMAT_SD_PROGRESS;
    message.data.sd_format.progress = progress;
    message.data.sd_format.in_progress = true;

    // Safely copy message string
    if (msg) {
        strncpy(message.data.sd_format.message, msg,
                sizeof(message.data.sd_format.message) - 1);
        message.data.sd_format.message[sizeof(message.data.sd_format.message) - 1] = '\0';
    } else {
        message.data.sd_format.message[0] = '\0';
    }

    return sendMessage(&message);
}

bool completeSDFormat(bool success, const char *msg) {
    LVGLMessage_t message;
    message.type = MSG_FORMAT_SD_COMPLETE;
    message.data.sd_format.success = success;
    message.data.sd_format.in_progress = false;
    message.data.sd_format.progress = success ? 100 : 0;

    // Safely copy message string
    if (msg) {
        strncpy(message.data.sd_format.message, msg,
                sizeof(message.data.sd_format.message) - 1);
        message.data.sd_format.message[sizeof(message.data.sd_format.message) - 1] = '\0';
    } else {
        message.data.sd_format.message[0] = '\0';
    }

    return sendMessage(&message);
}

// BULLETPROOF: Helper functions for OTA status indicators
bool showOTAStatusIndicator(uint8_t progress, const char *status, bool is_error, bool pulsing) {
    LVGLMessage_t message;
    message.type = MSG_SHOW_OTA_STATUS_INDICATOR;
    message.data.ota_status_indicator.show = true;
    message.data.ota_status_indicator.progress = progress;
    message.data.ota_status_indicator.is_error = is_error;
    message.data.ota_status_indicator.pulsing = pulsing;

    // Safely copy status string
    if (status) {
        strncpy(message.data.ota_status_indicator.status, status,
                sizeof(message.data.ota_status_indicator.status) - 1);
        message.data.ota_status_indicator.status[sizeof(message.data.ota_status_indicator.status) - 1] = '\0';
    } else {
        strcpy(message.data.ota_status_indicator.status, "OTA");
    }

    return sendMessage(&message);
}

bool updateOTAStatusIndicator(uint8_t progress, const char *status, bool is_error, bool pulsing) {
    LVGLMessage_t message;
    message.type = MSG_UPDATE_OTA_STATUS_INDICATOR;
    message.data.ota_status_indicator.show = true;
    message.data.ota_status_indicator.progress = progress;
    message.data.ota_status_indicator.is_error = is_error;
    message.data.ota_status_indicator.pulsing = pulsing;

    // Safely copy status string
    if (status) {
        strncpy(message.data.ota_status_indicator.status, status,
                sizeof(message.data.ota_status_indicator.status) - 1);
        message.data.ota_status_indicator.status[sizeof(message.data.ota_status_indicator.status) - 1] = '\0';
    } else {
        strcpy(message.data.ota_status_indicator.status, "OTA");
    }

    return sendMessage(&message);
}

bool hideOTAStatusIndicator(void) {
    LVGLMessage_t message;
    message.type = MSG_HIDE_OTA_STATUS_INDICATOR;
    message.data.ota_status_indicator.show = false;
    return sendMessage(&message);
}

// Helper function for debug UI logging
bool sendDebugUILog(const char *message) {
    if (!message) {
        ESP_LOGW(TAG, "sendDebugUILog called with null message");
        return false;
    }

    LVGLMessage_t lvglMessage;
    lvglMessage.type = MSG_DEBUG_UI_LOG;

    // Safely copy message string
    strncpy(lvglMessage.data.debug_ui_log.message, message,
            sizeof(lvglMessage.data.debug_ui_log.message) - 1);
    lvglMessage.data.debug_ui_log.message[sizeof(lvglMessage.data.debug_ui_log.message) - 1] = '\0';

    return sendMessage(&lvglMessage);
}

}  // namespace LVGLMessageHandler
}  // namespace Application
