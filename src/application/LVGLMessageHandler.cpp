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
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/MqttManager.h"
#include "../hardware/SDManager.h"
#include "AudioManager.h"
#include "AudioUI.h"
#include "../display/DisplayManager.h"
#include <esp_log.h>
#include <ui/ui.h>
#include <functional>
#include <unordered_map>

// BULLETPROOF: External UI screen declarations
extern lv_obj_t *ui_screenMain;
extern lv_obj_t *ui_screenOTA;
extern lv_obj_t *ui_screenDebug;
extern lv_obj_t *ui_screenFileExplorer;

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
    [MSG_HIDE_OTA_STATUS_INDICATOR] = "HIDE_OTA_STATUS_INDICATOR"};

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

    // Save current screen for restoration later
    static lv_obj_t *previousScreen = nullptr;
    previousScreen = lv_scr_act();

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

// PERFORMANCE: Initialize message handler mappings - single O(1) lookup
static void initializeMessageHandlers() {
    messageHandlers = {
        // Common/simple message handlers
        {MSG_UPDATE_WIFI_STATUS, handleWifiStatus},
        {MSG_UPDATE_NETWORK_INFO, handleNetworkInfo},
        {MSG_UPDATE_FPS_DISPLAY, handleFpsDisplay},
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
        {MSG_HIDE_OTA_STATUS_INDICATOR, handleHideOtaStatusIndicator}};
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

    // Collect network state
    strncpy(message.data.state_overview.wifi_status, Hardware::Network::getWifiStatusString(),
            sizeof(message.data.state_overview.wifi_status) - 1);
    message.data.state_overview.wifi_status[sizeof(message.data.state_overview.wifi_status) - 1] = '\0';

    message.data.state_overview.wifi_rssi = Hardware::Network::getSignalStrength();

    strncpy(message.data.state_overview.ip_address, Hardware::Network::getIpAddress(),
            sizeof(message.data.state_overview.ip_address) - 1);
    message.data.state_overview.ip_address[sizeof(message.data.state_overview.ip_address) - 1] = '\0';

    strncpy(message.data.state_overview.mqtt_status, Hardware::Mqtt::getStatusString(),
            sizeof(message.data.state_overview.mqtt_status) - 1);
    message.data.state_overview.mqtt_status[sizeof(message.data.state_overview.mqtt_status) - 1] = '\0';

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

}  // namespace LVGLMessageHandler
}  // namespace Application
