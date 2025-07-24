#pragma once

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enhanced Logo Browser with dual-core architecture and advanced features
 * 
 * Features:
 * - Dual-core optimized architecture (Core 0: UI, Core 1: background processing)
 * - Asynchronous logo loading with preview generation
 * - Multi-select with batch operations
 * - Touch gestures and swipe navigation
 * - Smart caching with LRU eviction
 * - Lazy loading with progressive enhancement
 * - Real-time thumbnail generation
 * - Intuitive UX with haptic feedback
 * - Advanced animations and transitions
 */

#define ENHANCED_LOGOS_PER_PAGE 6
#define MAX_SELECTED_LOGOS 32
#define PREVIEW_CACHE_SIZE 128
#define THUMBNAIL_SIZE 96
#define GESTURE_THRESHOLD 50

// Enhanced browser states
typedef enum {
    ENHANCED_BROWSER_STATE_IDLE,
    ENHANCED_BROWSER_STATE_LOADING,
    ENHANCED_BROWSER_STATE_SEARCHING,
    ENHANCED_BROWSER_STATE_PROCESSING,
    ENHANCED_BROWSER_STATE_MULTI_SELECT,
    ENHANCED_BROWSER_STATE_ERROR
} enhanced_browser_state_t;

// View modes
typedef enum {
    VIEW_MODE_GRID,
    VIEW_MODE_LIST,
    VIEW_MODE_LARGE_ICONS
} view_mode_t;

// Sort options
typedef enum {
    SORT_NAME_ASC,
    SORT_NAME_DESC,
    SORT_DATE_ASC,
    SORT_DATE_DESC,
    SORT_SIZE_ASC,
    SORT_SIZE_DESC
} sort_mode_t;

// Background task types
typedef enum {
    TASK_LOAD_THUMBNAILS,
    TASK_GENERATE_PREVIEW,
    TASK_SCAN_DIRECTORY,
    TASK_PROCESS_BATCH
} background_task_type_t;

// Background task message
typedef struct {
    background_task_type_t type;
    int page_index;
    char path[64];
    void* user_data;
} background_task_t;

// Logo metadata for enhanced features
typedef struct {
    char path[64];
    char filename[32];
    size_t file_size;
    uint64_t last_modified;
    bool has_thumbnail;
    bool is_selected;
    uint16_t width;
    uint16_t height;
    lv_img_dsc_t* thumbnail_cache;
} logo_metadata_t;

/**
 * @brief Create enhanced logo browser with advanced features
 * 
 * @param parent Parent object for the browser
 * @return Pointer to the created browser, or NULL on failure
 */
lv_obj_t* enhanced_logo_browser_create(lv_obj_t* parent);

/**
 * @brief Initialize dual-core background processing
 * 
 * @param browser_obj Browser object
 * @return true if initialization successful
 */
bool enhanced_logo_browser_init_background(lv_obj_t* browser_obj);

/**
 * @brief Scan directory with background processing
 * 
 * @param browser_obj Browser object
 * @param directory Directory to scan
 * @return Number of logos found (preliminary count)
 */
int enhanced_logo_browser_scan_directory(lv_obj_t* browser_obj, const char* directory);

/**
 * @brief Set view mode
 * 
 * @param browser_obj Browser object
 * @param mode View mode to set
 */
void enhanced_logo_browser_set_view_mode(lv_obj_t* browser_obj, view_mode_t mode);

/**
 * @brief Set sort mode
 * 
 * @param browser_obj Browser object
 * @param mode Sort mode to set
 */
void enhanced_logo_browser_set_sort_mode(lv_obj_t* browser_obj, sort_mode_t mode);

/**
 * @brief Enable/disable multi-select mode
 * 
 * @param browser_obj Browser object
 * @param enabled True to enable multi-select
 */
void enhanced_logo_browser_set_multi_select(lv_obj_t* browser_obj, bool enabled);

/**
 * @brief Get selected logos
 * 
 * @param browser_obj Browser object
 * @param paths Array to store selected logo paths
 * @param max_count Maximum number of paths to return
 * @return Number of selected logos
 */
int enhanced_logo_browser_get_selected_logos(lv_obj_t* browser_obj, const char** paths, int max_count);

/**
 * @brief Clear selection
 * 
 * @param browser_obj Browser object
 */
void enhanced_logo_browser_clear_selection(lv_obj_t* browser_obj);

/**
 * @brief Show logo preview in overlay
 * 
 * @param browser_obj Browser object
 * @param logo_path Path to logo to preview
 */
void enhanced_logo_browser_show_preview(lv_obj_t* browser_obj, const char* logo_path);

/**
 * @brief Set search filter with real-time results
 * 
 * @param browser_obj Browser object
 * @param filter Search filter string
 */
void enhanced_logo_browser_set_filter(lv_obj_t* browser_obj, const char* filter);

/**
 * @brief Navigate to next page with animation
 * 
 * @param browser_obj Browser object
 */
void enhanced_logo_browser_next_page(lv_obj_t* browser_obj);

/**
 * @brief Navigate to previous page with animation
 * 
 * @param browser_obj Browser object
 */
void enhanced_logo_browser_prev_page(lv_obj_t* browser_obj);

/**
 * @brief Jump to specific page
 * 
 * @param browser_obj Browser object
 * @param page_index Target page index
 */
void enhanced_logo_browser_goto_page(lv_obj_t* browser_obj, int page_index);

/**
 * @brief Set progress callback for background operations
 * 
 * @param browser_obj Browser object
 * @param callback Progress callback function
 * @param user_data User data for callback
 */
void enhanced_logo_browser_set_progress_callback(lv_obj_t* browser_obj, 
                                               void (*callback)(int progress, void* user_data),
                                               void* user_data);

/**
 * @brief Set selection change callback
 * 
 * @param browser_obj Browser object
 * @param callback Selection callback function
 * @param user_data User data for callback
 */
void enhanced_logo_browser_set_selection_callback(lv_obj_t* browser_obj,
                                                void (*callback)(const char** selected, int count, void* user_data),
                                                void* user_data);

/**
 * @brief Refresh browser content
 * 
 * @param browser_obj Browser object
 * @param force_rescan Force rescan of directory
 */
void enhanced_logo_browser_refresh(lv_obj_t* browser_obj, bool force_rescan);

/**
 * @brief Cleanup enhanced browser and background tasks
 * 
 * @param browser_obj Browser object
 */
void enhanced_logo_browser_cleanup(lv_obj_t* browser_obj);

#ifdef __cplusplus
}
#endif