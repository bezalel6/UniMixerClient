#include "LogoBrowser.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
static const char* TAG = "logo_browser";
#include <Arduino.h>
// Forward declaration for C++ interop
#ifdef __cplusplus
extern "C" {
#endif
int logo_browser_get_total_logos(void);
void logo_browser_get_paged_logos(int pageIndex, int itemsPerPage, char** paths, int* count);
void logo_browser_get_lvgl_path(const char* path, char* lvglPath, size_t maxLen);
void logo_browser_scan_logos(void);
#ifdef __cplusplus
}
#endif

// Browser structure definition
typedef struct {
    lv_obj_t* container;
    lv_obj_t* grid_container;
    lv_obj_t* nav_container;
    lv_obj_t* btn_prev;
    lv_obj_t* btn_next;
    lv_obj_t* page_label;
    lv_obj_t* logo_images[LOGOS_PER_PAGE];
    lv_obj_t* logo_containers[LOGOS_PER_PAGE];  // Store container references
    lv_obj_t* logo_labels[LOGOS_PER_PAGE];      // Store label references
    char** current_page_paths;                  // Array of paths for current page
    int current_page_count;                     // Number of logos on current page
    uint16_t current_page;
    uint16_t total_pages;
    uint16_t selected_logo;
    int total_logos;
} logo_browser_data_t;

// Event handlers
static void btn_prev_clicked(lv_event_t* e);
static void btn_next_clicked(lv_event_t* e);
static void logo_clicked(lv_event_t* e);

// Internal functions
static void update_page_display(logo_browser_data_t* browser);
static void update_navigation_buttons(logo_browser_data_t* browser);
static const char* extract_filename(const char* path);
static void free_page_paths(logo_browser_data_t* browser);
static void allocate_page_paths(logo_browser_data_t* browser);

lv_obj_t* logo_browser_create(lv_obj_t* parent) {
    // Create main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(90), lv_pct(85));
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Allocate browser structure
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_malloc(sizeof(logo_browser_data_t));
    if (!browser) {
        ESP_LOGE(TAG, "Failed to allocate browser structure");
        return NULL;
    }
    memset(browser, 0, sizeof(logo_browser_data_t));

    // Initialize structure
    browser->container = container;
    browser->current_page = 0;
    browser->total_pages = 0;
    browser->selected_logo = 0;
    browser->total_logos = 0;
    browser->current_page_paths = NULL;
    browser->current_page_count = 0;

    // Store browser pointer in container user data
    lv_obj_set_user_data(container, browser);

    // Create grid container for logos
    browser->grid_container = lv_obj_create(container);
    lv_obj_set_size(browser->grid_container, lv_pct(100), lv_pct(80));
    lv_obj_set_style_pad_all(browser->grid_container, 10, 0);

    // Set up 3x2 grid
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(browser->grid_container, col_dsc, row_dsc);

    // Create logo slots
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        // Container for each logo + label
        lv_obj_t* logo_cont = lv_obj_create(browser->grid_container);
        browser->logo_containers[i] = logo_cont;
        lv_obj_set_flex_flow(logo_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(logo_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_grid_cell(logo_cont, LV_GRID_ALIGN_CENTER, i % 3, 1, LV_GRID_ALIGN_CENTER, i / 3, 1);
        lv_obj_add_flag(logo_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(logo_cont, logo_clicked, LV_EVENT_CLICKED, browser);
        lv_obj_set_style_pad_all(logo_cont, 5, 0);

        // Store index in user data for click handling
        lv_obj_set_user_data(logo_cont, (void*)(intptr_t)i);

        // Image object
        browser->logo_images[i] = lv_image_create(logo_cont);
        lv_obj_set_size(browser->logo_images[i], 150, 150);

        // Label for filename
        browser->logo_labels[i] = lv_label_create(logo_cont);
        lv_label_set_text(browser->logo_labels[i], "");
        lv_obj_set_style_text_align(browser->logo_labels[i], LV_TEXT_ALIGN_CENTER, 0);

        // Hide initially
        lv_obj_add_flag(logo_cont, LV_OBJ_FLAG_HIDDEN);
    }

    // Create navigation container
    browser->nav_container = lv_obj_create(container);
    lv_obj_set_size(browser->nav_container, lv_pct(100), 50);
    lv_obj_set_flex_flow(browser->nav_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(browser->nav_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(browser->nav_container, 20, 0);

    // Previous button
    browser->btn_prev = lv_button_create(browser->nav_container);
    lv_obj_set_size(browser->btn_prev, 100, 40);
    lv_obj_add_event_cb(browser->btn_prev, btn_prev_clicked, LV_EVENT_CLICKED, browser);
    lv_obj_t* prev_label = lv_label_create(browser->btn_prev);
    lv_label_set_text(prev_label, "Previous");
    lv_obj_center(prev_label);

    // Page indicator
    browser->page_label = lv_label_create(browser->nav_container);
    lv_label_set_text(browser->page_label, "Page 0 of 0");

    // Next button
    browser->btn_next = lv_button_create(browser->nav_container);
    lv_obj_set_size(browser->btn_next, 100, 40);
    lv_obj_add_event_cb(browser->btn_next, btn_next_clicked, LV_EVENT_CLICKED, browser);
    lv_obj_t* next_label = lv_label_create(browser->btn_next);
    lv_label_set_text(next_label, "Next");
    lv_obj_center(next_label);

    ESP_LOGI(TAG, "Logo browser created");
    return container;
}

int logo_browser_scan_directory(lv_obj_t* browser_obj, const char* logo_directory) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) {
        ESP_LOGE(TAG, "Invalid browser object");
        return 0;
    }

    // Scan logos through interop
    logo_browser_scan_logos();

    // Get total count
    browser->total_logos = logo_browser_get_total_logos();
    ESP_LOGI(TAG, "Found %d logos", browser->total_logos);

    // Calculate pages
    browser->total_pages = (browser->total_logos + LOGOS_PER_PAGE - 1) / LOGOS_PER_PAGE;
    browser->current_page = 0;

    // Allocate paths array for current page
    allocate_page_paths(browser);

    // Update display
    update_page_display(browser);
    update_navigation_buttons(browser);

    return browser->total_logos;
}

void logo_browser_next_page(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page < browser->total_pages - 1) {
        browser->current_page++;
        update_page_display(browser);
        update_navigation_buttons(browser);
    }
}

void logo_browser_prev_page(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page > 0) {
        browser->current_page--;
        update_page_display(browser);
        update_navigation_buttons(browser);
    }
}

