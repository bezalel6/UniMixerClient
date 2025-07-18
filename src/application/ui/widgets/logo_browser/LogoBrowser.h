#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Paged logo browser component
 *
 * This component provides a paged interface for browsing through all logos
 * stored on the SD card. It displays logos in a grid with navigation controls.
 */

#define LOGOS_PER_PAGE 6  // 2x3 grid

typedef struct {
    lv_obj_t* container;                   /**< Main container */
    lv_obj_t* grid_container;              /**< Grid container for logos */
    lv_obj_t* nav_container;               /**< Navigation container */
    lv_obj_t* btn_prev;                    /**< Previous page button */
    lv_obj_t* btn_next;                    /**< Next page button */
    lv_obj_t* page_label;                  /**< Page indicator label */
    lv_obj_t* logo_images[LOGOS_PER_PAGE]; /**< Array of image objects */
    void* internal_data;                   /**< Internal data pointer */
    uint16_t current_page;                 /**< Current page index */
    uint16_t total_pages;                  /**< Total number of pages */
    uint16_t selected_logo;                /**< Currently selected logo index */
} logo_browser_t;

/**
 * Create a logo browser component
 * @param parent pointer to parent object
 * @return pointer to the created logo browser container
 */
lv_obj_t* logo_browser_create(lv_obj_t* parent);

/**
 * Scan SD card for logo files and populate the browser
 * @param browser pointer to logo browser object
 * @param logo_directory directory path to scan (e.g., "S:/logos/")
 * @return number of logos found
 */
int logo_browser_scan_directory(lv_obj_t* browser, const char* logo_directory);

/**
 * Go to next page
 * @param browser pointer to logo browser object
 */
void logo_browser_next_page(lv_obj_t* browser);

/**
 * Go to previous page
 * @param browser pointer to logo browser object
 */
void logo_browser_prev_page(lv_obj_t* browser);

/**
 * Get the path of the currently selected logo
 * @param browser pointer to logo browser object
 * @return pointer to logo file path string
 */
const char* logo_browser_get_selected_logo(lv_obj_t* browser);

/**
 * Set the selected logo by index
 * @param browser pointer to logo browser object
 * @param logo_index global index of logo to select
 */
void logo_browser_set_selected_logo(lv_obj_t* browser, uint16_t logo_index);

/**
 * Cleanup the logo browser
 * @param browser pointer to logo browser object
 */
void logo_browser_cleanup(lv_obj_t* browser);

#ifdef __cplusplus
}
#endif
