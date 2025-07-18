#include "lv_image_roller.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "ImageRoller";

/** Forward declarations */
static void image_roller_event_cb(lv_event_t* e);
static void update_displayed_image(lv_image_roller_t* img_roller, uint16_t index);
static void animate_image_change(lv_obj_t* old_img, lv_obj_t* new_img);

/**
 * Create an image roller object
 */
lv_obj_t* lv_image_roller_create(lv_obj_t* parent) {
    // Create main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(container, 200, 300);
    
    // Allocate and initialize image roller data
    lv_image_roller_t* img_roller = (lv_image_roller_t*)malloc(sizeof(lv_image_roller_t));
    if (!img_roller) {
        ESP_LOGE(TAG, "Failed to allocate image roller data");
        lv_obj_delete(container);
        return NULL;
    }
    
    memset(img_roller, 0, sizeof(lv_image_roller_t));
    img_roller->container = container;
    img_roller->image_width = 128;
    img_roller->image_height = 128;
    
    // Create image display area
    img_roller->image_area = lv_obj_create(container);
    if (!img_roller->image_area) {
        ESP_LOGE(TAG, "Failed to create image area");
        free(img_roller);
        lv_obj_delete(container);
        return NULL;
    }
    lv_obj_remove_flag(img_roller->image_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(img_roller->image_area, img_roller->image_width + 20, img_roller->image_height + 20);
    lv_obj_align(img_roller->image_area, LV_ALIGN_TOP_MID, 0, 10);
    
    // Create current image object (will be properly created later)
    img_roller->current_image = NULL;
    
    // Create roller
    img_roller->roller = lv_roller_create(container);
    if (!img_roller->roller) {
        ESP_LOGE(TAG, "Failed to create roller");
        free(img_roller);
        lv_obj_delete(container);
        return NULL;
    }
    lv_obj_set_width(img_roller->roller, 180);
    lv_obj_align(img_roller->roller, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // Store image roller data in container user data
    lv_obj_set_user_data(container, img_roller);
    
    // Add event handler to roller
    lv_obj_add_event_cb(img_roller->roller, image_roller_event_cb, LV_EVENT_VALUE_CHANGED, container);
    
    // Apply default styling
    lv_image_roller_set_default_style(container);
    
    ESP_LOGI(TAG, "Image roller created");
    
    return container;
}

/**
 * Set the options and associated images for the roller
 */
void lv_image_roller_set_options(lv_obj_t* roller, 
                                 const char* options,
                                 const char** image_paths,
                                 lv_image_dsc_t** embedded_images,
                                 uint16_t count,
                                 bool use_sd_card) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    // Set roller options
    lv_roller_set_options(img_roller->roller, options, LV_ROLLER_MODE_NORMAL);
    
    // Store image information
    img_roller->image_paths = image_paths;
    img_roller->embedded_images = embedded_images;
    img_roller->image_count = count;
    img_roller->use_sd_card = use_sd_card;
    
    // Display first image
    if (count > 0) {
        update_displayed_image(img_roller, 0);
    }
    
    ESP_LOGI(TAG, "Set %d options with %s images", count, use_sd_card ? "SD card" : "embedded");
}

/**
 * Set the selected option
 */
void lv_image_roller_set_selected(lv_obj_t* roller, uint16_t idx, lv_anim_enable_t anim) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    lv_roller_set_selected(img_roller->roller, idx, anim);
    update_displayed_image(img_roller, idx);
}

/**
 * Get the index of the selected option
 */
uint16_t lv_image_roller_get_selected(const lv_obj_t* roller) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return 0;
    
    return lv_roller_get_selected(img_roller->roller);
}

/**
 * Get the text of the selected option
 */
void lv_image_roller_get_selected_str(const lv_obj_t* roller, char* buf, uint32_t buf_size) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    lv_roller_get_selected_str(img_roller->roller, buf, buf_size);
}

/**
 * Set the size of the displayed image
 */
void lv_image_roller_set_image_size(lv_obj_t* roller, lv_coord_t width, lv_coord_t height) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    img_roller->image_width = width;
    img_roller->image_height = height;
    
    // Resize image area
    lv_obj_set_size(img_roller->image_area, width + 20, height + 20);
    
    // Update current image if any
    if (img_roller->current_index < img_roller->image_count) {
        update_displayed_image(img_roller, img_roller->current_index);
    }
}

/**
 * Set the number of visible rows in the roller
 */
void lv_image_roller_set_visible_row_count(lv_obj_t* roller, uint8_t row_cnt) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    lv_obj_set_height(img_roller->roller, lv_obj_get_style_text_line_space(img_roller->roller, LV_PART_MAIN) * row_cnt + 
                                         lv_obj_get_style_pad_top(img_roller->roller, LV_PART_MAIN) + 
                                         lv_obj_get_style_pad_bottom(img_roller->roller, LV_PART_MAIN));
}

