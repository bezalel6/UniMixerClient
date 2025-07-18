#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Custom LVGL image roller component for visual selection
 * 
 * This component combines an LVGL roller with image display capabilities,
 * ideal for selecting audio devices or other items with associated icons.
 */

/** Custom event code for image roller value changes */
#define LV_EVENT_IMAGE_ROLLER_CHANGED ((lv_event_code_t)(LV_EVENT_LAST + 1))

/** Image roller data structure */
typedef struct {
    lv_obj_t* container;        /**< Main container object */
    lv_obj_t* image_area;       /**< Image display container */
    lv_obj_t* current_image;    /**< Currently displayed image */
    lv_obj_t* roller;           /**< Text roller object */
    const char** image_paths;   /**< Array of image paths */
    lv_image_dsc_t** embedded_images; /**< Array of embedded image descriptors */
    uint16_t image_count;       /**< Number of images */
    uint16_t current_index;     /**< Currently selected index */
    bool use_sd_card;          /**< Load from SD card vs embedded */
    lv_coord_t image_width;    /**< Image display width */
    lv_coord_t image_height;   /**< Image display height */
} lv_image_roller_t;

/**
 * Create an image roller object
 * @param parent pointer to an object, it will be the parent of the new roller
 * @return pointer to the created image roller container
 */
lv_obj_t* lv_image_roller_create(lv_obj_t* parent);

/**
 * Set the options and associated images for the roller
 * @param roller pointer to image roller object
 * @param options text options separated by '\n'
 * @param image_paths array of image paths (for SD card loading)
 * @param embedded_images array of embedded image descriptors (for embedded loading)
 * @param count number of options/images
 * @param use_sd_card true to load from SD card, false for embedded images
 */
void lv_image_roller_set_options(lv_obj_t* roller, 
                                 const char* options,
                                 const char** image_paths,
                                 lv_image_dsc_t** embedded_images,
                                 uint16_t count,
                                 bool use_sd_card);

/**
 * Set the selected option
 * @param roller pointer to image roller object
 * @param idx index of the option to select
 * @param anim LV_ANIM_ON: animate to the new position, LV_ANIM_OFF: no animation
 */
void lv_image_roller_set_selected(lv_obj_t* roller, uint16_t idx, lv_anim_enable_t anim);

/**
 * Get the index of the selected option
 * @param roller pointer to image roller object
 * @return index of the selected option
 */
uint16_t lv_image_roller_get_selected(const lv_obj_t* roller);

/**
 * Get the text of the selected option
 * @param roller pointer to image roller object
 * @param buf buffer to store the selected text
 * @param buf_size size of the buffer
 */
void lv_image_roller_get_selected_str(const lv_obj_t* roller, char* buf, uint32_t buf_size);

/**
 * Set the size of the displayed image
 * @param roller pointer to image roller object
 * @param width image width in pixels
 * @param height image height in pixels
 */
void lv_image_roller_set_image_size(lv_obj_t* roller, lv_coord_t width, lv_coord_t height);

/**
 * Set the number of visible rows in the roller
 * @param roller pointer to image roller object
 * @param row_cnt number of visible rows
 */
void lv_image_roller_set_visible_row_count(lv_obj_t* roller, uint8_t row_cnt);

/**
 * Apply default styling to the image roller
 * @param roller pointer to image roller object
 */
void lv_image_roller_set_default_style(lv_obj_t* roller);

/**
 * Get the internal roller object for additional customization
 * @param roller pointer to image roller object
 * @return pointer to the internal LVGL roller object
 */
lv_obj_t* lv_image_roller_get_roller(lv_obj_t* roller);

/**
 * Get the image display area for additional customization
 * @param roller pointer to image roller object
 * @return pointer to the image display container
 */
lv_obj_t* lv_image_roller_get_image_area(lv_obj_t* roller);

/**
 * Cleanup image roller memory
 * @param roller pointer to image roller object
 */
void lv_image_roller_cleanup(lv_obj_t* roller);

#ifdef __cplusplus
}
#endif