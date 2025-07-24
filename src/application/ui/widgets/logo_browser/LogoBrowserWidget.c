#include "LogoBrowser.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
static const char* TAG = "logo_browser";

// Forward declaration for C++ interop
#ifdef __cplusplus
extern "C" {
#endif
int logo_browser_get_total_logos(void);
void logo_browser_get_paged_logos(int pageIndex, int itemsPerPage, char** paths, int* count);
void logo_browser_get_lvgl_path(const char* path, char* lvglPath, size_t maxLen);
void logo_browser_scan_logos(void);
int logo_browser_get_filtered_total_logos(const char* filter);
void logo_browser_get_filtered_paged_logos(const char* filter, int pageIndex, int itemsPerPage, char** paths, int* count);
#ifdef __cplusplus
}
#endif

// Configuration constants
#define MAX_FILENAME_LENGTH 64
#define GRID_COLS 3
#define GRID_ROWS 2
#define LOGO_SIZE 120
#define CONTAINER_PADDING 10
#define TITLE_HEIGHT 40
#define SEARCH_HEIGHT 50
#define NAV_HEIGHT 60
#define GRID_SPACING 10
#define ANIMATION_TIME 200
#define DEBOUNCE_MS 300
#define KEYBOARD_HEIGHT 200

// Color scheme
#define COLOR_BG lv_color_hex(0x1a1a1a)
#define COLOR_CARD lv_color_hex(0x2d2d2d)
#define COLOR_SELECTED lv_palette_main(LV_PALETTE_BLUE)
#define COLOR_HOVER lv_palette_lighten(LV_PALETTE_GREY, 1)
#define COLOR_TEXT lv_color_hex(0xffffff)
#define COLOR_TEXT_SECONDARY lv_color_hex(0xcccccc)

// Browser state enum (using typedef from header)

// Logo item structure
typedef struct {
    lv_obj_t* container;
    lv_obj_t* image;
    lv_obj_t* label;
    lv_obj_t* loading_spinner;
    bool is_loaded;
    bool is_selected;
    char path[MAX_FILENAME_LENGTH];
} logo_item_t;

// Browser structure definition
typedef struct {
    // Core objects
    lv_obj_t* container;
    lv_obj_t* content_panel;
    lv_obj_t* title_panel;
    lv_obj_t* search_panel;
    lv_obj_t* grid_panel;
    lv_obj_t* nav_panel;

    // Title elements
    lv_obj_t* title_label;
    lv_obj_t* status_label;

    // Search elements
    lv_obj_t* search_textarea;
    lv_obj_t* search_icon;
    lv_obj_t* btn_edit;
    lv_obj_t* btn_clear;

    // Grid elements
    logo_item_t logos[LOGOS_PER_PAGE];

    // Navigation elements
    lv_obj_t* btn_prev;
    lv_obj_t* btn_next;
    lv_obj_t* page_label;
    lv_obj_t* loading_bar;

    // Keyboard
    lv_obj_t* keyboard;
    bool keyboard_visible;

    // Data management
    char** current_page_paths;
    int current_page_count;
    uint16_t current_page;
    uint16_t total_pages;
    uint16_t selected_index;
    int total_logos;

    // Search state
    char search_filter[MAX_FILENAME_LENGTH];
    lv_timer_t* search_timer;
    uint32_t last_search_time;

    // Browser state
    browser_state_t state;

    // Styles
    lv_style_t style_container;
    lv_style_t style_card;
    lv_style_t style_selected;
    lv_style_t style_hover;
    lv_style_t style_title;
    lv_style_t style_button;
} logo_browser_data_t;

// Forward declarations
static void init_styles(logo_browser_data_t* browser);
static void create_title_panel(logo_browser_data_t* browser);
static void create_search_panel(logo_browser_data_t* browser);
static void create_grid_panel(logo_browser_data_t* browser);
static void create_nav_panel(logo_browser_data_t* browser);
static void update_page_display(logo_browser_data_t* browser);
static void update_navigation_state(logo_browser_data_t* browser);
static void update_status_label(logo_browser_data_t* browser);
static void set_browser_state(logo_browser_data_t* browser, browser_state_t state);
static void toggle_keyboard(logo_browser_data_t* browser, bool show);
static void apply_search_filter(logo_browser_data_t* browser);
static void allocate_page_paths(logo_browser_data_t* browser);
static void free_page_paths(logo_browser_data_t* browser);
static const char* extract_filename(const char* path);
static void scale_hide_anim_cb(lv_anim_t* a);