/**
 * Apply default styling to the image roller
 */
void lv_image_roller_set_default_style(lv_obj_t* roller) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (!img_roller) return;
    
    // Container style
    lv_obj_set_style_bg_color(img_roller->container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(img_roller->container, 10, 0);
    lv_obj_set_style_pad_all(img_roller->container, 10, 0);
    lv_obj_set_style_border_width(img_roller->container, 0, 0);
    
    // Image area style
    lv_obj_set_style_bg_color(img_roller->image_area, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(img_roller->image_area, 8, 0);
    lv_obj_set_style_border_width(img_roller->image_area, 0, 0);
    
    // Roller style
    lv_obj_set_style_bg_color(img_roller->roller, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_text_color(img_roller->roller, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_color(img_roller->roller, lv_color_hex(0x00ff00), LV_PART_SELECTED);
    lv_obj_set_style_border_width(img_roller->roller, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(img_roller->roller, 8, LV_PART_MAIN);
}

/**
 * Get the internal roller object
 */
lv_obj_t* lv_image_roller_get_roller(lv_obj_t* roller) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    return img_roller ? img_roller->roller : NULL;
}

/**
 * Get the image display area
 */
lv_obj_t* lv_image_roller_get_image_area(lv_obj_t* roller) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    return img_roller ? img_roller->image_area : NULL;
}

/**
 * Cleanup image roller memory
 */
void lv_image_roller_cleanup(lv_obj_t* roller) {
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
    if (img_roller) {
        // Free the allocated memory
        free(img_roller);
        lv_obj_set_user_data(roller, NULL);
        ESP_LOGI(TAG, "Image roller memory cleaned up");
    }
}

/**
 * Event callback for roller value changes
 */
static void image_roller_event_cb(lv_event_t* e) {
    lv_obj_t* container = (lv_obj_t*)lv_event_get_user_data(e);
    lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(container);
    if (!img_roller) return;
    
    uint16_t selected = lv_roller_get_selected(img_roller->roller);
    
    // Update displayed image with animation
    if (selected != img_roller->current_index) {
        ESP_LOGI(TAG, "Selection changed from %d to %d", img_roller->current_index, selected);
        update_displayed_image(img_roller, selected);
        
        // Send custom event
        lv_obj_send_event(container, LV_EVENT_IMAGE_ROLLER_CHANGED, NULL);
    }
}

/**
 * Update the displayed image
 */
static void update_displayed_image(lv_image_roller_t* img_roller, uint16_t index) {
    if (!img_roller || index >= img_roller->image_count) {
        ESP_LOGE(TAG, "Invalid image roller or index");
        return;
    }
    
    ESP_LOGI(TAG, "Updating image to index %d", index);
    
    // Delete old image first to avoid memory issues
    if (img_roller->current_image) {
        lv_obj_delete(img_roller->current_image);
        img_roller->current_image = NULL;
    }
    
    // Create new image - simple approach without animation
    img_roller->current_image = lv_image_create(img_roller->image_area);
    if (!img_roller->current_image) {
        ESP_LOGE(TAG, "Failed to create image object");
        return;
    }
    
    lv_obj_center(img_roller->current_image);
    
    // Set image source based on type
    if (img_roller->use_sd_card && img_roller->image_paths && img_roller->image_paths[index]) {
        ESP_LOGI(TAG, "Loading SD card image: %s", img_roller->image_paths[index]);
        lv_image_set_src(img_roller->current_image, img_roller->image_paths[index]);
    } else if (!img_roller->use_sd_card && img_roller->embedded_images && img_roller->embedded_images[index]) {
        ESP_LOGI(TAG, "Loading embedded image at index %d", index);
        lv_image_set_src(img_roller->current_image, img_roller->embedded_images[index]);
    } else {
        ESP_LOGW(TAG, "No image source available for index %d", index);
    }
    
    // Set image size
    lv_obj_set_size(img_roller->current_image, img_roller->image_width, img_roller->image_height);
    lv_obj_set_style_opa(img_roller->current_image, LV_OPA_COVER, 0);
    
    img_roller->current_index = index;
    ESP_LOGI(TAG, "Image updated successfully");
}

/**
 * Animate image transition - DISABLED for stability
 */
static void animate_image_change(lv_obj_t* old_img, lv_obj_t* new_img) {
    // Animation disabled to prevent memory corruption
    // Simply make new image visible and delete old one
    ESP_LOGI(TAG, "Animation disabled for stability");
}