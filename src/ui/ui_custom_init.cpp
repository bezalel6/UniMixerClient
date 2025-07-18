#include "ui_custom_init.h"
#include "process_selector.h"
#include "esp_log.h"

static const char* TAG = "UICustomInit";

/**
 * Initialize custom UI components after SquareLine Studio UI is created
 * This function should be called after ui_init() to add custom components
 */
void ui_custom_init() {
    ESP_LOGI(TAG, "Initializing custom UI components");
    
    // Initialize the process selector in the Single tab
    process_selector_init();
    
    ESP_LOGI(TAG, "Custom UI initialization complete");
}

/**
 * Cleanup custom UI components
 * This function should be called before ui_destroy()
 */
void ui_custom_cleanup() {
    ESP_LOGI(TAG, "Cleaning up custom UI components");
    
    // Cleanup the process selector
    process_selector_cleanup();
    
    ESP_LOGI(TAG, "Custom UI cleanup complete");
}