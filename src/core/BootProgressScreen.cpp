#include "BootProgressScreen.h"
#include "BuildInfo.h"
#include <lvgl.h>
#include <esp_log.h>

static const char* TAG = "BootProgress";
static lv_obj_t* bootScreen = nullptr;
static lv_obj_t* statusLabel = nullptr;
static lv_obj_t* progressBar = nullptr;
static bool screenVisible = false;

namespace BootProgress {

bool init() {
    ESP_LOGI(TAG, "Initializing boot progress screen");

    // Check if LVGL is initialized
    if (!lv_is_initialized()) {
        ESP_LOGE(TAG, "LVGL not initialized - cannot show boot screen");
        return false;
    }

    // Create boot screen
    bootScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bootScreen, lv_color_hex(0x1a1a1a), 0);  // Dark background

    // Create container for centered content
    lv_obj_t* container = lv_obj_create(bootScreen);
    lv_obj_set_size(container, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_center(container);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Logo/Title
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "UniMixer Client");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_pad_bottom(title, 30, 0);

    // Progress bar
    progressBar = lv_bar_create(container);
    lv_obj_set_size(progressBar, LV_PCT(100), 8);
    lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x0078D7), LV_PART_INDICATOR);
    lv_bar_set_range(progressBar, 0, 100);
    lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_pad_bottom(progressBar, 20, 0);

    // Status label
    statusLabel = lv_label_create(container);
    lv_label_set_text(statusLabel, "Starting...");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_bottom(statusLabel, 20, 0);

    // Build info
    lv_obj_t* buildInfo = lv_label_create(container);
    lv_label_set_text(buildInfo, getBuildInfo());
    lv_obj_set_style_text_color(buildInfo, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(buildInfo, &lv_font_montserrat_12, 0);

    // Load and display the screen
    lv_scr_load(bootScreen);
    screenVisible = true;

    // Force immediate render
    lv_timer_handler();

    ESP_LOGI(TAG, "Boot progress screen initialized");
    return true;
}

void updateStatus(const char* status) {
    if (!screenVisible || !statusLabel) return;

    ESP_LOGI(TAG, "Boot status: %s", status);
    lv_label_set_text(statusLabel, status);

    // Force immediate update
    lv_timer_handler();
}

void updateProgress(int percentage) {
    if (!screenVisible || !progressBar) return;

    // Clamp to valid range
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    lv_bar_set_value(progressBar, percentage, LV_ANIM_OFF);

    // Force immediate update
    lv_timer_handler();
}

void hide() {
    if (!screenVisible || !bootScreen) return;

    ESP_LOGI(TAG, "Hiding boot progress screen");

    // Clean up the screen objects
    lv_obj_del(bootScreen);
    bootScreen = nullptr;
    statusLabel = nullptr;
    progressBar = nullptr;
    screenVisible = false;

    ESP_LOGI(TAG, "Boot progress screen hidden and cleaned up");
}

bool isVisible() {
    return screenVisible;
}

}  // namespace BootProgress
