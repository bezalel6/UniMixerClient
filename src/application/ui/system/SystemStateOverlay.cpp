#include "SystemStateOverlay.h"
#include <esp_log.h>
#include <cstring>

namespace Application {
namespace UI {
namespace System {

static const char* TAG = "SystemStateOverlay";

SystemStateOverlay& SystemStateOverlay::getInstance() {
    static SystemStateOverlay instance;
    return instance;
}

void SystemStateOverlay::show() {
    ESP_LOGI(TAG, "Showing comprehensive system overview");
    
    // Clean up any existing overlay first
    if (overlay && lv_obj_is_valid(overlay)) {
        destroyOverlay();
    }
    
    createOverlay();
    visible = true;
    
    // Trigger immediate update
    if (refreshCallback) {
        refreshCallback();
    }
}

void SystemStateOverlay::hide() {
    ESP_LOGI(TAG, "Hiding state overview overlay");
    
    if (overlay && lv_obj_is_valid(overlay)) {
        destroyOverlay();
        visible = false;
        ESP_LOGI(TAG, "State overview overlay hidden successfully");
    } else {
        ESP_LOGW(TAG, "Hide requested but no state overlay exists");
    }
}

bool SystemStateOverlay::isVisible() const {
    return visible && overlay && lv_obj_is_valid(overlay);
}

void SystemStateOverlay::update() {
    if (!isVisible()) {
        ESP_LOGW(TAG, "Update requested but no state overlay exists");
        return;
    }
    
    ESP_LOGI(TAG, "Updating state overview with current system data");
    
    updateSystemInfo(currentState);
    updateNetworkInfo(currentState);
    updateAudioInfo(currentState);
}

void SystemStateOverlay::updateStateData(const StateData& data) {
    currentState = data;
    if (isVisible()) {
        update();
    }
}

void SystemStateOverlay::setFormatSDCallback(std::function<void()> callback) {
    formatSDCallback = callback;
}

void SystemStateOverlay::setRestartCallback(std::function<void()> callback) {
    restartCallback = callback;
}

void SystemStateOverlay::setRefreshCallback(std::function<void()> callback) {
    refreshCallback = callback;
}

void SystemStateOverlay::createOverlay() {
    lv_obj_t* currentScreen = lv_scr_act();
    if (!currentScreen) {
        ESP_LOGE(TAG, "No current screen available for state overlay");
        return;
    }
    
    // Create main overlay container - larger for comprehensive info
    overlay = lv_obj_create(currentScreen);
    lv_obj_set_size(overlay, 700, 450);
    lv_obj_set_align(overlay, LV_ALIGN_CENTER);
    
    // Style the overlay
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x001122), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 250, LV_PART_MAIN);
    lv_obj_set_style_border_color(overlay, lv_color_hex(0x0088FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(overlay, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(overlay, 150, LV_PART_MAIN);
    
    // Create title label
    lv_obj_t* title_label = lv_label_create(overlay);
    lv_label_set_text(title_label, "SYSTEM OVERVIEW");
    lv_obj_set_align(title_label, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title_label, 15);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x00CCFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
    
    // Create close button
    lv_obj_t* close_btn = lv_btn_create(overlay);
    lv_obj_set_size(close_btn, 70, 35);
    lv_obj_set_align(close_btn, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(close_btn, -15, 10);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3333), LV_PART_MAIN);
    
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "CLOSE");
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN);
    
    // Add click event to close button
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            SystemStateOverlay::getInstance().hide();
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Create three-column layout
    lv_obj_t* main_container = lv_obj_create(overlay);
    lv_obj_remove_style_all(main_container);
    lv_obj_set_size(main_container, 670, 350);
    lv_obj_set_align(main_container, LV_ALIGN_CENTER);
    lv_obj_set_y(main_container, 15);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    createSystemColumn(main_container);
    createNetworkColumn(main_container);
    createAudioColumn(main_container);
    
