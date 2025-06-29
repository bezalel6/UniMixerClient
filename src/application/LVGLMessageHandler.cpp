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

        // OPTIMIZED: Fast-path processing for common message types
        switch (message.type) {
            case MSG_UPDATE_WIFI_STATUS:
                // Update WiFi status indicators
                if (ui_lblWifiStatus) {
                    lv_label_set_text(ui_lblWifiStatus, message.data.wifi_status.status);
                }
                if (ui_objWifiIndicator) {
                    lv_color_t color = message.data.wifi_status.connected ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
                    lv_obj_set_style_bg_color(ui_objWifiIndicator, color, LV_PART_MAIN);
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

            case MSG_UPDATE_MASTER_VOLUME:
                if (ui_primaryVolumeSlider) {
                    lv_arc_set_value(ui_primaryVolumeSlider, message.data.master_volume.volume);
                    lv_obj_send_event(ui_primaryVolumeSlider, LV_EVENT_VALUE_CHANGED, NULL);
                }
                break;

            case MSG_UPDATE_SINGLE_VOLUME:
                if (ui_singleVolumeSlider) {
                    lv_arc_set_value(ui_singleVolumeSlider, message.data.single_volume.volume);
                    lv_obj_send_event(ui_singleVolumeSlider, LV_EVENT_VALUE_CHANGED, NULL);
                }
                break;

            case MSG_UPDATE_BALANCE_VOLUME:
                if (ui_balanceVolumeSlider) {
                    lv_arc_set_value(ui_balanceVolumeSlider, message.data.balance_volume.volume);
                    lv_obj_send_event(ui_balanceVolumeSlider, LV_EVENT_VALUE_CHANGED, NULL);
                }
                break;

            case MSG_UPDATE_MASTER_DEVICE:
                // Update Master tab device label
                if (ui_lblPrimaryAudioDeviceValue) {
                    lv_label_set_text(ui_lblPrimaryAudioDeviceValue,
                                      message.data.master_device.device_name);
                }
                break;

            // OPTIMIZED: Combine less critical message processing
            default:
                processComplexMessage(&message);
                break;
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
        // Purge some old messages to prevent complete overflow
        LVGLMessage_t dummyMessage;
        for (int i = 0; i < 20 && xQueueReceive(lvglMessageQueue, &dummyMessage, 0) == pdTRUE; i++) {
            // Just discard messages
        }
    }
}

// OPTIMIZED: Separate function for complex message processing to reduce main loop time
void processComplexMessage(const LVGLMessage_t *message) {
    switch (message->type) {
        case MSG_UPDATE_OTA_PROGRESS:
            // Update OTA progress
            if (message->data.ota_progress.in_progress) {
                // Switch to OTA screen if not already there
                if (lv_scr_act() != ui_screenOTA) {
                    _ui_screen_change(&ui_screenOTA, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                      ui_screenOTA_screen_init);
                }

                // Update progress bar
                if (ui_barOTAUpdateProgress) {
                    lv_bar_set_value(ui_barOTAUpdateProgress,
                                     message->data.ota_progress.progress, LV_ANIM_OFF);
                }

                // Update status label
                if (ui_lblOTAUpdateProgress) {
                    lv_label_set_text(ui_lblOTAUpdateProgress,
                                      message->data.ota_progress.message);
                }

                ESP_LOGI(TAG, "OTA Progress: %d%% - %s",
                         message->data.ota_progress.progress,
                         message->data.ota_progress.message);
            } else {
                // OTA finished - update final status
                if (ui_lblOTAUpdateProgress) {
                    lv_label_set_text(ui_lblOTAUpdateProgress,
                                      message->data.ota_progress.message);
                }
                if (ui_barOTAUpdateProgress) {
                    lv_bar_set_value(ui_barOTAUpdateProgress,
                                     message->data.ota_progress.success ? 100 : 0,
                                     LV_ANIM_OFF);
                }
            }
            break;

        case MSG_UPDATE_SINGLE_DEVICE:
            // Update Single tab device dropdown selection
            ESP_LOGI(TAG, "Single device update requested: %s",
                     message->data.single_device.device_name);
            break;

        case MSG_UPDATE_BALANCE_DEVICES:
            // Update Balance tab device dropdown selections
            ESP_LOGI(TAG, "Balance devices update requested: %s, %s",
                     message->data.balance_devices.device1_name,
                     message->data.balance_devices.device2_name);
            break;

        case MSG_SCREEN_CHANGE:
            // Change screen
            if (message->data.screen_change.screen) {
                lv_screen_load_anim_t anim = static_cast<lv_screen_load_anim_t>(
                    message->data.screen_change.anim_type);
                _ui_screen_change((lv_obj_t **)&message->data.screen_change.screen, anim,
                                  message->data.screen_change.time,
                                  message->data.screen_change.delay, NULL);
            }
            break;

        case MSG_REQUEST_DATA:
            // Handle data request - could trigger other system actions
            ESP_LOGI(TAG, "Data request triggered from UI");
            break;

            // ... (continue with other complex messages like state overlay, etc.)

        default:
            ESP_LOGD(TAG, "Unhandled message type: %d", message->type);
            break;
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
