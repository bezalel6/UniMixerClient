#include "process_selector.h"
#include "esp_log.h"
#include "ui/ui.h"
#include "../application/audio/AudioManager.h"
#include "../application/audio/AudioData.h"
#include "../logo/LogoManager.h"

extern "C" {
#include "ui/components/lv_image_roller.h"
}

static const char* TAG = "ProcessSelector";

// Dynamic process data storage
static std::vector<String> process_names;
static std::vector<String> process_image_paths;

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

    ESP_LOGW(TAG, "Selected process: %s (index: %d)", process_name, selected);
    
    // Log the image path being used
    if (selected < process_image_paths.size()) {
        ESP_LOGW(TAG, "Image path for selection: %s", process_image_paths[selected].c_str());
    }

    // Update the audio manager with the selected process
    Application::Audio::AudioManager& audioManager = Application::Audio::AudioManager::getInstance();

    // Set the selected audio device/process for the Single tab
    if (audioManager.getCurrentTab() == Events::UI::TabState::SINGLE) {
        // Use the correct method name
        audioManager.selectDevice(process_name);

        ESP_LOGW(TAG, "Audio process selection sent: %s", process_name);
        
        // Force LVGL to refresh the image roller display
        lv_obj_invalidate(roller);
    }
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
    
    // Hide the centered image if it exists and is visible
    extern lv_obj_t* ui_img;
    if (ui_img) {
        lv_obj_add_flag(ui_img, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Hidden ui_img overlay");
    }

    // Create the image roller in the single tab
    process_image_roller = lv_image_roller_create(ui_Single);

    // First, ensure the volume slider is visible and properly positioned
    if (ui_containerSingleVolumeSlider) {
        lv_obj_clear_flag(ui_containerSingleVolumeSlider, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Get the parent dimensions for proper layout
    lv_coord_t parent_height = lv_obj_get_height(ui_Single);
    lv_coord_t parent_width = lv_obj_get_width(ui_Single);

    // If parent dimensions are not yet calculated, use default values
    if (parent_height <= 0) parent_height = 400;
    if (parent_width <= 0) parent_width = 800;
    
    ESP_LOGI(TAG, "Parent dimensions: %dx%d", parent_width, parent_height);

    // Calculate optimal roller dimensions
    lv_coord_t roller_width = 280;
    lv_coord_t roller_height = 240;

    // Size and position - centered in upper portion
    lv_obj_set_size(process_image_roller, roller_width, roller_height);
    lv_obj_align(process_image_roller, LV_ALIGN_TOP_MID, 0, 20);

    // Get actual device data from audio manager
    const auto& devices = Application::Audio::AudioManager::getInstance().getAllDevices();
    
    ESP_LOGI(TAG, "Process selector init: Found %d devices from AudioManager", devices.size());
    
    // If no devices yet, try to trigger a refresh from the audio manager
    if (devices.empty()) {
        ESP_LOGW(TAG, "No devices available yet - will show placeholder and wait for device list update");
        
        // Show a placeholder temporarily
        const char* placeholder = "Loading...";
        const char* placeholder_path[] = {"S:/logos/default.png"};
        lv_image_roller_set_options(process_image_roller,
                                    placeholder,
                                    placeholder_path,
                                    NULL,
                                    1,
                                    true);
    } else {
        // Build process names and image paths from actual device data
        process_names.clear();
        process_image_paths.clear();
        
        String names_string = "";
        for (size_t i = 0; i < devices.size(); i++) {
            if (i > 0) {
                names_string += "\n";
            }
            names_string += devices[i].processName;
            process_names.push_back(devices[i].processName);
            
            ESP_LOGI(TAG, "Adding device %d: %s", i, devices[i].processName.c_str());
            
            // Use LogoManager to get the proper logo path
            String logo_path = AssetManagement::LogoManager::getInstance().getLVGLPath(devices[i].processName);
            process_image_paths.push_back(logo_path);
            
            // Check if logo exists, if not request it
            if (!AssetManagement::LogoManager::getInstance().hasLogo(devices[i].processName)) {
                ESP_LOGW(TAG, "Logo not found for %s, requesting from host", devices[i].processName.c_str());
                AssetManagement::LogoManager::getInstance().requestLogo(devices[i].processName, 
                    [processName = devices[i].processName](bool success, uint8_t* data, size_t size, const String& error) {
                        if (success) {
                            ESP_LOGI(TAG, "Logo received for %s, size: %d bytes", processName.c_str(), size);
                            // The image roller will automatically use the new logo on next refresh
                        } else {
                            ESP_LOGE(TAG, "Failed to get logo for %s: %s", processName.c_str(), error.c_str());
                        }
                    });
            }
        }
        
        // Convert paths to C-style array for the roller
        const char** paths_array = (const char**)lv_malloc(process_image_paths.size() * sizeof(char*));
        for (size_t i = 0; i < process_image_paths.size(); i++) {
            paths_array[i] = process_image_paths[i].c_str();
            ESP_LOGW(TAG, "Setting image path %d: %s", i, paths_array[i]);
        }
        
        lv_image_roller_set_options(process_image_roller,
                                    names_string.c_str(),
                                    paths_array,
                                    NULL,   // No embedded images
                                    devices.size(),
                                    true);  // Use SD card
        
        lv_free(paths_array);
        
        ESP_LOGI(TAG, "Image roller configured with %d devices", devices.size());
    }

    // Configure appearance - elegant sizing with appropriate visible rows
    lv_image_roller_set_image_size(process_image_roller, 90, 90);
    lv_image_roller_set_visible_row_count(process_image_roller, 3);
    
    // Ensure the roller is visible
    lv_obj_clear_flag(process_image_roller, LV_OBJ_FLAG_HIDDEN);
    
    ESP_LOGI(TAG, "Image roller created at position (%d, %d) with size %dx%d", 
             lv_obj_get_x(process_image_roller), lv_obj_get_y(process_image_roller),
             lv_obj_get_width(process_image_roller), lv_obj_get_height(process_image_roller));

    // Add selection change handler
    lv_obj_add_event_cb(process_image_roller, process_selected_cb,
                        LV_EVENT_IMAGE_ROLLER_CHANGED, NULL);

    // Position the volume slider container below the roller
    if (ui_containerSingleVolumeSlider) {
        // Clear any hidden flags
        lv_obj_clear_flag(ui_containerSingleVolumeSlider, LV_OBJ_FLAG_HIDDEN);
        
        // Position below the image roller with some spacing
        lv_obj_set_size(ui_containerSingleVolumeSlider, 400, 80);
        lv_obj_align_to(ui_containerSingleVolumeSlider, process_image_roller, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
        
        ESP_LOGI(TAG, "Volume slider positioned below image roller");
    } else {
        ESP_LOGW(TAG, "Volume slider container not found!");
    }

    // Hide the original panel containing the dropdown
    if (ui_pnlSingleSelectAudioDevice) {
        lv_obj_add_flag(ui_pnlSingleSelectAudioDevice, LV_OBJ_FLAG_HIDDEN);
    }

    // Add a visible label to confirm the tab is working
    lv_obj_t* debug_label = lv_label_create(ui_Single);
    lv_label_set_text(debug_label, "Select Audio Process:");
    lv_obj_align(debug_label, LV_ALIGN_TOP_LEFT, 10, 5);
    
    ESP_LOGI(TAG, "Process selector initialized with %d processes", devices.size());
    
    // Log the visibility state of key components
    ESP_LOGI(TAG, "Image roller visible: %s", 
             lv_obj_has_flag(process_image_roller, LV_OBJ_FLAG_HIDDEN) ? "NO" : "YES");
    ESP_LOGI(TAG, "Volume slider visible: %s", 
             ui_containerSingleVolumeSlider && !lv_obj_has_flag(ui_containerSingleVolumeSlider, LV_OBJ_FLAG_HIDDEN) ? "YES" : "NO");
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

    // Find the process index by name using the actual process list
    for (size_t i = 0; i < process_names.size(); i++) {
        if (process_names[i] == process_name) {
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

    // Show the original panel
    if (ui_pnlSingleSelectAudioDevice) {
        lv_obj_remove_flag(ui_pnlSingleSelectAudioDevice, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Process selector cleanup complete");
}

/**
 * Synchronize the process selector with current audio state
 */
void process_selector_sync_with_audio_state() {
    if (!process_image_roller) {
        ESP_LOGE(TAG, "Process selector not initialized");
        return;
    }

    // Get current audio state
    Application::Audio::AudioManager& audioManager = Application::Audio::AudioManager::getInstance();
    const Application::Audio::AudioAppState& state = audioManager.getState();

    // Only sync if we're on the Single tab
    if (audioManager.getCurrentTab() == Events::UI::TabState::SINGLE) {
        // Get the current selected device name for the Single tab
        String deviceName = state.getCurrentSelectedDeviceName();
        
        if (!deviceName.isEmpty()) {
            // Try to set the selection to match
            process_selector_set_selected_process(deviceName.c_str());
            ESP_LOGI(TAG, "Synced process selector with audio state: %s", deviceName.c_str());
        }
    }
}

/**
 * Refresh the process list from the audio manager
 */
void process_selector_refresh_devices() {
    if (!process_image_roller) {
        ESP_LOGE(TAG, "Process selector not initialized");
        return;
    }
    
    // Get actual device data from audio manager
    const auto& devices = Application::Audio::AudioManager::getInstance().getAllDevices();
    
    // Build process names and image paths from actual device data
    process_names.clear();
    process_image_paths.clear();
    
    String names_string = "";
    for (size_t i = 0; i < devices.size(); i++) {
        if (i > 0) {
            names_string += "\n";
        }
        names_string += devices[i].processName;
        process_names.push_back(devices[i].processName);
        
        // Generate logo path based on process name
        String logo_path = "S:/logos/" + devices[i].processName + ".png";
        process_image_paths.push_back(logo_path);
    }
    
    // Only configure if we have devices
    if (!devices.empty()) {
        // Convert paths to C-style array for the roller
        const char** paths_array = (const char**)lv_malloc(process_image_paths.size() * sizeof(char*));
        for (size_t i = 0; i < process_image_paths.size(); i++) {
            paths_array[i] = process_image_paths[i].c_str();
            ESP_LOGW(TAG, "Setting image path %d: %s", i, paths_array[i]);
        }
        
        lv_image_roller_set_options(process_image_roller,
                                    names_string.c_str(),
                                    paths_array,
                                    NULL,   // No embedded images
                                    devices.size(),
                                    true);  // Use SD card
        
        lv_free(paths_array);
        ESP_LOGI(TAG, "Refreshed process selector with %d devices", devices.size());
    } else {
        // No devices - show a placeholder
        const char* placeholder = "No devices";
        const char* placeholder_path[] = {"S:/logos/default.png"};
        lv_image_roller_set_options(process_image_roller,
                                    placeholder,
                                    placeholder_path,
                                    NULL,
                                    1,
                                    true);
        ESP_LOGW(TAG, "No devices available for process selector");
    }
}
