/**
 * @file LogoBrowserExample.c
 * @brief Example usage of the improved Logo Browser widget
 * 
 * This file demonstrates how to integrate and use the logo browser
 * in your application screens.
 */

#include "LogoBrowser.h"
#include <lvgl.h>
#include <esp_log.h>

static const char* TAG = "LogoBrowserExample";

// Example screen with logo browser
static lv_obj_t* example_screen = NULL;
static lv_obj_t* logo_browser = NULL;

/**
 * @brief Event handler for logo selection
 */
static void logo_selected_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        const char* selected_path = logo_browser_get_selected_logo(logo_browser);
        if (selected_path) {
            ESP_LOGI(TAG, "Logo selected: %s", selected_path);
            // Handle logo selection here
        }
    }
}

/**
 * @brief Create example screen with logo browser
 */
lv_obj_t* create_logo_browser_example_screen(void) {
    // Create screen
    example_screen = lv_obj_create(NULL);
    
    // Create logo browser
    logo_browser = logo_browser_create(example_screen);
    if (!logo_browser) {
        ESP_LOGE(TAG, "Failed to create logo browser");
        return example_screen;
    }
    
    // Scan for logos
    int logo_count = logo_browser_scan_directory(logo_browser, "/logos");
    ESP_LOGI(TAG, "Found %d logos", logo_count);
    
    // Add value changed event for selection
    lv_obj_add_event_cb(logo_browser, logo_selected_event_handler, 
                       LV_EVENT_VALUE_CHANGED, NULL);
    
    return example_screen;
}

/**
 * @brief Clean up example screen
 */
void cleanup_logo_browser_example_screen(void) {
    if (logo_browser) {
        logo_browser_cleanup(logo_browser);
        logo_browser = NULL;
    }
    
    if (example_screen) {
        lv_obj_del(example_screen);
        example_screen = NULL;
    }
}

/**
 * @brief Advanced usage examples
 */
void logo_browser_advanced_examples(void) {
    if (!logo_browser) return;
    
    // Example 1: Navigate to specific page
    logo_browser_next_page(logo_browser);
    logo_browser_prev_page(logo_browser);
    
    // Example 2: Set specific logo selection
    logo_browser_set_selected_logo(logo_browser, 5);  // Select 6th logo (0-based)
    
    // Example 3: Get current selection
    const char* current_logo = logo_browser_get_selected_logo(logo_browser);
    if (current_logo) {
        ESP_LOGI(TAG, "Current selection: %s", current_logo);
    }
    
    // Example 4: Refresh logos (useful after adding/removing logos)
    logo_browser_scan_directory(logo_browser, "/logos");
}

/**
 * @brief Integration with application
 * 
 * This shows how to integrate the logo browser into your app's
 * screen management system.
 */
void integrate_logo_browser_in_app(void) {
    // Create the screen
    lv_obj_t* screen = create_logo_browser_example_screen();
    
    // Load the screen
    lv_scr_load(screen);
    
    // Later, when switching screens or shutting down
    // cleanup_logo_browser_example_screen();
}

/**
 * @brief Custom styling example
 * 
 * While the logo browser comes with a polished dark theme,
 * you can still customize it further if needed.
 */
void customize_logo_browser_style(lv_obj_t* browser) {
    // The browser uses a hierarchical structure, so you can
    // access and modify child elements if needed
    
    // Example: Change the background color
    lv_obj_set_style_bg_color(browser, lv_color_hex(0x2a2a2a), 0);
    
    // Note: Most styling is handled internally for consistency
    // Only modify if you have specific requirements
}