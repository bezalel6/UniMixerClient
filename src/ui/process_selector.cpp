#include "process_selector.h"
#include "esp_log.h"
#include "ui/ui.h"

extern "C" {
#include "ui/components/lv_image_roller.h"
}

static const char* TAG = "ProcessSelector";

// Process data based on actual SD card contents
static const char* available_processes =
    "Chrome\n"
    "COD\n"
    "Legcord\n"
    "YouTube Music";

// Process image paths matching actual SD card files
static const char* process_image_paths[] = {
    "S:/logos/chrome.png",
    "S:/logos/cod.png",
    "S:/logos/Legcord.png",
    "S:/logos/YouTube Music.png"};

// Global reference to the image roller
static lv_obj_t* process_image_roller = NULL;

/**
 * Event handler for process selection changes
 */
static void process_selected_cb(lv_event_t* e) {
    lv_obj_t* roller = (lv_obj_t*)lv_event_get_target(e);
    uint16_t selected = lv_image_roller_get_selected(roller);

    char process_name[64];
    lv_image_roller_get_selected_str(roller, process_name, sizeof(process_name));

    ESP_LOGI(TAG, "Selected process: %s (index: %d)", process_name, selected);

    // Send message to audio system (example - same behavior as original dropdown)
    // Message msg = Message::createAudioProcessSelect(process_name, selected);
    // MessagingService::getInstance()->sendMessage(msg);
}

/**
 * Initialize the process selector in the Single tab
 */
void process_selector_init() {
    if (!ui_Single) {
        ESP_LOGE(TAG, "Single tab not initialized");
        return;
    }

    // Hide the original dropdown
    if (ui_selectAudioDevice) {
        lv_obj_add_flag(ui_selectAudioDevice, LV_OBJ_FLAG_HIDDEN);
    }

    // Create the image roller in the single tab
    process_image_roller = lv_image_roller_create(ui_Single);

    // Size and position - centered and properly sized with more height
    lv_obj_set_size(process_image_roller, 320, 380);
    lv_obj_align(process_image_roller, LV_ALIGN_CENTER, 0, -60);

    // Configure with actual process data from SD card
    lv_image_roller_set_options(process_image_roller,
                                available_processes,
                                process_image_paths,
                                NULL,   // No embedded images
                                4,      // Number of processes (Chrome, COD, Legcord, YouTube Music)
                                true);  // Use SD card

    // Configure appearance - elegant sizing with more visible rows
    lv_image_roller_set_image_size(process_image_roller, 100, 100);
    lv_image_roller_set_visible_row_count(process_image_roller, 4);

    // Add selection change handler
    lv_obj_add_event_cb(process_image_roller, process_selected_cb,
                        LV_EVENT_IMAGE_ROLLER_CHANGED, NULL);

    // Move the volume slider container to make room and align properly
    if (ui_containerSingleVolumeSlider) {
        lv_obj_set_y(ui_containerSingleVolumeSlider, 120);
        lv_obj_set_size(ui_containerSingleVolumeSlider, 450, 100);
    }

    ESP_LOGI(TAG, "Process selector initialized with %d processes", 4);
}

/**
 * Update the process list dynamically
 */
void process_selector_update_processes(const char* processes, const char** image_paths, uint16_t count) {
    if (!process_image_roller) {
        ESP_LOGE(TAG, "Process selector not initialized");
        return;
    }

    lv_image_roller_set_options(process_image_roller,
                                processes,
                                image_paths,
                                NULL,
                                count,
                                true);

    ESP_LOGI(TAG, "Updated process list with %d processes", count);
}
#include <string.h>
/**
 * Set the selected process by name
 */
void process_selector_set_selected_process(const char* process_name) {
    if (!process_image_roller) {
        ESP_LOGE(TAG, "Process selector not initialized");
        return;
    }

    // Find the process index by name
    char current_process[64];
    uint16_t count = 4;  // Current process count

    for (uint16_t i = 0; i < count; i++) {
        lv_image_roller_set_selected(process_image_roller, i, LV_ANIM_OFF);
        lv_image_roller_get_selected_str(process_image_roller, current_process, sizeof(current_process));

        if (strcmp(current_process, process_name) == 0) {
            lv_image_roller_set_selected(process_image_roller, i, LV_ANIM_ON);
            ESP_LOGI(TAG, "Selected process: %s at index %d", process_name, i);
            return;
        }
    }

    ESP_LOGW(TAG, "Process not found: %s", process_name);
}

/**
 * Get the currently selected process name
 */
void process_selector_get_selected_process(char* buffer, size_t buffer_size) {
    if (!process_image_roller) {
        ESP_LOGE(TAG, "Process selector not initialized");
        if (buffer && buffer_size > 0) {
            buffer[0] = '\0';
        }
        return;
    }

    lv_image_roller_get_selected_str(process_image_roller, buffer, buffer_size);
}

/**
 * Get the image roller widget for additional customization
 */
lv_obj_t* process_selector_get_widget() {
    return process_image_roller;
}

/**
 * Cleanup the process selector
 */
void process_selector_cleanup() {
    if (process_image_roller) {
        // Cleanup memory properly
        lv_image_roller_cleanup(process_image_roller);
        lv_obj_delete(process_image_roller);
        process_image_roller = NULL;
    }

    // Show the original dropdown again
    if (ui_selectAudioDevice) {
        lv_obj_remove_flag(ui_selectAudioDevice, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Process selector cleanup complete");
}