const char* logo_browser_get_selected_logo(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return NULL;

    // Calculate which logo is selected on current page
    int page_index = browser->selected_logo % LOGOS_PER_PAGE;
    if (page_index < browser->current_page_count && browser->current_page_paths) {
        return browser->current_page_paths[page_index];
    }
    return NULL;
}

void logo_browser_set_selected_logo(lv_obj_t* browser_obj, uint16_t logo_index) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (logo_index < browser->total_logos) {
        browser->selected_logo = logo_index;
        // Navigate to the page containing this logo
        browser->current_page = logo_index / LOGOS_PER_PAGE;
        update_page_display(browser);
        update_navigation_buttons(browser);
    }
}

void logo_browser_cleanup(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    free_page_paths(browser);
    lv_free(browser);
    ESP_LOGI(TAG, "Logo browser cleaned up");
}
#define MAX_FILENAME_LENGTH 64

// Static helper functions
static void allocate_page_paths(logo_browser_data_t* browser) {
    if (!browser) return;

    // Free existing paths
    free_page_paths(browser);

    // Allocate array for paths
    browser->current_page_paths = (char**)lv_malloc(LOGOS_PER_PAGE * sizeof(char*));
    if (!browser->current_page_paths) return;

    // Allocate each path string
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        browser->current_page_paths[i] = (char*)lv_malloc(MAX_FILENAME_LENGTH);
        if (browser->current_page_paths[i]) {
            browser->current_page_paths[i][0] = '\0';
        }
    }
}

static void free_page_paths(logo_browser_data_t* browser) {
    if (!browser || !browser->current_page_paths) return;

    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        if (browser->current_page_paths[i]) {
            lv_free(browser->current_page_paths[i]);
        }
    }
    lv_free(browser->current_page_paths);
    browser->current_page_paths = NULL;
}

static void update_page_display(logo_browser_data_t* browser) {
    if (!browser) return;

    // Get logos for current page through interop
    logo_browser_get_paged_logos(browser->current_page, LOGOS_PER_PAGE,
                                 browser->current_page_paths, &browser->current_page_count);

    // Update each slot
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        lv_obj_t* logo_cont = browser->logo_containers[i];
        lv_obj_t* image = browser->logo_images[i];
        lv_obj_t* label = browser->logo_labels[i];

        if (i < browser->current_page_count && browser->current_page_paths[i][0] != '\0') {
            // Get LVGL path
            char lvgl_path[MAX_FILENAME_LENGTH + 10];

            logo_browser_get_lvgl_path(browser->current_page_paths[i], lvgl_path, sizeof(lvgl_path));

            lv_image_set_src(image, lvgl_path);

            // Extract and set filename
            const char* filename = extract_filename(browser->current_page_paths[i]);
            lv_label_set_text(label, filename);

            // Show container
            lv_obj_remove_flag(logo_cont, LV_OBJ_FLAG_HIDDEN);

            // Highlight if selected
            int globalIdx = browser->current_page * LOGOS_PER_PAGE + i;
            if (globalIdx == browser->selected_logo) {
                lv_obj_set_style_border_width(logo_cont, 3, 0);
                lv_obj_set_style_border_color(logo_cont, lv_palette_main(LV_PALETTE_BLUE), 0);
            } else {
                lv_obj_set_style_border_width(logo_cont, 1, 0);
                lv_obj_set_style_border_color(logo_cont, lv_palette_main(LV_PALETTE_GREY), 0);
            }
        } else {
            // Hide unused slots
            lv_obj_add_flag(logo_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update page indicator
    char page_text[32];
    snprintf(page_text, sizeof(page_text), "Page %d of %d",
             browser->current_page + 1, browser->total_pages);
    lv_label_set_text(browser->page_label, page_text);
}

static void update_navigation_buttons(logo_browser_data_t* browser) {
    if (!browser) return;

    // Enable/disable buttons based on current page
    if (browser->current_page == 0) {
        lv_obj_add_state(browser->btn_prev, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(browser->btn_prev, LV_STATE_DISABLED);
    }

    if (browser->current_page >= browser->total_pages - 1) {
        lv_obj_add_state(browser->btn_next, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(browser->btn_next, LV_STATE_DISABLED);
    }
}

static const char* extract_filename(const char* path) {
    if (!path) return "";

    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        return last_slash + 1;
    }
    return path;
}

// Event handlers
static void btn_prev_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    logo_browser_prev_page(browser->container);
}

static void btn_next_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    logo_browser_next_page(browser->container);
}

static void logo_clicked(lv_event_t* e) {
    lv_obj_t* logo_cont = lv_event_get_target(e);
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    // Get index from container user data
    int index = (int)(intptr_t)lv_obj_get_user_data(logo_cont);
    browser->selected_logo = browser->current_page * LOGOS_PER_PAGE + index;
    update_page_display(browser);
    ESP_LOGI(TAG, "Selected logo index: %d", browser->selected_logo);
}
