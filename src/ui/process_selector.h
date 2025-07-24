#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process selector using image roller component
 * 
 * This module provides a visual process selector for the Single tab,
 * replacing the dropdown with an image roller showing process logos.
 */

/**
 * Initialize the process selector in the Single tab
 * This will hide the original dropdown and create the image roller
 */
void process_selector_init();

/**
 * Update the process list dynamically
 * @param processes newline-separated process names
 * @param image_paths array of image file paths
 * @param count number of processes
 */
void process_selector_update_processes(const char* processes, const char** image_paths, uint16_t count);

/**
 * Set the selected process by name
 * @param process_name name of the process to select
 */
void process_selector_set_selected_process(const char* process_name);

/**
 * Get the currently selected process name
 * @param buffer buffer to store the process name
 * @param buffer_size size of the buffer
 */
void process_selector_get_selected_process(char* buffer, size_t buffer_size);

/**
 * Get the image roller widget for additional customization
 * @return pointer to the image roller widget
 */
lv_obj_t* process_selector_get_widget();

/**
 * Cleanup the process selector
 * This will delete the image roller and restore the original dropdown
 */
void process_selector_cleanup();

/**
 * Synchronize the process selector with current audio state
 * This updates the selection to match the current audio device
 */
void process_selector_sync_with_audio_state();

/**
 * Refresh the process list from the audio manager
 * This updates the image roller with the latest device list
 */
void process_selector_refresh_devices();

#ifdef __cplusplus
}
#endif