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
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/MqttManager.h"
#include "../hardware/SDManager.h"
#include "AudioManager.h"
#include <esp_log.h>
#include <ui/ui.h>

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

static const char *TAG = "LVGLMessageHandler";

namespace Application {
namespace LVGLMessageHandler {

#define VOLUME_CASE(enum_name, field_name) \
    case MSG_UPDATE_##enum_name:           \
        return &msg->data.field_name.volume;

const int *get_volume_ptr(const LVGLMessage_t *msg) {
    switch (msg->type) {
        VOLUME_CASE(MASTER_VOLUME, master_volume)
        VOLUME_CASE(SINGLE_VOLUME, single_volume)
        VOLUME_CASE(BALANCE_VOLUME, balance_volume)
        default:
            return nullptr;
    }
}
#define PARSE_VOLUME(msg) ([&]() -> int { \
    const int *_v = get_volume_ptr(msg);  \
    return _v ? *_v : -1;                 \
}())

#define ARC_VOLUME_UPDATE_MSG(tab, slider)                           \
    case MSG_UPDATE_##tab##_VOLUME:                                  \
        if (slider) {                                                \
            lv_arc_set_value(slider, PARSE_VOLUME(&message));        \
            lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL); \
        }                                                            \
        break;

// Queue handle
QueueHandle_t lvglMessageQueue = NULL;

// Message queue size
#define LVGL_MESSAGE_QUEUE_SIZE 128  // Emergency increase from 32 to handle message overflow

