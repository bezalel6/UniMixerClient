#include "BSODHandler.h"
#include "BuildInfo.h"
#include <lvgl.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "BSOD";
static bool bsodReady = false;
static lv_obj_t* bsodScreen = nullptr;
static bool bsodActive = false;

namespace BSODHandler {

bool init() {
    ESP_LOGI(TAG, "Initializing BSOD handler");
    
    // Mark as ready - we'll create the screen on-demand when needed
    bsodReady = true;
    
    // Install custom panic handler
    extern void esp_panic_handler_reconfigure(void);
    esp_panic_handler_reconfigure();
    
    return true;
}

bool isReady() {
    return bsodReady;
}

void show(const char* message, const char* file, int line) {
    // Prevent recursive BSOD
    if (bsodActive) {
        // If we're already in BSOD, just halt
        esp_task_wdt_deinit();
        while(1) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    bsodActive = true;
    
    ESP_LOGE(TAG, "CRITICAL FAILURE: %s", message);
    if (file) {
        ESP_LOGE(TAG, "Location: %s:%d", file, line);
    }
    
    // Disable watchdog to prevent reboot while showing BSOD
    esp_task_wdt_deinit();
    
    // Check if LVGL is initialized
    if (!lv_is_initialized()) {
        ESP_LOGE(TAG, "LVGL not initialized - cannot show BSOD screen");
        // Fall back to infinite loop
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGE(TAG, "SYSTEM HALTED: %s", message);
        }
    }
    
    // Create BSOD screen
    bsodScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bsodScreen, lv_color_hex(0x0078D7), 0); // Windows 10 blue
    
    // Create container for centered content
    lv_obj_t* container = lv_obj_create(bsodScreen);
    lv_obj_set_size(container, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_center(container);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Sad face
    lv_obj_t* sadFace = lv_label_create(container);
    lv_label_set_text(sadFace, ":(");
    lv_obj_set_style_text_color(sadFace, lv_color_white(), 0);
    lv_obj_set_style_text_font(sadFace, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_bottom(sadFace, 20, 0);
    
    // Error title
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "Your device ran into a problem");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);
    
    // Main error message
    lv_obj_t* msgLabel = lv_label_create(container);
    lv_label_set_text(msgLabel, message);
    lv_label_set_long_mode(msgLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msgLabel, LV_PCT(100));
    lv_obj_set_style_text_color(msgLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(msgLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(msgLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(msgLabel, 20, 0);
    
    // Technical details
    if (file) {
        lv_obj_t* techDetails = lv_label_create(container);
        char techBuf[256];
        snprintf(techBuf, sizeof(techBuf), "Technical details:\n%s:%d", file, line);
        lv_label_set_text(techDetails, techBuf);
        lv_label_set_long_mode(techDetails, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(techDetails, LV_PCT(100));
        lv_obj_set_style_text_color(techDetails, lv_color_white(), 0);
        lv_obj_set_style_text_font(techDetails, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(techDetails, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_bottom(techDetails, 10, 0);
    }
    
    // Build info
    lv_obj_t* buildInfo = lv_label_create(container);
    lv_label_set_text(buildInfo, getBuildInfo());
    lv_obj_set_style_text_color(buildInfo, lv_color_white(), 0);
    lv_obj_set_style_text_font(buildInfo, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_opa(buildInfo, LV_OPA_70, 0);
    lv_obj_set_style_pad_bottom(buildInfo, 20, 0);
    
    // Restart instruction
    lv_obj_t* restart = lv_label_create(container);
    lv_label_set_text(restart, "Please restart your device");
    lv_obj_set_style_text_color(restart, lv_color_white(), 0);
    lv_obj_set_style_text_font(restart, &lv_font_montserrat_16, 0);
    
    // Load and display the screen
    lv_scr_load(bsodScreen);
    
    // Force LVGL to process and render immediately
    lv_task_handler();
    
    // Infinite loop - system is halted
    while(1) {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

} // namespace BSODHandler

// ESP panic handler hook
extern "C" void bsod_panic_handler(const char* reason) {
    if (BSODHandler::isReady()) {
        BSODHandler::show(reason ? reason : "System panic", nullptr, 0);
    }
}