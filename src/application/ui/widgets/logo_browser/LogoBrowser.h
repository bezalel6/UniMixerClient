#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Advanced paged logo browser component
 *
 * This component provides a sophisticated paged interface for browsing logos
 * stored on the SD card. Features include:
 * - Modern dark theme with card-based UI
 * - Real-time search with debouncing
 * - Smooth animations and transitions
 * - Loading indicators and status feedback
 * - Keyboard input with animated appearance
 * - Efficient memory management
 * - State-based architecture
 * 
 * The browser displays logos in a responsive 3x2 grid with navigation controls.
 */

#define LOGOS_PER_PAGE 6  // 2x3 grid layout

/**
 * @brief Browser states for visual feedback
 */
typedef enum {
    BROWSER_STATE_IDLE,      /**< Ready for interaction */
    BROWSER_STATE_LOADING,   /**< Loading logos */
    BROWSER_STATE_SEARCHING, /**< Applying search filter */
    BROWSER_STATE_ERROR      /**< Error state */
} browser_state_t;

/**
 * @brief Create a logo browser component
 * 
 * Creates a full-featured logo browser with search, navigation, and selection
 * capabilities. The browser automatically manages memory and provides smooth
 * animations for all interactions.
 * 
 * @param parent Parent object where the browser will be created
 * @return Pointer to the created logo browser container, or NULL on failure
 * 
 * @note The browser requires the SimpleLogoManager to be initialized
 * @note Call logo_browser_cleanup() when done to free resources
 */
lv_obj_t* logo_browser_create(lv_obj_t* parent);

/**
 * @brief Scan SD card for logo files and populate the browser
 * 
 * Scans the specified directory (or default logos directory) for PNG files
 * and populates the browser with paginated results. Applies any active
 * search filter during the scan.
 * 
 * @param browser Pointer to logo browser object
 * @param logo_directory Directory path to scan (e.g., "S:/logos/") - unused in current implementation
 * @return Number of logos found
 * 
 * @note The directory parameter is kept for API compatibility but scanning
 *       is handled by SimpleLogoManager
 */
int logo_browser_scan_directory(lv_obj_t* browser, const char* logo_directory);

/**
 * @brief Navigate to the next page
 * 
 * Advances to the next page of logos with smooth animation.
 * Automatically disabled when on the last page.
 * 
 * @param browser Pointer to logo browser object
 */
void logo_browser_next_page(lv_obj_t* browser);

/**
 * @brief Navigate to the previous page
 * 
 * Goes back to the previous page of logos with smooth animation.
 * Automatically disabled when on the first page.
 * 
 * @param browser Pointer to logo browser object
 */
void logo_browser_prev_page(lv_obj_t* browser);

/**
 * @brief Get the path of the currently selected logo
 * 
 * Returns the file path of the logo that is currently selected.
 * The selection is maintained across page navigation.
 * 
 * @param browser Pointer to logo browser object
 * @return Pointer to logo file path string, or NULL if no selection
 * 
 * @note The returned string is owned by the browser and should not be freed
 */
const char* logo_browser_get_selected_logo(lv_obj_t* browser);

/**
 * @brief Set the selected logo by global index
 * 
 * Selects a logo by its global index (not page-relative).
 * Automatically navigates to the page containing the specified logo.
 * 
 * @param browser Pointer to logo browser object
 * @param logo_index Global index of logo to select (0-based)
 * 
 * @note If the index is out of range, the selection is not changed
 */
void logo_browser_set_selected_logo(lv_obj_t* browser, uint16_t logo_index);

/**
 * @brief Clean up the logo browser and free resources
 * 
 * Properly cleans up all resources used by the logo browser including:
 * - Keyboard if visible
 * - Search timer if active
 * - Allocated memory for paths
 * - Custom styles
 * - Browser data structure
 * 
 * @param browser Pointer to logo browser object
 * 
 * @note This function should be called before deleting the browser object
 */
void logo_browser_cleanup(lv_obj_t* browser);

#ifdef __cplusplus
}
#endif