bool init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Message Handler");

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
        // ESP_LOGD(TAG, "Skipping message processing - rendering in progress");
        return;
    }

    LVGLMessage_t message;

    // Emergency fix: Reduce messages per cycle to prevent mutex monopolization
    int messagesProcessed = 0;
    const int MAX_MESSAGES_PER_CYCLE = 5;  // Reduced from 20 to 5 for emergency fix
    uint32_t processing_start = millis();

    // Process available messages in the queue (limited per cycle and time)
    while (messagesProcessed < MAX_MESSAGES_PER_CYCLE &&
           (millis() - processing_start) < 30 &&  // Emergency timeout: max 30ms processing
           xQueueReceive(lvglMessageQueue, &message, 0) == pdTRUE) {
        messagesProcessed++;

        switch (message.type) {
            case MSG_UPDATE_WIFI_STATUS:
                // Update WiFi status indicators
                if (ui_lblWifiStatus) {
                    lv_label_set_text(ui_lblWifiStatus, message.data.wifi_status.status);
                }
                if (ui_objWifiIndicator) {
                    if (message.data.wifi_status.connected) {
                        lv_obj_set_style_bg_color(ui_objWifiIndicator, lv_color_hex(0x00FF00),
                                                  LV_PART_MAIN);
                    } else {
                        lv_obj_set_style_bg_color(ui_objWifiIndicator, lv_color_hex(0xFF0000),
                                                  LV_PART_MAIN);
                    }
                }
                break;

            case MSG_UPDATE_NETWORK_INFO:
                // Update network information
                if (ui_lblSSIDValue) {
                    lv_label_set_text(ui_lblSSIDValue, message.data.network_info.ssid);
                }
                if (ui_lblIPValue) {
                    lv_label_set_text(ui_lblIPValue, message.data.network_info.ip);
                }
                break;

            case MSG_UPDATE_OTA_PROGRESS:
                // Update OTA progress
                if (message.data.ota_progress.in_progress) {
                    // Switch to OTA screen if not already there
                    if (lv_scr_act() != ui_screenOTA) {
                        _ui_screen_change(&ui_screenOTA, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0,
                                          ui_screenOTA_screen_init);
                    }

                    // Update progress bar
                    if (ui_barOTAUpdateProgress) {
                        lv_bar_set_value(ui_barOTAUpdateProgress,
                                         message.data.ota_progress.progress, LV_ANIM_OFF);
                    }

                    // Update status label
                    if (ui_lblOTAUpdateProgress) {
                        lv_label_set_text(ui_lblOTAUpdateProgress,
                                          message.data.ota_progress.message);
                    }

                    ESP_LOGI(TAG, "OTA Progress: %d%% - %s",
                             message.data.ota_progress.progress,
                             message.data.ota_progress.message);
                } else {
                    // OTA finished - update final status
                    if (ui_lblOTAUpdateProgress) {
                        lv_label_set_text(ui_lblOTAUpdateProgress,
                                          message.data.ota_progress.message);
                    }
                    if (ui_barOTAUpdateProgress) {
                        lv_bar_set_value(ui_barOTAUpdateProgress,
                                         message.data.ota_progress.success ? 100 : 0,
                                         LV_ANIM_OFF);
                    }
                }
                break;

            case MSG_UPDATE_FPS_DISPLAY:
                // Update FPS display with both task and actual render FPS
                if (ui_lblFPS) {
                    char fpsText[64];
                    float actualFps = Display::getActualRenderFPS();
                    snprintf(fpsText, sizeof(fpsText), "FPS: %.1f/%.1f",
                             actualFps, message.data.fps_display.fps);
                    lv_label_set_text(ui_lblFPS, fpsText);
                }
                break;

                ARC_VOLUME_UPDATE_MSG(MASTER, ui_primaryVolumeSlider);
                ARC_VOLUME_UPDATE_MSG(SINGLE, ui_singleVolumeSlider);
                ARC_VOLUME_UPDATE_MSG(BALANCE, ui_balanceVolumeSlider);
            case MSG_UPDATE_MASTER_DEVICE:
                // Update Master tab device label
                if (ui_lblPrimaryAudioDeviceValue) {
                    lv_label_set_text(ui_lblPrimaryAudioDeviceValue,
                                      message.data.master_device.device_name);
                }
                break;

            case MSG_UPDATE_SINGLE_DEVICE:
                // Update Single tab device dropdown selection
                // This would require finding the index in the dropdown options
                // For now, just log the update (implementation depends on dropdown management)
                ESP_LOGI(TAG, "Single device update requested: %s",
                         message.data.single_device.device_name);
                break;

            case MSG_UPDATE_BALANCE_DEVICES:
                // Update Balance tab device dropdown selections
                // This would require finding the indices in the dropdown options
                ESP_LOGI(TAG, "Balance devices update requested: %s, %s",
                         message.data.balance_devices.device1_name,
                         message.data.balance_devices.device2_name);
                break;

            case MSG_SCREEN_CHANGE:
                // Change screen
                if (message.data.screen_change.screen) {
                    lv_screen_load_anim_t anim = static_cast<lv_screen_load_anim_t>(
                        message.data.screen_change.anim_type);
                    _ui_screen_change((lv_obj_t **)&message.data.screen_change.screen, anim,
                                      message.data.screen_change.time,
                                      message.data.screen_change.delay, NULL);
                }
                break;

            case MSG_REQUEST_DATA:
                // Handle data request - could trigger other system actions
                ESP_LOGI(TAG, "Data request triggered from UI");
                break;

            case MSG_SHOW_OTA_SCREEN:
                if (!custom_ota_screen) {
                    custom_ota_screen = lv_obj_create(NULL);
                    lv_obj_set_style_bg_color(custom_ota_screen, lv_color_hex(0x000000), 0);

                    custom_ota_label = lv_label_create(custom_ota_screen);
                    lv_label_set_text(custom_ota_label, "Starting OTA...");
                    lv_obj_set_style_text_color(custom_ota_label, lv_color_hex(0xFFFFFF),
                                                0);
                    lv_obj_set_style_text_font(custom_ota_label, &lv_font_montserrat_26, 0);
                    lv_obj_align(custom_ota_label, LV_ALIGN_CENTER, 0, -20);

                    custom_ota_bar = lv_bar_create(custom_ota_screen);
                    lv_obj_set_size(custom_ota_bar, 200, 20);
                    lv_obj_align(custom_ota_bar, LV_ALIGN_CENTER, 0, 20);
                    lv_bar_set_value(custom_ota_bar, 0, LV_ANIM_OFF);
                }
                lv_scr_load(custom_ota_screen);
                break;

            case MSG_UPDATE_OTA_SCREEN_PROGRESS:
                if (custom_ota_label) {
                    lv_label_set_text(custom_ota_label,
                                      message.data.ota_screen_progress.message);
                }
                if (custom_ota_bar) {
                    lv_bar_set_value(custom_ota_bar,
                                     message.data.ota_screen_progress.progress,
                                     LV_ANIM_OFF);
                }
                // For RGB displays: Force immediate refresh to minimize timing conflicts
                // during OTA operations that may disable interrupts
                // lv_refr_now(lv_disp_get_default());
                break;

            case MSG_HIDE_OTA_SCREEN:
                if (custom_ota_screen) {
                    lv_obj_del(custom_ota_screen);
                    custom_ota_screen = NULL;
                    custom_ota_label = NULL;
                    custom_ota_bar = NULL;
                }
                // Restore the main screen
                if (ui_screenMain) {
                    lv_scr_load(ui_screenMain);
                }
                break;

            case MSG_SHOW_STATE_OVERVIEW:
                if (!state_overlay) {
                    // Create full-screen overlay with proper z-index
                    state_overlay = lv_obj_create(lv_scr_act());
                    lv_obj_set_size(state_overlay, LV_PCT(100), LV_PCT(100));
                    lv_obj_set_pos(state_overlay, 0, 0);
                    lv_obj_set_style_bg_color(state_overlay, lv_color_hex(0x000000), 0);
                    lv_obj_set_style_bg_opa(state_overlay, 180, 0);  // More opaque
                    lv_obj_remove_flag(state_overlay, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_move_foreground(state_overlay);  // Ensure on top

                    // Create modern content panel
                    state_overlay_panel = lv_obj_create(state_overlay);
                    lv_obj_set_size(state_overlay_panel, LV_PCT(95), LV_PCT(80));
                    lv_obj_center(state_overlay_panel);
                    lv_obj_set_style_bg_color(state_overlay_panel, lv_color_hex(0x2A2A2A), 0);
                    lv_obj_set_style_border_color(state_overlay_panel, lv_color_hex(0x606060), 0);
                    lv_obj_set_style_border_width(state_overlay_panel, 1, 0);
                    lv_obj_set_style_radius(state_overlay_panel, 8, 0);
                    lv_obj_set_style_pad_all(state_overlay_panel, 15, 0);

                    // Create title and close button
                    lv_obj_t *title_label = lv_label_create(state_overlay_panel);
                    lv_label_set_text(title_label, "System Overview");
                    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
                    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);

                    lv_obj_t *close_btn = lv_button_create(state_overlay_panel);
                    lv_obj_set_size(close_btn, 50, 30);
                    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
                    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF4444), 0);
                    lv_obj_t *close_label = lv_label_create(close_btn);
                    lv_label_set_text(close_label, "X");
                    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);
                    lv_obj_center(close_label);

                    // Create SD Format button next to close button
                    lv_obj_t *format_btn = lv_button_create(state_overlay_panel);
                    lv_obj_set_size(format_btn, 60, 30);
                    lv_obj_align(format_btn, LV_ALIGN_TOP_RIGHT, -60, 5);
                    lv_obj_set_style_bg_color(format_btn, lv_color_hex(0xFF8800), 0);
                    lv_obj_t *format_label = lv_label_create(format_btn);
                    lv_label_set_text(format_label, "Format");
                    lv_obj_set_style_text_font(format_label, &lv_font_montserrat_12, 0);
                    lv_obj_center(format_label);

                    // Create horizontal layout for data columns
                    lv_obj_t *content = lv_obj_create(state_overlay_panel);
                    lv_obj_set_size(content, LV_PCT(100), LV_PCT(85));
                    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, -5);
                    lv_obj_set_style_bg_opa(content, 0, 0);
                    lv_obj_set_style_border_opa(content, 0, 0);
                    lv_obj_set_style_pad_all(content, 5, 0);
                    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

                    // System info column with progress bar
                    lv_obj_t *system_container = lv_obj_create(content);
                    lv_obj_set_size(system_container, LV_PCT(33), LV_PCT(100));
                    lv_obj_set_style_bg_opa(system_container, 0, 0);
                    lv_obj_set_style_border_opa(system_container, 0, 0);
                    lv_obj_set_style_pad_all(system_container, 5, 0);

                    state_system_label = lv_label_create(system_container);
                    lv_obj_set_style_text_color(state_system_label, lv_color_hex(0xE0E0E0), 0);
                    lv_obj_set_style_text_font(state_system_label, &lv_font_montserrat_14, 0);
                    lv_obj_set_width(state_system_label, LV_PCT(100));
                    lv_obj_align(state_system_label, LV_ALIGN_TOP_LEFT, 0, 0);

                    // Heap usage progress bar
                    state_heap_bar = lv_bar_create(system_container);
                    lv_obj_set_size(state_heap_bar, LV_PCT(90), 8);
                    lv_obj_align(state_heap_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
                    lv_obj_set_style_bg_color(state_heap_bar, lv_color_hex(0x404040), LV_PART_MAIN);
                    lv_obj_set_style_bg_color(state_heap_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
                    lv_bar_set_range(state_heap_bar, 0, 100);

                    // Network info column with signal bar
                    lv_obj_t *network_container = lv_obj_create(content);
                    lv_obj_set_size(network_container, LV_PCT(33), LV_PCT(100));
                    lv_obj_set_style_bg_opa(network_container, 0, 0);
                    lv_obj_set_style_border_opa(network_container, 0, 0);
                    lv_obj_set_style_pad_all(network_container, 5, 0);

                    state_network_label = lv_label_create(network_container);
                    lv_obj_set_style_text_color(state_network_label, lv_color_hex(0xE0E0E0), 0);
                    lv_obj_set_style_text_font(state_network_label, &lv_font_montserrat_14, 0);
                    lv_obj_set_width(state_network_label, LV_PCT(100));
                    lv_obj_align(state_network_label, LV_ALIGN_TOP_LEFT, 0, 0);

                    // WiFi signal strength bar
                    state_wifi_bar = lv_bar_create(network_container);
                    lv_obj_set_size(state_wifi_bar, LV_PCT(90), 8);
                    lv_obj_align(state_wifi_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
                    lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0x404040), LV_PART_MAIN);
                    lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0x0088FF), LV_PART_INDICATOR);
                    lv_bar_set_range(state_wifi_bar, 0, 100);

                    // Audio info column (scrollable for many devices)
                    lv_obj_t *audio_container = lv_obj_create(content);
                    lv_obj_set_size(audio_container, LV_PCT(33), LV_PCT(100));
                    lv_obj_set_style_bg_opa(audio_container, 0, 0);
                    lv_obj_set_style_border_opa(audio_container, 0, 0);
                    lv_obj_set_style_pad_all(audio_container, 5, 0);
                    lv_obj_set_scroll_dir(audio_container, LV_DIR_VER);

                    state_audio_label = lv_label_create(audio_container);
                    lv_obj_set_style_text_color(state_audio_label, lv_color_hex(0xE0E0E0), 0);
                    lv_obj_set_style_text_font(state_audio_label, &lv_font_montserrat_14, 0);
                    lv_obj_set_width(state_audio_label, LV_PCT(100));

                    // Add close button functionality
                    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
                        lv_event_code_t code = lv_event_get_code(e);
                        
                        if (code == LV_EVENT_CLICKED) {
                            hideStateOverview();
                        } }, LV_EVENT_ALL, NULL);

                    // Add format button functionality
                    lv_obj_add_event_cb(format_btn, [](lv_event_t *e) {
                        lv_event_code_t code = lv_event_get_code(e);
                        
                        if (code == LV_EVENT_CLICKED) {
                            // Check if SD card is available before showing format dialog
                            if (Hardware::SD::isMounted()) {
                                requestSDFormat();
                            } else {
                                ESP_LOGW(TAG, "Format failed: No SD card");
                            }
                        } }, LV_EVENT_ALL, NULL);
                }
                // Request initial state update and set up periodic updates
                updateStateOverview();

                // Create a timer to refresh state data every 2 seconds while overlay is visible
                static lv_timer_t *state_refresh_timer = NULL;
                if (!state_refresh_timer) {
                    state_refresh_timer = lv_timer_create([](lv_timer_t *timer) {
                        if (state_overlay) {
                            updateStateOverview();
                        } else {
                            // Delete timer when overlay is not visible
                            lv_timer_delete(timer);
                            timer = NULL;
                        }
                    },
                                                          2000, NULL);
                }

                ESP_LOGI(TAG, "State Overlay: CREATED and SHOWN");
                break;

            case MSG_UPDATE_STATE_OVERVIEW:
                // Update system info with visual elements and icons
                if (state_system_label) {
                    char system_text[512];
                    uint32_t heap_kb = message.data.state_overview.free_heap / 1024;
                    uint32_t psram_kb = message.data.state_overview.free_psram / 1024;
                    uint32_t uptime_sec = message.data.state_overview.uptime_ms / 1000;
                    uint32_t uptime_min = uptime_sec / 60;
                    uint32_t uptime_hr = uptime_min / 60;

                    // Get SD card status for display
                    const char *sd_icon = LV_SYMBOL_SD_CARD;
                    const char *sd_status_text = "Not Available";
                    char sd_info[64] = "";

                    // Check if SD manager is available by trying to get status
                    Hardware::SD::SDStatus sdStatus = Hardware::SD::getStatus();
                    if (sdStatus != Hardware::SD::SD_STATUS_NOT_INITIALIZED) {
                        bool sd_mounted = Hardware::SD::isMounted();
                        const char *sd_status_str = Hardware::SD::getStatusString();

                        if (sd_mounted) {
                            Hardware::SD::SDCardInfo cardInfo = Hardware::SD::getCardInfo();
                            uint64_t total_mb = cardInfo.totalBytes / (1024 * 1024);
                            uint64_t used_mb = cardInfo.usedBytes / (1024 * 1024);
                            snprintf(sd_info, sizeof(sd_info), " (%llu/%llu MB)", used_mb, total_mb);
                            sd_status_text = sd_status_str;
                        } else {
                            sd_status_text = sd_status_str;
                        }
                    }

                    snprintf(system_text, sizeof(system_text),
                             LV_SYMBOL_SETTINGS " SYSTEM STATUS\n" LV_SYMBOL_FILE " Heap: %u KB\n" LV_SYMBOL_DRIVE " PSRAM: %u KB\n" LV_SYMBOL_CHARGE " CPU: %u MHz\n" LV_SYMBOL_LOOP " Uptime: %u:%u:%u\n" LV_SYMBOL_WIFI " WiFi: %s\n" LV_SYMBOL_SETTINGS " Signal: %d dBm\n" LV_SYMBOL_HOME
                                                " IP: %s\n"
                                                "%s SD: %s%s",
                             heap_kb, psram_kb, message.data.state_overview.cpu_freq,
                             uptime_hr, uptime_min % 60, uptime_sec % 60,
                             message.data.state_overview.wifi_status,
                             message.data.state_overview.wifi_rssi,
                             message.data.state_overview.ip_address,
                             sd_icon, sd_status_text, sd_info);
                    lv_label_set_text(state_system_label, system_text);

                    // Update heap progress bar (percentage of available heap)
                    if (state_heap_bar) {
                        int heap_percent = (heap_kb > 500) ? 100 : (heap_kb * 100 / 500);  // Assume 500KB is "full"
                        lv_bar_set_value(state_heap_bar, heap_percent, LV_ANIM_OFF);

                        // Change color based on heap level
                        if (heap_percent > 60) {
                            lv_obj_set_style_bg_color(state_heap_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
                        } else if (heap_percent > 30) {
                            lv_obj_set_style_bg_color(state_heap_bar, lv_color_hex(0xFFAA00), LV_PART_INDICATOR);
                        } else {
                            lv_obj_set_style_bg_color(state_heap_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
                        }
                    }
                }

                // Update network info with status indicators and icons
                if (state_network_label) {
                    char network_text[512];
                    const char *wifi_icon = strstr(message.data.state_overview.wifi_status, "Connected") ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING;
                    const char *wifi_quality = "";
                    if (message.data.state_overview.wifi_rssi > -50)
                        wifi_quality = "Excellent";
                    else if (message.data.state_overview.wifi_rssi > -60)
                        wifi_quality = "Good";
                    else if (message.data.state_overview.wifi_rssi > -70)
                        wifi_quality = "Fair";
                    else
                        wifi_quality = "Poor";

                    const char *mqtt_icon = strstr(message.data.state_overview.mqtt_status, "Connected") ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE;

                    snprintf(network_text, sizeof(network_text),
                             LV_SYMBOL_WIFI
                             " NETWORK STATUS\n"
                             "%s WiFi: %s\n" LV_SYMBOL_CALL " Signal: %s (%d dBm)\n" LV_SYMBOL_GPS
                             " IP: %s\n"
                             "%s MQTT: %s",
                             wifi_icon, message.data.state_overview.wifi_status,
                             wifi_quality, message.data.state_overview.wifi_rssi,
                             message.data.state_overview.ip_address,
                             mqtt_icon, message.data.state_overview.mqtt_status);
                    lv_label_set_text(state_network_label, network_text);

                    // Update WiFi signal strength bar
                    if (state_wifi_bar) {
                        // Convert RSSI to percentage (typical range -30 to -90 dBm)
                        int rssi = message.data.state_overview.wifi_rssi;
                        int signal_percent = 0;
                        if (rssi >= -30)
                            signal_percent = 100;
                        else if (rssi >= -90)
                            signal_percent = (rssi + 90) * 100 / 60;
                        else
                            signal_percent = 0;

                        lv_bar_set_value(state_wifi_bar, signal_percent, LV_ANIM_OFF);

                        // Change color based on signal strength
                        if (signal_percent > 75) {
                            lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
                        } else if (signal_percent > 50) {
                            lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0xAAFF00), LV_PART_INDICATOR);
                        } else if (signal_percent > 25) {
                            lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0xFFAA00), LV_PART_INDICATOR);
                        } else {
                            lv_obj_set_style_bg_color(state_wifi_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
                        }
                    }
                }

                // Enhanced audio info with icons and full device status
                if (state_audio_label) {
                    // Get full audio status for comprehensive display
                    Application::Audio::AudioManager &audioManager = Application::Audio::AudioManager::getInstance();
                    const auto &audioState = audioManager.getState();

                    char audio_text[1024];
                    char *pos = audio_text;
                    int remaining = sizeof(audio_text);

                    // Header with current tab info and selected devices
                    int written = snprintf(pos, remaining,
                                           LV_SYMBOL_AUDIO " AUDIO STATUS\n" LV_SYMBOL_LIST " Active Tab: %s\n\n" LV_SYMBOL_SETTINGS " SELECTED DEVICES:\n",
                                           message.data.state_overview.current_tab);
                    pos += written;
                    remaining -= written;

                    // Show Main device (Master/Single tabs)
                    if (strlen(message.data.state_overview.main_device) > 0 && remaining > 50) {
                        const char *main_icon = message.data.state_overview.main_device_muted ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO;
                        written = snprintf(pos, remaining,
                                           "%s Main: %s %d%%\n",
                                           main_icon,
                                           message.data.state_overview.main_device,
                                           message.data.state_overview.main_device_volume);
                        pos += written;
                        remaining -= written;
                    }

                    // Show Balance devices
                    if (strlen(message.data.state_overview.balance_device1) > 0 && remaining > 50) {
                        const char *bal1_icon = message.data.state_overview.balance_device1_muted ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO;
                        written = snprintf(pos, remaining,
                                           "%s Bal1: %s %d%%\n",
                                           bal1_icon,
                                           message.data.state_overview.balance_device1,
                                           message.data.state_overview.balance_device1_volume);
                        pos += written;
                        remaining -= written;
                    }

                    if (strlen(message.data.state_overview.balance_device2) > 0 && remaining > 50) {
                        const char *bal2_icon = message.data.state_overview.balance_device2_muted ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO;
                        written = snprintf(pos, remaining,
                                           "%s Bal2: %s %d%%\n",
                                           bal2_icon,
                                           message.data.state_overview.balance_device2,
                                           message.data.state_overview.balance_device2_volume);
                        pos += written;
                        remaining -= written;
                    }

                    // Add separator for all devices section
                    if (remaining > 20) {
                        written = snprintf(pos, remaining, "\n" LV_SYMBOL_HOME " ALL DEVICES:\n");
                        pos += written;
                        remaining -= written;
                    }

                    // Show default device if available
                    if (audioState.currentStatus.hasDefaultDevice && remaining > 50) {
                        const char *default_icon = audioState.currentStatus.defaultDevice.isMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_AUDIO;
                        written = snprintf(pos, remaining,
                                           "%s Default: %s %.0f%%\n",
                                           default_icon,
                                           audioState.currentStatus.defaultDevice.friendlyName.c_str(),
                                           (float)audioState.currentStatus.defaultDevice.volume);
                        pos += written;
                        remaining -= written;
                    }

                    // Show individual audio devices (limit to fit)
                    int deviceCount = 0;
                    for (const auto &pair : audioState.currentStatus) {
                        const auto &device = pair.second;
                        if (remaining < 30 || deviceCount >= 5) break;  // Limit devices shown

                        snprintf(pos, remaining,
                                 "%s: %s %.0f%%\n",
                                 device.friendlyName.isEmpty() ? device.processName.c_str() : device.friendlyName.c_str(),
                                 device.isMuted ? "MUTED" : "Active",
                                 (float)device.volume);

                        int written = strlen(pos);
                        pos += written;
                        remaining -= written;
                        deviceCount++;
                    }

                    // Add summary if there are more devices
                    if (audioState.currentStatus.getDeviceCount() > deviceCount && remaining > 20) {
                        snprintf(pos, remaining,
                                 LV_SYMBOL_PLUS " ... +%d more devices",
                                 (int)(audioState.currentStatus.getDeviceCount() - deviceCount));
                    }

                    lv_label_set_text(state_audio_label, audio_text);
                }
                break;

            case MSG_HIDE_STATE_OVERVIEW:
                ESP_LOGI(TAG, "State Overlay: MSG_HIDE_STATE_OVERVIEW received");
                if (state_overlay) {
                    lv_obj_del(state_overlay);
                    state_overlay = NULL;
                    state_overlay_bg = NULL;
                    state_overlay_panel = NULL;
                    state_system_label = NULL;
                    state_network_label = NULL;
                    state_audio_label = NULL;
                    state_heap_bar = NULL;
                    state_wifi_bar = NULL;
                }

                // Also cleanup format dialog if it's open
                if (format_dialog) {
                    lv_obj_del(format_dialog);
                    format_dialog = NULL;
                    format_dialog_panel = NULL;
                    format_progress_bar = NULL;
                    format_status_label = NULL;
                }

                ESP_LOGI(TAG, "State Overlay: HIDDEN successfully");
                break;

            case MSG_UPDATE_SD_STATUS:
                // Update SD card status indicators
                if (ui_lblSDStatus) {
                    lv_label_set_text(ui_lblSDStatus, message.data.sd_status.status);
                }
                if (ui_objSDIndicator) {
                    if (message.data.sd_status.mounted) {
                        lv_obj_set_style_bg_color(ui_objSDIndicator, lv_color_hex(0x00FF00),
                                                  LV_PART_MAIN);
                    } else {
                        lv_obj_set_style_bg_color(ui_objSDIndicator, lv_color_hex(0xFF0000),
                                                  LV_PART_MAIN);
                    }
                }
                break;

            case MSG_FORMAT_SD_REQUEST:
                // Show format confirmation dialog
                if (!format_dialog) {
                    // Create format confirmation dialog
                    format_dialog = lv_obj_create(lv_scr_act());
                    lv_obj_set_size(format_dialog, LV_PCT(100), LV_PCT(100));
                    lv_obj_set_pos(format_dialog, 0, 0);
                    lv_obj_set_style_bg_color(format_dialog, lv_color_hex(0x000000), 0);
                    lv_obj_set_style_bg_opa(format_dialog, 200, 0);
                    lv_obj_remove_flag(format_dialog, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_move_foreground(format_dialog);

                    // Create dialog panel
                    format_dialog_panel = lv_obj_create(format_dialog);
                    lv_obj_set_size(format_dialog_panel, 400, 250);
                    lv_obj_center(format_dialog_panel);
                    lv_obj_set_style_bg_color(format_dialog_panel, lv_color_hex(0x2A2A2A), 0);
                    lv_obj_set_style_border_color(format_dialog_panel, lv_color_hex(0xFF4444), 0);
                    lv_obj_set_style_border_width(format_dialog_panel, 2, 0);
                    lv_obj_set_style_radius(format_dialog_panel, 8, 0);
                    lv_obj_set_style_pad_all(format_dialog_panel, 20, 0);

                    // Create warning title
                    lv_obj_t *title = lv_label_create(format_dialog_panel);
                    lv_label_set_text(title, LV_SYMBOL_WARNING " Format SD Card");
                    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
                    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
                    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

                    // Create warning message
                    lv_obj_t *warning = lv_label_create(format_dialog_panel);
                    lv_label_set_text(warning, "WARNING!\n\nThis will permanently delete ALL\ndata on the SD card.\n\nThis action cannot be undone!");
                    lv_obj_set_style_text_color(warning, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(warning, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_align(warning, LV_ALIGN_CENTER, 0, -10);

                    // Create button container
                    lv_obj_t *btn_container = lv_obj_create(format_dialog_panel);
                    lv_obj_set_size(btn_container, LV_PCT(100), 40);
                    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
                    lv_obj_set_style_bg_opa(btn_container, 0, 0);
                    lv_obj_set_style_border_opa(btn_container, 0, 0);
                    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                    // Create Cancel button
                    lv_obj_t *cancel_btn = lv_button_create(btn_container);
                    lv_obj_set_size(cancel_btn, 100, 35);
                    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x666666), 0);
                    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
                    lv_label_set_text(cancel_label, "Cancel");
                    lv_obj_center(cancel_label);

                    // Create Format button
                    lv_obj_t *confirm_btn = lv_button_create(btn_container);
                    lv_obj_set_size(confirm_btn, 100, 35);
                    lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(0xFF4444), 0);
                    lv_obj_t *confirm_label = lv_label_create(confirm_btn);
                    lv_label_set_text(confirm_label, "FORMAT");
                    lv_obj_center(confirm_label);

                    // Add button event handlers
                    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
                        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                            // Close format dialog
                            if (format_dialog) {
                                lv_obj_del(format_dialog);
                                format_dialog = NULL;
                                format_dialog_panel = NULL;
                            }
                        } }, LV_EVENT_CLICKED, NULL);

                    lv_obj_add_event_cb(confirm_btn, [](lv_event_t *e) {
                        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                            confirmSDFormat();
                        } }, LV_EVENT_CLICKED, NULL);
                }
                break;

            case MSG_FORMAT_SD_CONFIRM:
                // Start the actual format process
                if (format_dialog_panel) {
                    // Clear the dialog and show progress
                    lv_obj_clean(format_dialog_panel);

                    // Create progress title
                    lv_obj_t *title = lv_label_create(format_dialog_panel);
                    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Formatting SD Card...");
                    lv_obj_set_style_text_color(title, lv_color_hex(0xFF8800), 0);
                    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
                    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

                    // Create progress bar
                    format_progress_bar = lv_bar_create(format_dialog_panel);
                    lv_obj_set_size(format_progress_bar, 300, 20);
                    lv_obj_align(format_progress_bar, LV_ALIGN_CENTER, 0, 0);
                    lv_bar_set_range(format_progress_bar, 0, 100);
                    lv_bar_set_value(format_progress_bar, 0, LV_ANIM_OFF);

                    // Create status label
                    format_status_label = lv_label_create(format_dialog_panel);
                    lv_label_set_text(format_status_label, "Preparing to format...");
                    lv_obj_set_style_text_color(format_status_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(format_status_label, &lv_font_montserrat_12, 0);
                    lv_obj_align(format_status_label, LV_ALIGN_BOTTOM_MID, 0, -20);

                    // Start format in a separate task to avoid blocking UI
                    xTaskCreate([](void *param) {
                        updateSDFormatProgress(10, "Starting format...");
                        vTaskDelay(pdMS_TO_TICKS(500));

                        updateSDFormatProgress(30, "Unmounting SD card...");
                        vTaskDelay(pdMS_TO_TICKS(500));

                        updateSDFormatProgress(50, "Formatting...");
                        bool success = Hardware::SD::format();

                        if (success) {
                            updateSDFormatProgress(80, "Remounting card...");
                            vTaskDelay(pdMS_TO_TICKS(500));
                            updateSDFormatProgress(100, "Format complete!");
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            completeSDFormat(true, "SD card formatted successfully!");
                        } else {
                            completeSDFormat(false, "Format failed!");
                        }

                        vTaskDelete(NULL);
                    },
                                "SDFormat", 4096, NULL, 1, NULL);
                }
                break;

            case MSG_FORMAT_SD_PROGRESS:
                // Update format progress
                if (format_progress_bar) {
                    lv_bar_set_value(format_progress_bar, message.data.sd_format.progress, LV_ANIM_ON);
                }
                if (format_status_label) {
                    lv_label_set_text(format_status_label, message.data.sd_format.message);
                }
                break;

            case MSG_FORMAT_SD_COMPLETE:
                // Format operation completed
                if (format_dialog) {
                    // Show completion message for 2 seconds then close
                    if (format_status_label) {
                        lv_label_set_text(format_status_label, message.data.sd_format.message);
                    }

                    // Set bar to full
                    if (format_progress_bar) {
                        lv_bar_set_value(format_progress_bar, 100, LV_ANIM_ON);
                    }

                    // Change bar color based on success
                    if (format_progress_bar) {
                        if (message.data.sd_format.success) {
                            lv_obj_set_style_bg_color(format_progress_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
                        } else {
                            lv_obj_set_style_bg_color(format_progress_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
                        }
                    }

                    // Close dialog after delay
                    lv_timer_create([](lv_timer_t *timer) {
                        if (format_dialog) {
                            lv_obj_del(format_dialog);
                            format_dialog = NULL;
                            format_dialog_panel = NULL;
                            format_progress_bar = NULL;
                            format_status_label = NULL;
                        }
                        lv_timer_delete(timer);
                    },
                                    2000, NULL);
                }
                break;

            default:
                ESP_LOGW(TAG, "Unknown message type: %d", message.type);
                break;
        }
    }

    // Emergency monitoring: Log if we hit limits
    uint32_t processing_duration = millis() - processing_start;
    if (messagesProcessed >= MAX_MESSAGES_PER_CYCLE) {
        ESP_LOGW(TAG, "[EMERGENCY] Hit message limit: processed %d messages in %ums", messagesProcessed, processing_duration);
    }
    if (processing_duration >= 25) {
        ESP_LOGW(TAG, "[EMERGENCY] Long processing: %d messages took %ums", messagesProcessed, processing_duration);
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
    // if (custom_ota_label) {
    //   lv_label_set_text(custom_ota_label, msg);
    // }
    // if (custom_ota_bar) {
    //   lv_bar_set_value(custom_ota_bar, progress, LV_ANIM_OFF);
    // }
    // // For RGB displays: Force immediate refresh to minimize timing conflicts
    // // during OTA operations that may disable interrupts
    // lv_refr_now(lv_disp_get_default());
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

// Convenience function to update volume for the currently active tab
bool updateCurrentTabVolume(int volume) {
    // Get the currently active tab from the UI
    if (ui_tabsModeSwitch) {
        uint32_t activeTab = lv_tabview_get_tab_active(ui_tabsModeSwitch);

        switch (activeTab) {
            case 0:  // Master tab
                return updateMasterVolume(volume);
            case 1:  // Single tab
                return updateSingleVolume(volume);
            case 2:  // Balance tab
                return updateBalanceVolume(volume);
            default:
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

}  // namespace LVGLMessageHandler
}  // namespace Application
