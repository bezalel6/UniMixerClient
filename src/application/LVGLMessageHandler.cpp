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
#include "AudioController.h"
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
#define LVGL_MESSAGE_QUEUE_SIZE 32

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

    // Process maximum 20 messages per cycle to reduce latency
    int messagesProcessed = 0;
    const int MAX_MESSAGES_PER_CYCLE = 20;

    // Process available messages in the queue (limited per cycle)
    while (messagesProcessed < MAX_MESSAGES_PER_CYCLE &&
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
                    // Create overlay background
                    state_overlay = lv_obj_create(lv_scr_act());
                    lv_obj_set_size(state_overlay, LV_PCT(100), LV_PCT(100));
                    lv_obj_set_style_bg_color(state_overlay, lv_color_hex(0x000000), 0);
                    lv_obj_set_style_bg_opa(state_overlay, 150, 0);  // Semi-transparent
                    lv_obj_remove_flag(state_overlay, LV_OBJ_FLAG_SCROLLABLE);

                    // Create content panel
                    state_overlay_panel = lv_obj_create(state_overlay);
                    lv_obj_set_size(state_overlay_panel, LV_PCT(80), LV_PCT(60));
                    lv_obj_center(state_overlay_panel);
                    lv_obj_set_style_bg_color(state_overlay_panel, lv_color_hex(0x1E1E1E), 0);
                    lv_obj_set_style_border_color(state_overlay_panel, lv_color_hex(0x404040), 0);
                    lv_obj_set_style_border_width(state_overlay_panel, 2, 0);

                    // Create labels for different state categories
                    state_system_label = lv_label_create(state_overlay_panel);
                    lv_obj_set_style_text_color(state_system_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(state_system_label, &lv_font_montserrat_14, 0);
                    lv_obj_align(state_system_label, LV_ALIGN_TOP_LEFT, 10, 10);

                    state_network_label = lv_label_create(state_overlay_panel);
                    lv_obj_set_style_text_color(state_network_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(state_network_label, &lv_font_montserrat_14, 0);
                    lv_obj_align(state_network_label, LV_ALIGN_TOP_LEFT, 10, 80);

                    state_audio_label = lv_label_create(state_overlay_panel);
                    lv_obj_set_style_text_color(state_audio_label, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_set_style_text_font(state_audio_label, &lv_font_montserrat_14, 0);
                    lv_obj_align(state_audio_label, LV_ALIGN_TOP_LEFT, 10, 150);

                    // Add click handler to close overlay
                    lv_obj_add_event_cb(state_overlay, [](lv_event_t *e) {
                        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                            hideStateOverview();
                        } }, LV_EVENT_CLICKED, NULL);
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
                break;

            case MSG_UPDATE_STATE_OVERVIEW:
                if (state_system_label) {
                    char system_text[256];
                    snprintf(system_text, sizeof(system_text),
                             "SYSTEM\nHeap: %lu KB\nPSRAM: %lu KB\nCPU: %lu MHz\nUptime: %lu.%02lu s",
                             message.data.state_overview.free_heap / 1024,
                             message.data.state_overview.free_psram / 1024,
                             message.data.state_overview.cpu_freq,
                             message.data.state_overview.uptime_ms / 1000,
                             (message.data.state_overview.uptime_ms % 1000) / 10);
                    lv_label_set_text(state_system_label, system_text);
                }

                if (state_network_label) {
                    char network_text[256];
                    snprintf(network_text, sizeof(network_text),
                             "NETWORK\nWiFi: %s\nRSSI: %d dBm\nIP: %s\nMQTT: %s",
                             message.data.state_overview.wifi_status,
                             message.data.state_overview.wifi_rssi,
                             message.data.state_overview.ip_address,
                             message.data.state_overview.mqtt_status);
                    lv_label_set_text(state_network_label, network_text);
                }

                if (state_audio_label) {
                    char audio_text[256];
                    snprintf(audio_text, sizeof(audio_text),
                             "AUDIO\nTab: %s\nDevice: %s\nVolume: %d%%\nMuted: %s",
                             message.data.state_overview.current_tab,
                             message.data.state_overview.selected_device,
                             message.data.state_overview.current_volume,
                             message.data.state_overview.is_muted ? "Yes" : "No");
                    lv_label_set_text(state_audio_label, audio_text);
                }
                break;

            case MSG_HIDE_STATE_OVERVIEW:
                if (state_overlay) {
                    lv_obj_del(state_overlay);
                    state_overlay = NULL;
                    state_overlay_bg = NULL;
                    state_overlay_panel = NULL;
                    state_system_label = NULL;
                    state_network_label = NULL;
                    state_audio_label = NULL;
                }
                break;

            default:
                ESP_LOGW(TAG, "Unknown message type: %d", message.type);
                break;
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
    Application::Audio::AudioController &audioController = Application::Audio::AudioController::getInstance();

    const char *tabName = audioController.getTabName(audioController.getCurrentTab());
    strncpy(message.data.state_overview.current_tab, tabName,
            sizeof(message.data.state_overview.current_tab) - 1);
    message.data.state_overview.current_tab[sizeof(message.data.state_overview.current_tab) - 1] = '\0';

    String selectedDevice = audioController.getSelectedDevice();
    strncpy(message.data.state_overview.selected_device, selectedDevice.c_str(),
            sizeof(message.data.state_overview.selected_device) - 1);
    message.data.state_overview.selected_device[sizeof(message.data.state_overview.selected_device) - 1] = '\0';

    // For volume and mute status, we need to get the current device info
    AudioStatus currentStatus = audioController.getCurrentAudioStatus();
    message.data.state_overview.current_volume = 0;
    message.data.state_overview.is_muted = false;

    // Try to find current device in the audio status
    if (!selectedDevice.isEmpty()) {
        for (const auto &level : currentStatus.audioLevels) {
            if (level.processName == selectedDevice) {
                message.data.state_overview.current_volume = level.level;
                message.data.state_overview.is_muted = level.isMuted;
                break;
            }
        }
    } else if (currentStatus.hasDefaultDevice) {
        // Use default device if no specific device selected
        message.data.state_overview.current_volume = currentStatus.defaultDevice.level;
        message.data.state_overview.is_muted = currentStatus.defaultDevice.isMuted;
    }

    return sendMessage(&message);
}

bool hideStateOverview(void) {
    LVGLMessage_t message;
    message.type = MSG_HIDE_STATE_OVERVIEW;
    return sendMessage(&message);
}

}  // namespace LVGLMessageHandler
}  // namespace Application