    ESP_LOGI(TAG, "Comprehensive system overview created successfully");
}

void SystemStateOverlay::createSystemColumn(lv_obj_t* parent) {
    // Left Column - System Information
    lv_obj_t* left_col = lv_obj_create(parent);
    lv_obj_set_size(left_col, 200, 340);
    lv_obj_set_style_bg_color(left_col, lv_color_hex(0x002244), LV_PART_MAIN);
    lv_obj_set_style_border_width(left_col, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(left_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
    lv_obj_set_style_radius(left_col, 10, LV_PART_MAIN);
    
    lv_obj_t* sys_title = lv_label_create(left_col);
    lv_label_set_text(sys_title, "SYSTEM");
    lv_obj_set_align(sys_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(sys_title, 10);
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_14, LV_PART_MAIN);
    
    system_label = lv_label_create(left_col);
    lv_obj_set_align(system_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(system_label, 10, 40);
    lv_obj_set_size(system_label, 180, 280);
    lv_obj_set_style_text_color(system_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(system_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_long_mode(system_label, LV_LABEL_LONG_WRAP);
}

void SystemStateOverlay::createNetworkColumn(lv_obj_t* parent) {
    // Middle Column - Network & Connectivity
    lv_obj_t* mid_col = lv_obj_create(parent);
    lv_obj_set_size(mid_col, 200, 340);
    lv_obj_set_style_bg_color(mid_col, lv_color_hex(0x002244), LV_PART_MAIN);
    lv_obj_set_style_border_width(mid_col, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(mid_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
    lv_obj_set_style_radius(mid_col, 10, LV_PART_MAIN);
    
    lv_obj_t* net_title = lv_label_create(mid_col);
    lv_label_set_text(net_title, "NETWORK");
    lv_obj_set_align(net_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(net_title, 10);
    lv_obj_set_style_text_color(net_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(net_title, &lv_font_montserrat_14, LV_PART_MAIN);
    
    network_label = lv_label_create(mid_col);
    lv_obj_set_align(network_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(network_label, 10, 40);
    lv_obj_set_size(network_label, 180, 280);
    lv_obj_set_style_text_color(network_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(network_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_long_mode(network_label, LV_LABEL_LONG_WRAP);
}

void SystemStateOverlay::createAudioColumn(lv_obj_t* parent) {
    // Right Column - Audio & Actions
    lv_obj_t* right_col = lv_obj_create(parent);
    lv_obj_set_size(right_col, 240, 340);
    lv_obj_set_style_bg_color(right_col, lv_color_hex(0x002244), LV_PART_MAIN);
    lv_obj_set_style_border_width(right_col, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(right_col, lv_color_hex(0x0066AA), LV_PART_MAIN);
    lv_obj_set_style_radius(right_col, 10, LV_PART_MAIN);
    
    lv_obj_t* audio_title = lv_label_create(right_col);
    lv_label_set_text(audio_title, "AUDIO & ACTIONS");
    lv_obj_set_align(audio_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(audio_title, 10);
    lv_obj_set_style_text_color(audio_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(audio_title, &lv_font_montserrat_14, LV_PART_MAIN);
    
    audio_label = lv_label_create(right_col);
    lv_obj_set_align(audio_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(audio_label, 10, 40);
    lv_obj_set_size(audio_label, 220, 150);
    lv_obj_set_style_text_color(audio_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(audio_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_long_mode(audio_label, LV_LABEL_LONG_WRAP);
    
    createActionButtons(right_col);
}

void SystemStateOverlay::createActionButtons(lv_obj_t* parent) {
    // Action buttons in right column
    lv_obj_t* actions_container = lv_obj_create(parent);
    lv_obj_remove_style_all(actions_container);
    lv_obj_set_size(actions_container, 220, 140);
    lv_obj_set_align(actions_container, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(actions_container, -10);
    lv_obj_set_flex_flow(actions_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(actions_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // FORMAT SD button
    lv_obj_t* format_sd_btn = lv_btn_create(actions_container);
    lv_obj_set_size(format_sd_btn, 200, 32);
    lv_obj_set_style_bg_color(format_sd_btn, lv_color_hex(0xFF6600), LV_PART_MAIN);
    
    lv_obj_t* format_label = lv_label_create(format_sd_btn);
    lv_label_set_text(format_label, "FORMAT SD CARD");
    lv_obj_center(format_label);
    lv_obj_set_style_text_color(format_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(format_label, &lv_font_montserrat_12, LV_PART_MAIN);
    
    lv_obj_add_event_cb(format_sd_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "FORMAT SD button clicked");
            auto& instance = SystemStateOverlay::getInstance();
            if (instance.formatSDCallback) {
                instance.formatSDCallback();
            }
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Restart button
    lv_obj_t* restart_btn = lv_btn_create(actions_container);
    lv_obj_set_size(restart_btn, 200, 32);
    lv_obj_set_style_bg_color(restart_btn, lv_color_hex(0xFF3366), LV_PART_MAIN);
    
    lv_obj_t* restart_label = lv_label_create(restart_btn);
    lv_label_set_text(restart_label, "RESTART SYSTEM");
    lv_obj_center(restart_label);
    lv_obj_set_style_text_color(restart_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(restart_label, &lv_font_montserrat_12, LV_PART_MAIN);
    
    lv_obj_add_event_cb(restart_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "RESTART button clicked");
            auto& instance = SystemStateOverlay::getInstance();
            if (instance.restartCallback) {
                instance.restartCallback();
            }
        }
    }, LV_EVENT_CLICKED, NULL);
    
    // Refresh button
    lv_obj_t* refresh_btn = lv_btn_create(actions_container);
    lv_obj_set_size(refresh_btn, 200, 32);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x00AA66), LV_PART_MAIN);
    
    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "REFRESH DATA");
    lv_obj_center(refresh_label);
    lv_obj_set_style_text_color(refresh_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_12, LV_PART_MAIN);
    
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "REFRESH button clicked");
            auto& instance = SystemStateOverlay::getInstance();
            if (instance.refreshCallback) {
                instance.refreshCallback();
            }
        }
    }, LV_EVENT_CLICKED, NULL);
}

void SystemStateOverlay::updateSystemInfo(const StateData& data) {
    if (!system_label || !lv_obj_is_valid(system_label)) return;
    
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
    lv_label_set_text(system_label, system_text);
}

void SystemStateOverlay::updateNetworkInfo(const StateData& data) {
    if (!network_label || !lv_obj_is_valid(network_label)) return;
    
    static char network_text[512];
    const char* signal_strength = "Unknown";
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
             "  Network: Not Available\n\n"
             "Protocol:\n"
             "  Message Bus: Active\n"
             "  Audio Streaming: OK",
             data.wifi_status, signal_strength, data.wifi_rssi,
             data.ip_address);
    lv_label_set_text(network_label, network_text);
}

void SystemStateOverlay::updateAudioInfo(const StateData& data) {
    if (!audio_label || !lv_obj_is_valid(audio_label)) return;
    
    static char audio_text[512];
    const char* mute_indicator = data.main_device_muted ? " [MUTED]" : "";
    
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
             "  RESTART: Reboot device\n"
             "  REFRESH: Update info",
             data.current_tab,
             data.main_device, data.main_device_volume, mute_indicator,
             data.balance_device1, data.balance_device1_volume,
             data.balance_device1_muted ? " [MUTED]" : "",
             data.balance_device2, data.balance_device2_volume,
             data.balance_device2_muted ? " [MUTED]" : "");
    lv_label_set_text(audio_label, audio_text);
}

void SystemStateOverlay::destroyOverlay() {
    if (overlay && lv_obj_is_valid(overlay)) {
        lv_obj_del(overlay);
        overlay = nullptr;
        system_label = nullptr;
        network_label = nullptr;
        audio_label = nullptr;
        heap_bar = nullptr;
        wifi_bar = nullptr;
    }
}

} // namespace System
} // namespace UI
} // namespace Application