// Event handlers
static void btn_prev_clicked(lv_event_t* e);
static void btn_next_clicked(lv_event_t* e);
static void logo_clicked(lv_event_t* e);
static void btn_edit_clicked(lv_event_t* e);
static void btn_clear_clicked(lv_event_t* e);
static void search_text_changed(lv_event_t* e);
static void search_timer_cb(lv_timer_t* timer);
static void keyboard_event_cb(lv_event_t* e);
static void keyboard_cleanup_timer_cb(lv_timer_t* timer);

lv_obj_t* logo_browser_create(lv_obj_t* parent) {
    // Create main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(container, COLOR_BG, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);

    // Allocate browser structure
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_malloc(sizeof(logo_browser_data_t));
    if (!browser) {
        ESP_LOGE(TAG, "Failed to allocate browser structure");
        lv_obj_del(container);
        return NULL;
    }
    memset(browser, 0, sizeof(logo_browser_data_t));

    // Initialize structure
    browser->container = container;
    browser->current_page = 0;
    browser->total_pages = 0;
    browser->selected_index = 0;
    browser->total_logos = 0;
    browser->current_page_paths = NULL;
    browser->current_page_count = 0;
    browser->search_filter[0] = '\0';
    browser->keyboard_visible = false;
    browser->keyboard = NULL;
    browser->search_timer = NULL;
    browser->last_search_time = 0;
    browser->state = BROWSER_STATE_IDLE;

    // Store browser pointer in container user data
    lv_obj_set_user_data(container, browser);

    // Initialize styles
    init_styles(browser);

    // Create content panel for proper layout
    browser->content_panel = lv_obj_create(container);
    lv_obj_set_size(browser->content_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(browser->content_panel, 0, 0);
    lv_obj_clear_flag(browser->content_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->content_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->content_panel, CONTAINER_PADDING, 0);
    lv_obj_set_style_border_width(browser->content_panel, 0, 0);

    // Ensure content panel uses the full container size minus padding
    lv_obj_set_style_width(browser->content_panel, lv_pct(100), 0);
    lv_obj_set_style_height(browser->content_panel, lv_pct(100), 0);

    // Create UI panels
    create_title_panel(browser);
    create_search_panel(browser);
    create_grid_panel(browser);
    create_nav_panel(browser);

    ESP_LOGI(TAG, "Logo browser created successfully");
    return container;
}

static void init_styles(logo_browser_data_t* browser) {
    // Container style
    lv_style_init(&browser->style_container);
    lv_style_set_radius(&browser->style_container, 0);
    lv_style_set_bg_color(&browser->style_container, COLOR_BG);
    lv_style_set_border_width(&browser->style_container, 0);

    // Card style
    lv_style_init(&browser->style_card);
    lv_style_set_radius(&browser->style_card, 8);
    lv_style_set_bg_color(&browser->style_card, COLOR_CARD);
    lv_style_set_border_width(&browser->style_card, 1);
    lv_style_set_border_color(&browser->style_card, lv_palette_darken(LV_PALETTE_GREY, 2));
    lv_style_set_pad_all(&browser->style_card, 8);
    lv_style_set_shadow_width(&browser->style_card, 0);

    // Selected style
    lv_style_init(&browser->style_selected);
    lv_style_set_border_width(&browser->style_selected, 3);
    lv_style_set_border_color(&browser->style_selected, COLOR_SELECTED);
    lv_style_set_bg_color(&browser->style_selected, lv_palette_darken(LV_PALETTE_BLUE, 4));

    // Hover style
    lv_style_init(&browser->style_hover);
    lv_style_set_bg_color(&browser->style_hover, COLOR_HOVER);

    // Title style
    lv_style_init(&browser->style_title);
    lv_style_set_text_color(&browser->style_title, COLOR_TEXT);
    lv_style_set_text_font(&browser->style_title, &lv_font_montserrat_24);

    // Button style
    lv_style_init(&browser->style_button);
    lv_style_set_radius(&browser->style_button, 6);
    lv_style_set_bg_color(&browser->style_button, COLOR_CARD);
    lv_style_set_border_width(&browser->style_button, 1);
    lv_style_set_border_color(&browser->style_button, lv_palette_darken(LV_PALETTE_GREY, 1));
}

static void create_title_panel(logo_browser_data_t* browser) {
    browser->title_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->title_panel, lv_pct(100), TITLE_HEIGHT);
    lv_obj_set_pos(browser->title_panel, 0, 0);
    lv_obj_clear_flag(browser->title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->title_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->title_panel, 0, 0);
    lv_obj_set_style_border_width(browser->title_panel, 0, 0);

    // Title label
    browser->title_label = lv_label_create(browser->title_panel);
    lv_label_set_text(browser->title_label, "Logo Browser");
    lv_obj_add_style(browser->title_label, &browser->style_title, 0);
    lv_obj_align(browser->title_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Status label
    browser->status_label = lv_label_create(browser->title_panel);
    lv_label_set_text(browser->status_label, "Ready");
    lv_obj_set_style_text_color(browser->status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(browser->status_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_search_panel(logo_browser_data_t* browser) {
    browser->search_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->search_panel, lv_pct(100), SEARCH_HEIGHT);
    lv_obj_set_pos(browser->search_panel, 0, TITLE_HEIGHT + 10);
    lv_obj_clear_flag(browser->search_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(browser->search_panel, &browser->style_card, 0);

    // Search icon using LVGL symbol
    browser->search_icon = lv_label_create(browser->search_panel);
    lv_label_set_text(browser->search_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(browser->search_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(browser->search_icon, LV_ALIGN_LEFT_MID, 10, 0);

    // Search textarea
    browser->search_textarea = lv_textarea_create(browser->search_panel);
    lv_obj_set_size(browser->search_textarea, 400, 36);
    lv_obj_align(browser->search_textarea, LV_ALIGN_LEFT_MID, 40, 0);
    lv_textarea_set_placeholder_text(browser->search_textarea, "Search logos...");
    lv_textarea_set_one_line(browser->search_textarea, true);
    lv_obj_set_style_bg_color(browser->search_textarea, lv_color_darken(COLOR_CARD, 50), 0);
    lv_obj_set_style_border_width(browser->search_textarea, 1, 0);
    lv_obj_set_style_border_color(browser->search_textarea, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_add_event_cb(browser->search_textarea, search_text_changed, LV_EVENT_VALUE_CHANGED, browser);

    // Edit button
    browser->btn_edit = lv_button_create(browser->search_panel);
    lv_obj_set_size(browser->btn_edit, 80, 36);
    lv_obj_align(browser->btn_edit, LV_ALIGN_RIGHT_MID, -95, 0);
    lv_obj_add_style(browser->btn_edit, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_edit, btn_edit_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* edit_label = lv_label_create(browser->btn_edit);
    lv_label_set_text(edit_label, "Edit");
    lv_obj_center(edit_label);

    // Clear button
    browser->btn_clear = lv_button_create(browser->search_panel);
    lv_obj_set_size(browser->btn_clear, 80, 36);
    lv_obj_align(browser->btn_clear, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_style(browser->btn_clear, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_clear, btn_clear_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* clear_label = lv_label_create(browser->btn_clear);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);
}

static void create_grid_panel(logo_browser_data_t* browser) {
    // Calculate grid dimensions with proper spacing
    int grid_y = TITLE_HEIGHT + SEARCH_HEIGHT + 20;
    int available_height = lv_obj_get_height(browser->content_panel) - (2 * CONTAINER_PADDING);
    int grid_height = available_height - grid_y - NAV_HEIGHT - 10;

    browser->grid_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->grid_panel, lv_pct(100), grid_height);
    lv_obj_set_pos(browser->grid_panel, 0, grid_y);
    lv_obj_clear_flag(browser->grid_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->grid_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->grid_panel, 0, 0);
    lv_obj_set_style_border_width(browser->grid_panel, 0, 0);

    // Set up grid layout
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(browser->grid_panel, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(browser->grid_panel, GRID_SPACING, 0);
    lv_obj_set_style_pad_column(browser->grid_panel, GRID_SPACING, 0);
    lv_obj_set_style_pad_row(browser->grid_panel, GRID_SPACING, 0);

    // Create logo items
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        logo_item_t* item = &browser->logos[i];

        // Container
        item->container = lv_obj_create(browser->grid_panel);
        lv_obj_set_grid_cell(item->container, LV_GRID_ALIGN_STRETCH, i % GRID_COLS, 1,
                             LV_GRID_ALIGN_STRETCH, i / GRID_COLS, 1);
        lv_obj_add_flag(item->container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item->container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_style(item->container, &browser->style_card, 0);
        lv_obj_add_style(item->container, &browser->style_hover, LV_STATE_PRESSED);
        lv_obj_add_event_cb(item->container, logo_clicked, LV_EVENT_CLICKED, browser);
        lv_obj_set_user_data(item->container, (void*)(intptr_t)i);

        // Loading spinner
        item->loading_spinner = lv_spinner_create(item->container);
        lv_obj_set_size(item->loading_spinner, 40, 40);
        lv_obj_center(item->loading_spinner);
        lv_obj_add_flag(item->loading_spinner, LV_OBJ_FLAG_HIDDEN);

        // Image
        item->image = lv_image_create(item->container);
        lv_obj_set_size(item->image, LOGO_SIZE, LOGO_SIZE);
        lv_obj_align(item->image, LV_ALIGN_TOP_MID, 0, 10);
        lv_image_set_scale(item->image, 256);  // Enable scaling
        lv_image_set_antialias(item->image, true);

        // Label
        item->label = lv_label_create(item->container);
        lv_label_set_text(item->label, "");
        lv_obj_set_style_text_align(item->label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(item->label, COLOR_TEXT, 0);
        lv_label_set_long_mode(item->label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(item->label, lv_pct(90));
        lv_obj_align(item->label, LV_ALIGN_BOTTOM_MID, 0, -10);

        // Initially hidden
        lv_obj_add_flag(item->container, LV_OBJ_FLAG_HIDDEN);
        item->is_loaded = false;
        item->is_selected = false;
        item->path[0] = '\0';
    }
}

static void create_nav_panel(logo_browser_data_t* browser) {
    // Calculate navigation panel position
    int nav_y = lv_obj_get_height(browser->content_panel) - NAV_HEIGHT - CONTAINER_PADDING;

    browser->nav_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->nav_panel, lv_pct(100), NAV_HEIGHT);
    lv_obj_set_pos(browser->nav_panel, 0, nav_y);
    lv_obj_clear_flag(browser->nav_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(browser->nav_panel, &browser->style_card, 0);

    // Previous button
    browser->btn_prev = lv_button_create(browser->nav_panel);
    lv_obj_set_size(browser->btn_prev, 100, 40);
    lv_obj_align(browser->btn_prev, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_style(browser->btn_prev, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_prev, btn_prev_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* prev_label = lv_label_create(browser->btn_prev);
    lv_label_set_text(prev_label, "Previous");
    lv_obj_center(prev_label);

    // Page indicator
    browser->page_label = lv_label_create(browser->nav_panel);
    lv_label_set_text(browser->page_label, "Page 0 of 0");
    lv_obj_set_style_text_color(browser->page_label, COLOR_TEXT, 0);
    lv_obj_align(browser->page_label, LV_ALIGN_CENTER, 0, -5);

    // Loading bar
    browser->loading_bar = lv_bar_create(browser->nav_panel);
    lv_obj_set_size(browser->loading_bar, 200, 4);
    lv_obj_align(browser->loading_bar, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(browser->loading_bar, 0, 100);
    lv_obj_set_style_bg_color(browser->loading_bar, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
    lv_obj_set_style_bg_color(browser->loading_bar, COLOR_SELECTED, LV_PART_INDICATOR);
    lv_obj_add_flag(browser->loading_bar, LV_OBJ_FLAG_HIDDEN);

    // Next button
    browser->btn_next = lv_button_create(browser->nav_panel);
    lv_obj_set_size(browser->btn_next, 100, 40);
    lv_obj_align(browser->btn_next, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_style(browser->btn_next, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_next, btn_next_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* next_label = lv_label_create(browser->btn_next);
    lv_label_set_text(next_label, "Next");
    lv_obj_center(next_label);
}

int logo_browser_scan_directory(lv_obj_t* browser_obj, const char* logo_directory) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) {
        ESP_LOGE(TAG, "Invalid browser object");
        return 0;
    }

    set_browser_state(browser, BROWSER_STATE_LOADING);

    // Scan logos through interop
    logo_browser_scan_logos();

    // Get total count based on filter
    if (strlen(browser->search_filter) > 0) {
        browser->total_logos = logo_browser_get_filtered_total_logos(browser->search_filter);
        ESP_LOGI(TAG, "Found %d logos matching filter '%s'", browser->total_logos, browser->search_filter);
    } else {
        browser->total_logos = logo_browser_get_total_logos();
        ESP_LOGI(TAG, "Found %d logos", browser->total_logos);
    }

    // Calculate pages
    browser->total_pages = (browser->total_logos + LOGOS_PER_PAGE - 1) / LOGOS_PER_PAGE;
    browser->current_page = 0;

    // Allocate paths array for current page
    allocate_page_paths(browser);

    // Update display
    update_page_display(browser);
    update_navigation_state(browser);

    set_browser_state(browser, BROWSER_STATE_IDLE);

    return browser->total_logos;
}

static void update_page_display(logo_browser_data_t* browser) {
    if (!browser) return;

    // Show loading state
    lv_obj_remove_flag(browser->loading_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(browser->loading_bar, 0, LV_ANIM_OFF);

    // Get logos for current page
    if (strlen(browser->search_filter) > 0) {
        logo_browser_get_filtered_paged_logos(browser->search_filter, browser->current_page,
                                              LOGOS_PER_PAGE, browser->current_page_paths,
                                              &browser->current_page_count);
    } else {
        logo_browser_get_paged_logos(browser->current_page, LOGOS_PER_PAGE,
                                     browser->current_page_paths, &browser->current_page_count);
    }

    // Update each logo item
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        logo_item_t* item = &browser->logos[i];

        if (i < browser->current_page_count && browser->current_page_paths[i][0] != '\0') {
            // Update item data
            strncpy(item->path, browser->current_page_paths[i], MAX_FILENAME_LENGTH - 1);
            item->path[MAX_FILENAME_LENGTH - 1] = '\0';

            // Get LVGL path
            char lvgl_path[MAX_FILENAME_LENGTH + 10];
            logo_browser_get_lvgl_path(item->path, lvgl_path, sizeof(lvgl_path));

            // Set image source
            lv_image_set_src(item->image, lvgl_path);

            // Update label
            const char* filename = extract_filename(item->path);
            lv_label_set_text(item->label, filename);

            // Update selection state
            int global_idx = browser->current_page * LOGOS_PER_PAGE + i;
            item->is_selected = (global_idx == browser->selected_index);

            if (item->is_selected) {
                lv_obj_add_style(item->container, &browser->style_selected, 0);
            } else {
                lv_obj_remove_style(item->container, &browser->style_selected, 0);
            }

            // Show item with scale animation
            lv_obj_remove_flag(item->container, LV_OBJ_FLAG_HIDDEN);
            
            // Use scale animation instead of fade
            lv_anim_t scale_anim;
            lv_anim_init(&scale_anim);
            lv_anim_set_var(&scale_anim, item->container);
            lv_anim_set_values(&scale_anim, 200, 256); // Start small, scale to normal
            lv_anim_set_time(&scale_anim, ANIMATION_TIME);
            lv_anim_set_delay(&scale_anim, i * 50);
            lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&scale_anim, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_scale);
            lv_anim_start(&scale_anim);

            item->is_loaded = true;

            // Update loading bar
            lv_bar_set_value(browser->loading_bar, ((i + 1) * 100) / browser->current_page_count,
                             LV_ANIM_ON);
        } else {
            // Hide unused items with scale animation
            if (!lv_obj_has_flag(item->container, LV_OBJ_FLAG_HIDDEN)) {
                // Scale down animation before hiding
                lv_anim_t scale_anim;
                lv_anim_init(&scale_anim);
                lv_anim_set_var(&scale_anim, item->container);
                lv_anim_set_values(&scale_anim, 256, 200); // Scale down
                lv_anim_set_time(&scale_anim, ANIMATION_TIME);
                lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_in);
                lv_anim_set_exec_cb(&scale_anim, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_scale);
                lv_anim_set_deleted_cb(&scale_anim, scale_hide_anim_cb);
                lv_anim_start(&scale_anim);
            }
            item->is_loaded = false;
            item->path[0] = '\0';
        }
    }

    // Update page indicator
    char page_text[64];
    if (browser->total_pages > 0) {
        snprintf(page_text, sizeof(page_text), "Page %d of %d • %d logos",
                 browser->current_page + 1, browser->total_pages, browser->total_logos);
    } else {
        snprintf(page_text, sizeof(page_text), "No logos found");
    }
    lv_label_set_text(browser->page_label, page_text);

    // Hide loading bar after animation
    lv_obj_add_flag(browser->loading_bar, LV_OBJ_FLAG_HIDDEN);

    update_status_label(browser);
}

static void update_navigation_state(logo_browser_data_t* browser) {
    if (!browser) return;

    // Update previous button
    if (browser->current_page == 0) {
        lv_obj_add_state(browser->btn_prev, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(browser->btn_prev, lv_color_darken(COLOR_CARD, 100), LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(browser->btn_prev, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(browser->btn_prev, COLOR_CARD, 0);
    }

    // Update next button
    if (browser->current_page >= browser->total_pages - 1 || browser->total_pages == 0) {
        lv_obj_add_state(browser->btn_next, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(browser->btn_next, lv_color_darken(COLOR_CARD, 100), LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(browser->btn_next, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(browser->btn_next, COLOR_CARD, 0);
    }
}

static void update_status_label(logo_browser_data_t* browser) {
    if (!browser || !browser->status_label) return;

    char status[64];

    switch (browser->state) {
        case BROWSER_STATE_LOADING:
            snprintf(status, sizeof(status), "Loading...");
            break;
        case BROWSER_STATE_SEARCHING:
            snprintf(status, sizeof(status), "Searching...");
            break;
        case BROWSER_STATE_ERROR:
            snprintf(status, sizeof(status), "Error");
            break;
        default:
            if (strlen(browser->search_filter) > 0) {
                snprintf(status, sizeof(status), "Filter: \"%s\"", browser->search_filter);
            } else {
                snprintf(status, sizeof(status), "Ready");
            }
            break;
    }

    lv_label_set_text(browser->status_label, status);
}

static void set_browser_state(logo_browser_data_t* browser, browser_state_t state) {
    browser->state = state;
    update_status_label(browser);
}

static void toggle_keyboard(logo_browser_data_t* browser, bool show) {
    if (!browser) return;

    if (show && !browser->keyboard_visible) {
        // Create keyboard on the main container, not screen
        browser->keyboard = lv_keyboard_create(browser->container);
        if (!browser->keyboard) {
            ESP_LOGE(TAG, "Failed to create keyboard");
            return;
        }

        lv_obj_set_size(browser->keyboard, lv_pct(100), KEYBOARD_HEIGHT);
        lv_obj_align(browser->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);

        // Set keyboard properties safely
        lv_keyboard_set_textarea(browser->keyboard, browser->search_textarea);
        lv_keyboard_set_mode(browser->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

        // Add event handlers with proper null checks
        lv_obj_add_event_cb(browser->keyboard, keyboard_event_cb, LV_EVENT_READY, browser);
        lv_obj_add_event_cb(browser->keyboard, keyboard_event_cb, LV_EVENT_CANCEL, browser);

        // Show keyboard with scale animation instead of fade
        lv_anim_t scale_anim;
        lv_anim_init(&scale_anim);
        lv_anim_set_var(&scale_anim, browser->keyboard);
        lv_anim_set_values(&scale_anim, 200, 256); // Start small, scale to normal
        lv_anim_set_time(&scale_anim, ANIMATION_TIME);
        lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&scale_anim, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_scale);
        lv_anim_start(&scale_anim);

        browser->keyboard_visible = true;

        // Update button text
        lv_obj_t* label = lv_obj_get_child(browser->btn_edit, 0);
        if (label) {
            lv_label_set_text(label, "Done");
        }

        // Focus textarea
        lv_obj_add_state(browser->search_textarea, LV_STATE_FOCUSED);

    } else if (!show && browser->keyboard_visible) {
        // Hide keyboard with scale animation
        if (browser->keyboard) {
            // Use scale down animation instead of fade
            lv_anim_t scale_anim;
            lv_anim_init(&scale_anim);
            lv_anim_set_var(&scale_anim, browser->keyboard);
            lv_anim_set_values(&scale_anim, 256, 200); // Scale down
            lv_anim_set_time(&scale_anim, ANIMATION_TIME);
            lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_in);
            lv_anim_set_exec_cb(&scale_anim, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_scale);

            lv_anim_start(&scale_anim);
            
            // Create a timer to delete keyboard after animation
            lv_timer_t* cleanup_timer = lv_timer_create(keyboard_cleanup_timer_cb, ANIMATION_TIME + 50, browser->keyboard);
            lv_timer_set_repeat_count(cleanup_timer, 1);

            browser->keyboard = NULL;
        }

        browser->keyboard_visible = false;

        // Update button text
        lv_obj_t* label = lv_obj_get_child(browser->btn_edit, 0);
        if (label) {
            lv_label_set_text(label, "Edit");
        }

        // Remove focus
        lv_obj_remove_state(browser->search_textarea, LV_STATE_FOCUSED);
    }
}

static void apply_search_filter(logo_browser_data_t* browser) {
    if (!browser) return;

    set_browser_state(browser, BROWSER_STATE_SEARCHING);

    // Rescan with filter
    if (strlen(browser->search_filter) > 0) {
        browser->total_logos = logo_browser_get_filtered_total_logos(browser->search_filter);
        ESP_LOGI(TAG, "Found %d logos matching filter '%s'",
                 browser->total_logos, browser->search_filter);
    } else {
        browser->total_logos = logo_browser_get_total_logos();
        ESP_LOGI(TAG, "Found %d total logos", browser->total_logos);
    }

    // Recalculate pages
    browser->total_pages = (browser->total_logos + LOGOS_PER_PAGE - 1) / LOGOS_PER_PAGE;
    browser->current_page = 0;
    browser->selected_index = 0;

    // Update display
    update_page_display(browser);
    update_navigation_state(browser);

    set_browser_state(browser, BROWSER_STATE_IDLE);
}

// Event handlers
static void btn_prev_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser || browser->current_page == 0) return;

    browser->current_page--;
    update_page_display(browser);
    update_navigation_state(browser);
}

static void btn_next_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser || browser->current_page >= browser->total_pages - 1) return;

    browser->current_page++;
    update_page_display(browser);
    update_navigation_state(browser);
}

static void logo_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    lv_obj_t* container = lv_event_get_target(e);
    if (!browser || !container) return;

    int index = (int)(intptr_t)lv_obj_get_user_data(container);
    int global_idx = browser->current_page * LOGOS_PER_PAGE + index;

    // Update selection
    browser->selected_index = global_idx;

    // Update visual selection
    for (int i = 0; i < LOGOS_PER_PAGE; i++) {
        logo_item_t* item = &browser->logos[i];
        if (i == index) {
            item->is_selected = true;
            lv_obj_add_style(item->container, &browser->style_selected, 0);
        } else {
            item->is_selected = false;
            lv_obj_remove_style(item->container, &browser->style_selected, 0);
        }
    }

    ESP_LOGI(TAG, "Selected logo: %s (index %d)", browser->logos[index].path, global_idx);
}

static void btn_edit_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser || !browser->search_textarea) return;

    // Additional safety check
    if (!lv_obj_is_valid(browser->search_textarea)) {
        ESP_LOGE(TAG, "Search textarea is invalid");
        return;
    }

    toggle_keyboard(browser, !browser->keyboard_visible);
}

static void btn_clear_clicked(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    lv_textarea_set_text(browser->search_textarea, "");
    browser->search_filter[0] = '\0';
    apply_search_filter(browser);

    ESP_LOGI(TAG, "Search cleared");
}

static void search_text_changed(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    // Cancel existing timer
    if (browser->search_timer) {
        lv_timer_del(browser->search_timer);
        browser->search_timer = NULL;
    }

    // Get current time
    uint32_t current_time = lv_tick_get();

    // Check if enough time has passed for debouncing
    if ((current_time - browser->last_search_time) < DEBOUNCE_MS) {
        // Create timer for delayed search
        browser->search_timer = lv_timer_create(search_timer_cb, DEBOUNCE_MS, browser);
        lv_timer_set_repeat_count(browser->search_timer, 1);
    } else {
        // Perform search immediately
        const char* text = lv_textarea_get_text(browser->search_textarea);
        if (strcmp(browser->search_filter, text) != 0) {
            strncpy(browser->search_filter, text, MAX_FILENAME_LENGTH - 1);
            browser->search_filter[MAX_FILENAME_LENGTH - 1] = '\0';
            apply_search_filter(browser);
        }
    }

    browser->last_search_time = current_time;
}

static void search_timer_cb(lv_timer_t* timer) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_timer_get_user_data(timer);
    if (!browser) return;

    const char* text = lv_textarea_get_text(browser->search_textarea);
    if (strcmp(browser->search_filter, text) != 0) {
        strncpy(browser->search_filter, text, MAX_FILENAME_LENGTH - 1);
        browser->search_filter[MAX_FILENAME_LENGTH - 1] = '\0';
        apply_search_filter(browser);
    }

    browser->search_timer = NULL;
}

static void keyboard_event_cb(lv_event_t* e) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    lv_event_code_t code = lv_event_get_code(e);

    // Handle both ready (enter) and cancel events
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Safely hide keyboard
        toggle_keyboard(browser, false);
    }
}

static void keyboard_cleanup_timer_cb(lv_timer_t* timer) {
    lv_obj_t* keyboard = (lv_obj_t*)lv_timer_get_user_data(timer);
    if (keyboard && lv_obj_is_valid(keyboard)) {
        lv_obj_del(keyboard);
    }
    lv_timer_del(timer);
}

static void scale_hide_anim_cb(lv_anim_t* a) {
    lv_obj_t* obj = (lv_obj_t*)a->var;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// Helper functions
static void allocate_page_paths(logo_browser_data_t* browser) {
    if (!browser) return;

    free_page_paths(browser);

    browser->current_page_paths = (char**)lv_malloc(LOGOS_PER_PAGE * sizeof(char*));
    if (!browser->current_page_paths) return;

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

static const char* extract_filename(const char* path) {
    if (!path) return "";

    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        return last_slash + 1;
    }
    return path;
}

// Public API functions
void logo_browser_next_page(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page < browser->total_pages - 1) {
        browser->current_page++;
        update_page_display(browser);
        update_navigation_state(browser);
    }
}

void logo_browser_prev_page(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page > 0) {
        browser->current_page--;
        update_page_display(browser);
        update_navigation_state(browser);
    }
}

const char* logo_browser_get_selected_logo(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return NULL;

    int page_index = browser->selected_index % LOGOS_PER_PAGE;
    if (page_index < LOGOS_PER_PAGE && browser->logos[page_index].is_loaded) {
        return browser->logos[page_index].path;
    }
    return NULL;
}

void logo_browser_set_selected_logo(lv_obj_t* browser_obj, uint16_t logo_index) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (logo_index < browser->total_logos) {
        browser->selected_index = logo_index;
        browser->current_page = logo_index / LOGOS_PER_PAGE;
        update_page_display(browser);
        update_navigation_state(browser);
    }
}

void logo_browser_cleanup(lv_obj_t* browser_obj) {
    logo_browser_data_t* browser = (logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    // Clean up keyboard safely
    if (browser->keyboard && lv_obj_is_valid(browser->keyboard)) {
        lv_obj_del(browser->keyboard);
        browser->keyboard = NULL;
    }
    browser->keyboard_visible = false;

    // Clean up search timer
    if (browser->search_timer) {
        lv_timer_del(browser->search_timer);
        browser->search_timer = NULL;
    }

    // Free paths
    free_page_paths(browser);

    // Clean up styles
    lv_style_reset(&browser->style_container);
    lv_style_reset(&browser->style_card);
    lv_style_reset(&browser->style_selected);
    lv_style_reset(&browser->style_hover);
    lv_style_reset(&browser->style_title);
    lv_style_reset(&browser->style_button);

    // Free browser structure
    lv_free(browser);

    ESP_LOGI(TAG, "Logo browser cleaned up");
}
