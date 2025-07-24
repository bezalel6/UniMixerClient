#include "LogoBrowserEnhanced.h"
#include "LogoBrowser.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* TAG = "enhanced_logo_browser";

// Configuration constants
#define CONTAINER_PADDING 15
#define TITLE_HEIGHT 50
#define TOOLBAR_HEIGHT 60
#define SEARCH_HEIGHT 55
#define NAV_HEIGHT 70
#define GRID_SPACING 12
#define ANIMATION_TIME 300
#define DEBOUNCE_MS 250
#define KEYBOARD_HEIGHT 200
#define PREVIEW_OVERLAY_SIZE 400
#define BACKGROUND_TASK_QUEUE_SIZE 10
#define CORE_1_STACK_SIZE 8192
#define GESTURE_MIN_VELOCITY 100

// Enhanced color scheme
#define COLOR_BG lv_color_hex(0x1a1a1a)
#define COLOR_CARD lv_color_hex(0x2d2d2d)
#define COLOR_SELECTED lv_palette_main(LV_PALETTE_BLUE)
#define COLOR_MULTI_SELECTED lv_palette_main(LV_PALETTE_GREEN)
#define COLOR_HOVER lv_palette_lighten(LV_PALETTE_GREY, 1)
#define COLOR_TEXT lv_color_hex(0xffffff)
#define COLOR_TEXT_SECONDARY lv_color_hex(0xcccccc)
#define COLOR_ACCENT lv_palette_main(LV_PALETTE_ORANGE)
#define COLOR_SUCCESS lv_palette_main(LV_PALETTE_GREEN)
#define COLOR_WARNING lv_palette_main(LV_PALETTE_YELLOW)

// Enhanced logo item structure
typedef struct {
    lv_obj_t* container;
    lv_obj_t* image;
    lv_obj_t* label;
    lv_obj_t* size_label;
    lv_obj_t* selection_indicator;
    lv_obj_t* loading_spinner;
    lv_obj_t* progress_ring;
    logo_metadata_t metadata;
    bool is_loading;
    bool is_visible;
    float load_progress;
} enhanced_logo_item_t;

// Gesture tracking
typedef struct {
    bool active;
    lv_point_t start_point;
    lv_point_t current_point;
    uint32_t start_time;
    int32_t velocity_x;
    int32_t velocity_y;
} gesture_tracker_t;

// Smart cache with LRU eviction
typedef struct cache_entry {
    char path[64];
    lv_img_dsc_t* thumbnail;
    uint32_t last_access;
    struct cache_entry* next;
    struct cache_entry* prev;
} cache_entry_t;

typedef struct {
    cache_entry_t* head;
    cache_entry_t* tail;
    int count;
    int max_size;
} lru_cache_t;

// Background task manager
typedef struct {
    TaskHandle_t task_handle;
    QueueHandle_t task_queue;
    SemaphoreHandle_t cache_mutex;
    bool running;
    lv_obj_t* browser_ref;
} background_manager_t;

// Main enhanced browser structure
typedef struct {
    // Core objects
    lv_obj_t* container;
    lv_obj_t* content_panel;
    lv_obj_t* title_panel;
    lv_obj_t* toolbar_panel;
    lv_obj_t* search_panel;
    lv_obj_t* grid_panel;
    lv_obj_t* nav_panel;
    lv_obj_t* preview_overlay;

    // Title elements
    lv_obj_t* title_label;
    lv_obj_t* status_label;
    lv_obj_t* stats_label;

    // Toolbar elements
    lv_obj_t* btn_view_mode;
    lv_obj_t* btn_sort;
    lv_obj_t* btn_multi_select;
    lv_obj_t* btn_refresh;
    lv_obj_t* progress_arc;

    // Search elements
    lv_obj_t* search_textarea;
    lv_obj_t* search_icon;
    lv_obj_t* btn_edit;
    lv_obj_t* btn_clear;
    lv_obj_t* filter_chips;

    // Grid elements
    enhanced_logo_item_t logos[ENHANCED_LOGOS_PER_PAGE];

    // Navigation elements
    lv_obj_t* btn_prev;
    lv_obj_t* btn_next;
    lv_obj_t* page_label;
    lv_obj_t* page_slider;
    lv_obj_t* loading_bar;

    // Enhanced features
    lv_obj_t* keyboard;
    gesture_tracker_t gesture;
    background_manager_t bg_manager;
    lru_cache_t thumbnail_cache;

    // State management
    enhanced_browser_state_t state;
    view_mode_t view_mode;
    sort_mode_t sort_mode;
    bool multi_select_enabled;
    bool keyboard_visible;

    // Data management
    char** current_page_paths;
    logo_metadata_t* current_metadata;
    int current_page_count;
    uint16_t current_page;
    uint16_t total_pages;
    int total_logos;

    // Selection management
    bool selected_logos[MAX_SELECTED_LOGOS];
    int selected_count;
    int primary_selection;

    // Search and filtering
    char search_filter[64];
    lv_timer_t* search_timer;
    uint32_t last_search_time;

    // Callbacks
    void (*progress_callback)(int progress, void* user_data);
    void* progress_user_data;
    void (*selection_callback)(const char** selected, int count, void* user_data);
    void* selection_user_data;

    // Performance monitoring
    uint32_t last_render_time;
    uint32_t frame_count;
    float avg_fps;

    // Styles
    lv_style_t style_container;
    lv_style_t style_card;
    lv_style_t style_selected;
    lv_style_t style_multi_selected;
    lv_style_t style_hover;
    lv_style_t style_title;
    lv_style_t style_button;
    lv_style_t style_toolbar;
    lv_style_t style_preview;
} enhanced_logo_browser_data_t;

// Forward declarations
static void init_enhanced_styles(enhanced_logo_browser_data_t* browser);
static void create_title_panel(enhanced_logo_browser_data_t* browser);
static void create_toolbar_panel(enhanced_logo_browser_data_t* browser);
static void create_search_panel(enhanced_logo_browser_data_t* browser);
static void create_grid_panel(enhanced_logo_browser_data_t* browser);
static void create_nav_panel(enhanced_logo_browser_data_t* browser);
static void create_preview_overlay(enhanced_logo_browser_data_t* browser);

// Background processing
static void background_task(void* parameters);
static void init_thumbnail_cache(lru_cache_t* cache);
static void cleanup_thumbnail_cache(lru_cache_t* cache);
static lv_img_dsc_t* get_cached_thumbnail(lru_cache_t* cache, const char* path);
static void cache_thumbnail(lru_cache_t* cache, const char* path, lv_img_dsc_t* thumbnail);

// Missing function implementations
static void apply_view_mode(enhanced_logo_browser_data_t* browser);
static void view_mode_clicked(lv_event_t* e);
static void sort_mode_clicked(lv_event_t* e);
static void multi_select_clicked(lv_event_t* e);
static void refresh_clicked(lv_event_t* e);
static void gesture_handler(lv_event_t* e);

// Event handlers
static void enhanced_logo_clicked(lv_event_t* e);
static void gesture_handler(lv_event_t* e);
static void view_mode_clicked(lv_event_t* e);
static void sort_mode_clicked(lv_event_t* e);
static void multi_select_clicked(lv_event_t* e);
static void refresh_clicked(lv_event_t* e);

// Utility functions
static void update_enhanced_display(enhanced_logo_browser_data_t* browser);
static void update_stats_display(enhanced_logo_browser_data_t* browser);
static void animate_page_transition(enhanced_logo_browser_data_t* browser, bool forward);
static void apply_view_mode(enhanced_logo_browser_data_t* browser);
static void update_selection_indicators(enhanced_logo_browser_data_t* browser);

lv_obj_t* enhanced_logo_browser_create(lv_obj_t* parent) {
    // Create main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(container, COLOR_BG, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);

    // Allocate enhanced browser structure
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_malloc(sizeof(enhanced_logo_browser_data_t));
    if (!browser) {
        ESP_LOGE(TAG, "Failed to allocate enhanced browser structure");
        lv_obj_del(container);
        return NULL;
    }
    memset(browser, 0, sizeof(enhanced_logo_browser_data_t));

    // Initialize structure
    browser->container = container;
    browser->state = ENHANCED_BROWSER_STATE_IDLE;
    browser->view_mode = VIEW_MODE_GRID;
    browser->sort_mode = SORT_NAME_ASC;
    browser->multi_select_enabled = false;
    browser->keyboard_visible = false;
    browser->current_page = 0;
    browser->total_pages = 0;
    browser->total_logos = 0;
    browser->selected_count = 0;
    browser->primary_selection = -1;
    browser->search_filter[0] = '\0';
    browser->last_render_time = lv_tick_get();
    browser->frame_count = 0;
    browser->avg_fps = 0.0f;

    // Initialize gesture tracking
    browser->gesture.active = false;

    // Store browser pointer
    lv_obj_set_user_data(container, browser);

    // Initialize styles
    init_enhanced_styles(browser);

    // Initialize thumbnail cache
    init_thumbnail_cache(&browser->thumbnail_cache);

    // Create content panel
    browser->content_panel = lv_obj_create(container);
    lv_obj_set_size(browser->content_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(browser->content_panel, 0, 0);
    lv_obj_clear_flag(browser->content_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->content_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->content_panel, CONTAINER_PADDING, 0);
    lv_obj_set_style_border_width(browser->content_panel, 0, 0);

    // Create UI panels
    create_title_panel(browser);
    create_toolbar_panel(browser);
    create_search_panel(browser);
    create_grid_panel(browser);
    create_nav_panel(browser);
    create_preview_overlay(browser);

    // Add gesture handling
    lv_obj_add_event_cb(browser->grid_panel, gesture_handler, LV_EVENT_GESTURE, browser);
    lv_obj_add_flag(browser->grid_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);

    ESP_LOGI(TAG, "Enhanced logo browser created successfully");
    return container;
}

static void init_enhanced_styles(enhanced_logo_browser_data_t* browser) {
    // Container style
    lv_style_init(&browser->style_container);
    lv_style_set_radius(&browser->style_container, 0);
    lv_style_set_bg_color(&browser->style_container, COLOR_BG);
    lv_style_set_border_width(&browser->style_container, 0);

    // Enhanced card style without opacity
    lv_style_init(&browser->style_card);
    lv_style_set_radius(&browser->style_card, 12);
    lv_style_set_bg_color(&browser->style_card, COLOR_CARD);
    lv_style_set_border_width(&browser->style_card, 1);
    lv_style_set_border_color(&browser->style_card, lv_palette_darken(LV_PALETTE_GREY, 3));
    lv_style_set_pad_all(&browser->style_card, 10);
    lv_style_set_shadow_width(&browser->style_card, 8);
    lv_style_set_shadow_color(&browser->style_card, lv_color_black());
    lv_style_set_transform_scale(&browser->style_card, 256);

    // Selected style with animation
    lv_style_init(&browser->style_selected);
    lv_style_set_border_width(&browser->style_selected, 3);
    lv_style_set_border_color(&browser->style_selected, COLOR_SELECTED);
    lv_style_set_bg_color(&browser->style_selected, lv_palette_darken(LV_PALETTE_BLUE, 4));
    lv_style_set_transform_scale(&browser->style_selected, 280);
    lv_style_set_shadow_width(&browser->style_selected, 12);
    lv_style_set_shadow_color(&browser->style_selected, COLOR_SELECTED);

    // Multi-selected style
    lv_style_init(&browser->style_multi_selected);
    lv_style_set_border_width(&browser->style_multi_selected, 3);
    lv_style_set_border_color(&browser->style_multi_selected, COLOR_MULTI_SELECTED);
    lv_style_set_bg_color(&browser->style_multi_selected, lv_palette_darken(LV_PALETTE_GREEN, 4));
    lv_style_set_transform_scale(&browser->style_multi_selected, 270);

    // Hover style without opacity
    lv_style_init(&browser->style_hover);
    lv_style_set_bg_color(&browser->style_hover, COLOR_HOVER);
    lv_style_set_transform_scale(&browser->style_hover, 260);

    // Enhanced title style
    lv_style_init(&browser->style_title);
    lv_style_set_text_color(&browser->style_title, COLOR_TEXT);
    lv_style_set_text_font(&browser->style_title, &lv_font_montserrat_28);
    lv_style_set_text_font(&browser->style_title, &lv_font_montserrat_24);

    // Toolbar style
    lv_style_init(&browser->style_toolbar);
    lv_style_set_radius(&browser->style_toolbar, 8);
    lv_style_set_bg_color(&browser->style_toolbar, lv_color_darken(COLOR_CARD, 20));
    lv_style_set_border_width(&browser->style_toolbar, 1);
    lv_style_set_border_color(&browser->style_toolbar, lv_palette_darken(LV_PALETTE_GREY, 2));

    // Button style with hover effects
    lv_style_init(&browser->style_button);
    lv_style_set_radius(&browser->style_button, 8);
    lv_style_set_bg_color(&browser->style_button, COLOR_CARD);
    lv_style_set_border_width(&browser->style_button, 1);
    lv_style_set_border_color(&browser->style_button, lv_palette_darken(LV_PALETTE_GREY, 1));
    lv_style_set_transform_scale(&browser->style_button, 256);

    // Preview overlay style without opacity
    lv_style_init(&browser->style_preview);
    lv_style_set_radius(&browser->style_preview, 16);
    lv_style_set_bg_color(&browser->style_preview, lv_color_darken(COLOR_BG, 50));
    lv_style_set_border_width(&browser->style_preview, 2);
    lv_style_set_border_color(&browser->style_preview, COLOR_ACCENT);
    lv_style_set_shadow_width(&browser->style_preview, 20);
    lv_style_set_shadow_color(&browser->style_preview, lv_color_black());
}

static void create_title_panel(enhanced_logo_browser_data_t* browser) {
    browser->title_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->title_panel, lv_pct(100), TITLE_HEIGHT);
    lv_obj_set_pos(browser->title_panel, 0, 0);
    lv_obj_clear_flag(browser->title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->title_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->title_panel, 0, 0);
    lv_obj_set_style_border_width(browser->title_panel, 0, 0);

    // Enhanced title with icon
    browser->title_label = lv_label_create(browser->title_panel);
    lv_label_set_text(browser->title_label, LV_SYMBOL_IMAGE " Logo Browser");
    lv_obj_add_style(browser->title_label, &browser->style_title, 0);
    lv_obj_align(browser->title_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Status with better formatting
    browser->status_label = lv_label_create(browser->title_panel);
    lv_label_set_text(browser->status_label, "Ready");
    lv_obj_set_style_text_color(browser->status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(browser->status_label, LV_ALIGN_RIGHT_MID, -120, 0);

    // Statistics display
    browser->stats_label = lv_label_create(browser->title_panel);
    lv_label_set_text(browser->stats_label, "0 items");
    lv_obj_set_style_text_color(browser->stats_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(browser->stats_label, &lv_font_montserrat_14, 0);
    lv_obj_align(browser->stats_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_toolbar_panel(enhanced_logo_browser_data_t* browser) {
    browser->toolbar_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->toolbar_panel, lv_pct(100), TOOLBAR_HEIGHT);
    lv_obj_set_pos(browser->toolbar_panel, 0, TITLE_HEIGHT + 5);
    lv_obj_clear_flag(browser->toolbar_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(browser->toolbar_panel, &browser->style_toolbar, 0);

    // View mode button
    browser->btn_view_mode = lv_button_create(browser->toolbar_panel);
    lv_obj_set_size(browser->btn_view_mode, 50, 40);
    lv_obj_align(browser->btn_view_mode, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_style(browser->btn_view_mode, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_view_mode, view_mode_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* view_icon = lv_label_create(browser->btn_view_mode);
    lv_label_set_text(view_icon, LV_SYMBOL_LIST);
    lv_obj_center(view_icon);

    // Sort mode button
    browser->btn_sort = lv_button_create(browser->toolbar_panel);
    lv_obj_set_size(browser->btn_sort, 50, 40);
    lv_obj_align(browser->btn_sort, LV_ALIGN_LEFT_MID, 70, 0);
    lv_obj_add_style(browser->btn_sort, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_sort, sort_mode_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* sort_icon = lv_label_create(browser->btn_sort);
    lv_label_set_text(sort_icon, LV_SYMBOL_UP LV_SYMBOL_DOWN);
    lv_obj_center(sort_icon);

    // Multi-select toggle
    browser->btn_multi_select = lv_button_create(browser->toolbar_panel);
    lv_obj_set_size(browser->btn_multi_select, 50, 40);
    lv_obj_align(browser->btn_multi_select, LV_ALIGN_LEFT_MID, 130, 0);
    lv_obj_add_style(browser->btn_multi_select, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_multi_select, multi_select_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* multi_icon = lv_label_create(browser->btn_multi_select);
    lv_label_set_text(multi_icon, LV_SYMBOL_CALL);
    lv_obj_center(multi_icon);

    // Progress arc for background operations
    browser->progress_arc = lv_arc_create(browser->toolbar_panel);
    lv_obj_set_size(browser->progress_arc, 40, 40);
    lv_obj_align(browser->progress_arc, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_arc_set_range(browser->progress_arc, 0, 100);
    lv_arc_set_value(browser->progress_arc, 0);
    lv_obj_set_style_arc_color(browser->progress_arc, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_flag(browser->progress_arc, LV_OBJ_FLAG_HIDDEN);

    // Refresh button
    browser->btn_refresh = lv_button_create(browser->toolbar_panel);
    lv_obj_set_size(browser->btn_refresh, 50, 40);
    lv_obj_align(browser->btn_refresh, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_style(browser->btn_refresh, &browser->style_button, 0);
    lv_obj_add_event_cb(browser->btn_refresh, refresh_clicked, LV_EVENT_CLICKED, browser);

    lv_obj_t* refresh_icon = lv_label_create(browser->btn_refresh);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_icon);
}

static void create_search_panel(enhanced_logo_browser_data_t* browser) {
    int search_y = TITLE_HEIGHT + TOOLBAR_HEIGHT + 10;

    browser->search_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->search_panel, lv_pct(100), SEARCH_HEIGHT);
    lv_obj_set_pos(browser->search_panel, 0, search_y);
    lv_obj_clear_flag(browser->search_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(browser->search_panel, &browser->style_card, 0);

    // Enhanced search icon
    browser->search_icon = lv_label_create(browser->search_panel);
    lv_label_set_text(browser->search_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(browser->search_icon, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(browser->search_icon, LV_ALIGN_LEFT_MID, 15, 0);

    // Enhanced search textarea with better styling
    browser->search_textarea = lv_textarea_create(browser->search_panel);
    lv_obj_set_size(browser->search_textarea, 450, 40);
    lv_obj_align(browser->search_textarea, LV_ALIGN_LEFT_MID, 50, 0);
    lv_textarea_set_placeholder_text(browser->search_textarea, "Search logos by name...");
    lv_textarea_set_one_line(browser->search_textarea, true);
    lv_obj_set_style_bg_color(browser->search_textarea, lv_color_darken(COLOR_CARD, 30), 0);
    lv_obj_set_style_border_width(browser->search_textarea, 2, 0);
    lv_obj_set_style_border_color(browser->search_textarea, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_radius(browser->search_textarea, 8, 0);
    lv_obj_set_style_pad_all(browser->search_textarea, 8, 0);

    // Edit button with better icon
    browser->btn_edit = lv_button_create(browser->search_panel);
    lv_obj_set_size(browser->btn_edit, 60, 40);
    lv_obj_align(browser->btn_edit, LV_ALIGN_RIGHT_MID, -70, 0);
    lv_obj_add_style(browser->btn_edit, &browser->style_button, 0);

    lv_obj_t* edit_icon = lv_label_create(browser->btn_edit);
    lv_label_set_text(edit_icon, LV_SYMBOL_EDIT);
    lv_obj_center(edit_icon);

    // Clear button
    browser->btn_clear = lv_button_create(browser->search_panel);
    lv_obj_set_size(browser->btn_clear, 60, 40);
    lv_obj_align(browser->btn_clear, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_style(browser->btn_clear, &browser->style_button, 0);

    lv_obj_t* clear_icon = lv_label_create(browser->btn_clear);
    lv_label_set_text(clear_icon, LV_SYMBOL_CLOSE);
    lv_obj_center(clear_icon);
}

static void create_grid_panel(enhanced_logo_browser_data_t* browser) {
    int grid_y = TITLE_HEIGHT + TOOLBAR_HEIGHT + SEARCH_HEIGHT + 15;
    int available_height = lv_obj_get_height(browser->content_panel) - (2 * CONTAINER_PADDING);
    int grid_height = available_height - grid_y - NAV_HEIGHT - 10;

    browser->grid_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->grid_panel, lv_pct(100), grid_height);
    lv_obj_set_pos(browser->grid_panel, 0, grid_y);
    lv_obj_clear_flag(browser->grid_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(browser->grid_panel, COLOR_BG, 0);
    lv_obj_set_style_pad_all(browser->grid_panel, 0, 0);
    lv_obj_set_style_border_width(browser->grid_panel, 0, 0);

    // Enhanced grid layout with responsive design
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(browser->grid_panel, col_dsc, row_dsc);
    lv_obj_set_style_pad_all(browser->grid_panel, GRID_SPACING, 0);
    lv_obj_set_style_pad_column(browser->grid_panel, GRID_SPACING, 0);
    lv_obj_set_style_pad_row(browser->grid_panel, GRID_SPACING, 0);

    // Create enhanced logo items
    for (int i = 0; i < ENHANCED_LOGOS_PER_PAGE; i++) {
        enhanced_logo_item_t* item = &browser->logos[i];

        // Enhanced container with hover effects
        item->container = lv_obj_create(browser->grid_panel);
        lv_obj_set_grid_cell(item->container, LV_GRID_ALIGN_STRETCH, i % 3, 1,
                             LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_add_flag(item->container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item->container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_style(item->container, &browser->style_card, 0);
        lv_obj_add_style(item->container, &browser->style_hover, LV_STATE_PRESSED);
        lv_obj_add_event_cb(item->container, enhanced_logo_clicked, LV_EVENT_CLICKED, browser);
        lv_obj_set_user_data(item->container, (void*)(intptr_t)i);

        // Selection indicator
        item->selection_indicator = lv_obj_create(item->container);
        lv_obj_set_size(item->selection_indicator, 24, 24);
        lv_obj_align(item->selection_indicator, LV_ALIGN_TOP_RIGHT, -5, 5);
        lv_obj_set_style_radius(item->selection_indicator, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(item->selection_indicator, COLOR_SUCCESS, 0);
        lv_obj_set_style_border_width(item->selection_indicator, 2, 0);
        lv_obj_set_style_border_color(item->selection_indicator, COLOR_TEXT, 0);
        lv_obj_add_flag(item->selection_indicator, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* checkmark = lv_label_create(item->selection_indicator);
        lv_label_set_text(checkmark, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(checkmark, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(checkmark, &lv_font_montserrat_12, 0);
        lv_obj_center(checkmark);

        // Progress ring for loading
        item->progress_ring = lv_arc_create(item->container);
        lv_obj_set_size(item->progress_ring, 40, 40);
        lv_obj_align(item->progress_ring, LV_ALIGN_CENTER, 0, -10);
        lv_arc_set_range(item->progress_ring, 0, 100);
        lv_arc_set_value(item->progress_ring, 0);
        lv_obj_set_style_arc_color(item->progress_ring, COLOR_ACCENT, LV_PART_INDICATOR);
        lv_obj_add_flag(item->progress_ring, LV_OBJ_FLAG_HIDDEN);

        // Enhanced image with better sizing
        item->image = lv_image_create(item->container);
        lv_obj_set_size(item->image, THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        lv_obj_align(item->image, LV_ALIGN_TOP_MID, 0, 15);
        lv_image_set_scale(item->image, 256);
        lv_image_set_antialias(item->image, true);

        // Primary label with better typography
        item->label = lv_label_create(item->container);
        lv_label_set_text(item->label, "");
        lv_obj_set_style_text_align(item->label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(item->label, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(item->label, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(item->label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(item->label, lv_pct(90));
        lv_obj_align(item->label, LV_ALIGN_BOTTOM_MID, 0, -25);

        // Size label for metadata
        item->size_label = lv_label_create(item->container);
        lv_label_set_text(item->size_label, "");
        lv_obj_set_style_text_align(item->size_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(item->size_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(item->size_label, &lv_font_montserrat_10, 0);
        lv_obj_set_width(item->size_label, lv_pct(90));
        lv_obj_align(item->size_label, LV_ALIGN_BOTTOM_MID, 0, -5);

        // Initialize item state
        lv_obj_add_flag(item->container, LV_OBJ_FLAG_HIDDEN);
        item->is_loading = false;
        item->is_visible = false;
        item->load_progress = 0.0f;
        memset(&item->metadata, 0, sizeof(logo_metadata_t));
    }
}

static void create_nav_panel(enhanced_logo_browser_data_t* browser) {
    int nav_y = lv_obj_get_height(browser->content_panel) - NAV_HEIGHT - CONTAINER_PADDING;

    browser->nav_panel = lv_obj_create(browser->content_panel);
    lv_obj_set_size(browser->nav_panel, lv_pct(100), NAV_HEIGHT);
    lv_obj_set_pos(browser->nav_panel, 0, nav_y);
    lv_obj_clear_flag(browser->nav_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(browser->nav_panel, &browser->style_card, 0);

    // Enhanced previous button
    browser->btn_prev = lv_button_create(browser->nav_panel);
    lv_obj_set_size(browser->btn_prev, 80, 45);
    lv_obj_align(browser->btn_prev, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_add_style(browser->btn_prev, &browser->style_button, 0);

    lv_obj_t* prev_label = lv_label_create(browser->btn_prev);
    lv_label_set_text(prev_label, LV_SYMBOL_LEFT " Prev");
    lv_obj_center(prev_label);

    // Page slider for quick navigation
    browser->page_slider = lv_slider_create(browser->nav_panel);
    lv_obj_set_size(browser->page_slider, 200, 20);
    lv_obj_align(browser->page_slider, LV_ALIGN_CENTER, 0, -8);
    lv_slider_set_range(browser->page_slider, 0, 1);
    lv_slider_set_value(browser->page_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(browser->page_slider, COLOR_ACCENT, LV_PART_INDICATOR);

    // Enhanced page indicator
    browser->page_label = lv_label_create(browser->nav_panel);
    lv_label_set_text(browser->page_label, "Page 0 of 0");
    lv_obj_set_style_text_color(browser->page_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(browser->page_label, &lv_font_montserrat_16, 0);
    lv_obj_align(browser->page_label, LV_ALIGN_CENTER, 0, 12);

    // Enhanced loading bar
    browser->loading_bar = lv_bar_create(browser->nav_panel);
    lv_obj_set_size(browser->loading_bar, 250, 6);
    lv_obj_align(browser->loading_bar, LV_ALIGN_CENTER, 0, 25);
    lv_bar_set_range(browser->loading_bar, 0, 100);
    lv_obj_set_style_bg_color(browser->loading_bar, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
    lv_obj_set_style_bg_color(browser->loading_bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(browser->loading_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(browser->loading_bar, LV_OBJ_FLAG_HIDDEN);

    // Enhanced next button
    browser->btn_next = lv_button_create(browser->nav_panel);
    lv_obj_set_size(browser->btn_next, 80, 45);
    lv_obj_align(browser->btn_next, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_add_style(browser->btn_next, &browser->style_button, 0);

    lv_obj_t* next_label = lv_label_create(browser->btn_next);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_center(next_label);
}

static void create_preview_overlay(enhanced_logo_browser_data_t* browser) {
    browser->preview_overlay = lv_obj_create(browser->container);
    lv_obj_set_size(browser->preview_overlay, PREVIEW_OVERLAY_SIZE, PREVIEW_OVERLAY_SIZE);
    lv_obj_center(browser->preview_overlay);
    lv_obj_add_style(browser->preview_overlay, &browser->style_preview, 0);
    lv_obj_add_flag(browser->preview_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(browser->preview_overlay, LV_OBJ_FLAG_FLOATING);

    // Close button for preview
    lv_obj_t* close_btn = lv_button_create(browser->preview_overlay);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xff4444), 0);

    lv_obj_t* close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_center(close_icon);
}

// Background processing implementation
static void background_task(void* parameters) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)parameters;
    background_task_t task;

    ESP_LOGI(TAG, "Background task started on Core %d", xPortGetCoreID());

    while (browser->bg_manager.running) {
        if (xQueueReceive(browser->bg_manager.task_queue, &task, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (task.type) {
                case TASK_LOAD_THUMBNAILS:
                    // Implement thumbnail loading logic
                    ESP_LOGI(TAG, "Loading thumbnails for page %d", task.page_index);
                    break;

                case TASK_GENERATE_PREVIEW:
                    // Implement preview generation
                    ESP_LOGI(TAG, "Generating preview for %s", task.path);
                    break;

                case TASK_SCAN_DIRECTORY:
                    // Implement directory scanning
                    ESP_LOGI(TAG, "Scanning directory");
                    break;

                case TASK_PROCESS_BATCH:
                    // Implement batch processing
                    ESP_LOGI(TAG, "Processing batch operation");
                    break;
            }
        }

        // Yield to allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Background task terminated");
    vTaskDelete(NULL);
}

bool enhanced_logo_browser_init_background(lv_obj_t* browser_obj) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return false;

    // Create task queue
    browser->bg_manager.task_queue = xQueueCreate(BACKGROUND_TASK_QUEUE_SIZE, sizeof(background_task_t));
    if (!browser->bg_manager.task_queue) {
        ESP_LOGE(TAG, "Failed to create background task queue");
        return false;
    }

    // Create cache mutex
    browser->bg_manager.cache_mutex = xSemaphoreCreateMutex();
    if (!browser->bg_manager.cache_mutex) {
        ESP_LOGE(TAG, "Failed to create cache mutex");
        vQueueDelete(browser->bg_manager.task_queue);
        return false;
    }

    // Create background task on Core 1
    browser->bg_manager.running = true;
    browser->bg_manager.browser_ref = browser_obj;

    BaseType_t result = xTaskCreatePinnedToCore(
        background_task,
        "logo_browser_bg",
        CORE_1_STACK_SIZE,
        browser,
        tskIDLE_PRIORITY + 2,
        &browser->bg_manager.task_handle,
        1  // Pin to Core 1
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create background task");
        browser->bg_manager.running = false;
        vSemaphoreDelete(browser->bg_manager.cache_mutex);
        vQueueDelete(browser->bg_manager.task_queue);
        return false;
    }

    ESP_LOGI(TAG, "Background processing initialized successfully");
    return true;
}

// Cache management
static void init_thumbnail_cache(lru_cache_t* cache) {
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;
    cache->max_size = PREVIEW_CACHE_SIZE;
}

static void cleanup_thumbnail_cache(lru_cache_t* cache) {
    cache_entry_t* current = cache->head;
    while (current) {
        cache_entry_t* next = current->next;
        if (current->thumbnail) {
            lv_free((void*)current->thumbnail->data);
            lv_free(current->thumbnail);
        }
        lv_free(current);
        current = next;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;
}

// Event handlers
static void enhanced_logo_clicked(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    lv_obj_t* container = lv_event_get_target(e);
    if (!browser || !container) return;

    int index = (int)(intptr_t)lv_obj_get_user_data(container);
    enhanced_logo_item_t* item = &browser->logos[index];

    if (browser->multi_select_enabled) {
        // Toggle selection in multi-select mode
        item->metadata.is_selected = !item->metadata.is_selected;

        if (item->metadata.is_selected) {
            browser->selected_count++;
            lv_obj_remove_flag(item->selection_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_style(item->container, &browser->style_multi_selected, 0);
        } else {
            browser->selected_count--;
            lv_obj_add_flag(item->selection_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_style(item->container, &browser->style_multi_selected, 0);
        }

        update_selection_indicators(browser);
    } else {
        // Single selection mode
        if (browser->primary_selection >= 0) {
            enhanced_logo_item_t* prev_item = &browser->logos[browser->primary_selection];
            lv_obj_remove_style(prev_item->container, &browser->style_selected, 0);
        }

        browser->primary_selection = index;
        lv_obj_add_style(item->container, &browser->style_selected, 0);

        // Show preview after short delay
        enhanced_logo_browser_show_preview(browser->container, item->metadata.path);
    }

    ESP_LOGI(TAG, "Logo clicked: %s (index %d)", item->metadata.path, index);
}

static void update_selection_indicators(enhanced_logo_browser_data_t* browser) {
    // Update stats label with selection count
    char stats_text[64];
    if (browser->selected_count > 0) {
        snprintf(stats_text, sizeof(stats_text), "%d selected • %d total",
                 browser->selected_count, browser->total_logos);
    } else {
        snprintf(stats_text, sizeof(stats_text), "%d items", browser->total_logos);
    }
    lv_label_set_text(browser->stats_label, stats_text);
}

// Public API implementations
void enhanced_logo_browser_set_view_mode(lv_obj_t* browser_obj, view_mode_t mode) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    browser->view_mode = mode;
    apply_view_mode(browser);
}

void enhanced_logo_browser_set_multi_select(lv_obj_t* browser_obj, bool enabled) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    browser->multi_select_enabled = enabled;

    if (!enabled) {
        // Clear all selections when disabling multi-select
        enhanced_logo_browser_clear_selection(browser_obj);
    }

    // Update button appearance
    lv_obj_t* multi_icon = lv_obj_get_child(browser->btn_multi_select, 0);
    if (enabled) {
        lv_obj_set_style_bg_color(browser->btn_multi_select, COLOR_SUCCESS, 0);
        lv_label_set_text(multi_icon, LV_SYMBOL_OK);
    } else {
        lv_obj_set_style_bg_color(browser->btn_multi_select, COLOR_CARD, 0);
        lv_label_set_text(multi_icon, LV_SYMBOL_CALL);
    }
}

void enhanced_logo_browser_show_preview(lv_obj_t* browser_obj, const char* logo_path) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser || !logo_path) return;

    // Show overlay with scale animation instead of fade
    lv_obj_remove_flag(browser->preview_overlay, LV_OBJ_FLAG_HIDDEN);

    // Use scale animation for smooth appearance without opacity
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, browser->preview_overlay);
    lv_anim_set_values(&scale_anim, 200, 256);  // Start small, scale to normal
    lv_anim_set_time(&scale_anim, ANIMATION_TIME);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&scale_anim, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_scale);
    lv_anim_start(&scale_anim);

    ESP_LOGI(TAG, "Showing preview for: %s", logo_path);
}

void enhanced_logo_browser_cleanup(lv_obj_t* browser_obj) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    // Stop background task
    browser->bg_manager.running = false;
    if (browser->bg_manager.task_handle) {
        vTaskDelete(browser->bg_manager.task_handle);
    }

    // Cleanup resources
    if (browser->bg_manager.task_queue) {
        vQueueDelete(browser->bg_manager.task_queue);
    }
    if (browser->bg_manager.cache_mutex) {
        vSemaphoreDelete(browser->bg_manager.cache_mutex);
    }

    // Cleanup thumbnail cache
    cleanup_thumbnail_cache(&browser->thumbnail_cache);

    // Cleanup styles
    lv_style_reset(&browser->style_container);
    lv_style_reset(&browser->style_card);
    lv_style_reset(&browser->style_selected);
    lv_style_reset(&browser->style_multi_selected);
    lv_style_reset(&browser->style_hover);
    lv_style_reset(&browser->style_title);
    lv_style_reset(&browser->style_button);
    lv_style_reset(&browser->style_toolbar);
    lv_style_reset(&browser->style_preview);

    // Free browser structure
    lv_free(browser);

    ESP_LOGI(TAG, "Enhanced logo browser cleaned up");
}

// Missing function implementations
static void apply_view_mode(enhanced_logo_browser_data_t* browser) {
    if (!browser) return;

    // Update view mode icon
    lv_obj_t* view_icon = lv_obj_get_child(browser->btn_view_mode, 0);

    switch (browser->view_mode) {
        case VIEW_MODE_GRID:
            lv_label_set_text(view_icon, LV_SYMBOL_LIST);
            // Adjust grid layout for grid view
            break;
        case VIEW_MODE_LIST:
            lv_label_set_text(view_icon, LV_SYMBOL_LIST);
            // Implement list view layout
            break;
        case VIEW_MODE_LARGE_ICONS:
            lv_label_set_text(view_icon, LV_SYMBOL_IMAGE);
            // Implement large icon view
            break;
    }

    ESP_LOGI(TAG, "Applied view mode: %d", browser->view_mode);
}

static void view_mode_clicked(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    // Cycle through view modes
    browser->view_mode = (view_mode_t)((browser->view_mode + 1) % 3);
    apply_view_mode(browser);
}

static void sort_mode_clicked(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    // Cycle through sort modes
    browser->sort_mode = (sort_mode_t)((browser->sort_mode + 1) % 6);

    // Update sort icon
    lv_obj_t* sort_icon = lv_obj_get_child(browser->btn_sort, 0);
    switch (browser->sort_mode) {
        case SORT_NAME_ASC:
            lv_label_set_text(sort_icon, "A" LV_SYMBOL_UP);
            break;
        case SORT_NAME_DESC:
            lv_label_set_text(sort_icon, "A" LV_SYMBOL_DOWN);
            break;
        case SORT_DATE_ASC:
            lv_label_set_text(sort_icon, LV_SYMBOL_ENVELOPE LV_SYMBOL_UP);
            break;
        case SORT_DATE_DESC:
            lv_label_set_text(sort_icon, LV_SYMBOL_ENVELOPE LV_SYMBOL_DOWN);
            break;
        case SORT_SIZE_ASC:
            lv_label_set_text(sort_icon, LV_SYMBOL_SETTINGS LV_SYMBOL_UP);
            break;
        case SORT_SIZE_DESC:
            lv_label_set_text(sort_icon, LV_SYMBOL_SETTINGS LV_SYMBOL_DOWN);
            break;
    }

    ESP_LOGI(TAG, "Sort mode changed to: %d", browser->sort_mode);
}

static void multi_select_clicked(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    enhanced_logo_browser_set_multi_select(browser->container, !browser->multi_select_enabled);
}

static void refresh_clicked(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    enhanced_logo_browser_refresh(browser->container, true);
}

static void gesture_handler(lv_event_t* e) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_event_get_user_data(e);
    if (!browser) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    switch (dir) {
        case LV_DIR_LEFT:
            // Swipe left - next page
            enhanced_logo_browser_next_page(browser->container);
            break;
        case LV_DIR_RIGHT:
            // Swipe right - previous page
            enhanced_logo_browser_prev_page(browser->container);
            break;
        default:
            break;
    }
}

// Additional missing API implementations
void enhanced_logo_browser_clear_selection(lv_obj_t* browser_obj) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    // Clear all selections
    for (int i = 0; i < ENHANCED_LOGOS_PER_PAGE; i++) {
        enhanced_logo_item_t* item = &browser->logos[i];
        if (item->metadata.is_selected) {
            item->metadata.is_selected = false;
            lv_obj_add_flag(item->selection_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_style(item->container, &browser->style_multi_selected, 0);
        }
    }

    browser->selected_count = 0;
    browser->primary_selection = -1;
    update_selection_indicators(browser);
}

void enhanced_logo_browser_next_page(lv_obj_t* browser_obj) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page < browser->total_pages - 1) {
        browser->current_page++;
        update_enhanced_display(browser);

        // Update page slider
        lv_slider_set_value(browser->page_slider, browser->current_page, LV_ANIM_ON);
    }
}

void enhanced_logo_browser_prev_page(lv_obj_t* browser_obj) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    if (browser->current_page > 0) {
        browser->current_page--;
        update_enhanced_display(browser);

        // Update page slider
        lv_slider_set_value(browser->page_slider, browser->current_page, LV_ANIM_ON);
    }
}

void enhanced_logo_browser_refresh(lv_obj_t* browser_obj, bool force_rescan) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser) return;

    browser->state = ENHANCED_BROWSER_STATE_LOADING;

    // Show progress indicator
    lv_obj_remove_flag(browser->progress_arc, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(browser->progress_arc, 0);

    if (force_rescan) {
        // Queue background task for directory scan
        background_task_t task = {
            .type = TASK_SCAN_DIRECTORY,
            .page_index = 0,
            .user_data = browser};

        if (browser->bg_manager.task_queue) {
            xQueueSend(browser->bg_manager.task_queue, &task, pdMS_TO_TICKS(100));
        }
    }

    update_enhanced_display(browser);

    ESP_LOGI(TAG, "Browser refresh initiated (force_rescan: %d)", force_rescan);
}

static void update_enhanced_display(enhanced_logo_browser_data_t* browser) {
    if (!browser) return;

    // Update page indicator
    char page_text[64];
    if (browser->total_pages > 0) {
        snprintf(page_text, sizeof(page_text), "Page %d of %d",
                 browser->current_page + 1, browser->total_pages);
    } else {
        snprintf(page_text, sizeof(page_text), "No pages");
    }
    lv_label_set_text(browser->page_label, page_text);

    // Update navigation buttons
    lv_obj_set_state(browser->btn_prev, browser->current_page == 0 ? LV_STATE_DISABLED : 0, true);
    lv_obj_set_state(browser->btn_next, browser->current_page >= browser->total_pages - 1 ? LV_STATE_DISABLED : 0, true);

    // Update slider range
    if (browser->total_pages > 1) {
        lv_slider_set_range(browser->page_slider, 0, browser->total_pages - 1);
        lv_slider_set_value(browser->page_slider, browser->current_page, LV_ANIM_OFF);
    }

    update_stats_display(browser);
}

static void update_stats_display(enhanced_logo_browser_data_t* browser) {
    if (!browser) return;

    // Update statistics
    char stats_text[64];
    if (browser->selected_count > 0) {
        snprintf(stats_text, sizeof(stats_text), "%d selected • %d total",
                 browser->selected_count, browser->total_logos);
    } else {
        snprintf(stats_text, sizeof(stats_text), "%d items", browser->total_logos);
    }
    lv_label_set_text(browser->stats_label, stats_text);

    // Update status
    const char* status_text = "Ready";
    switch (browser->state) {
        case ENHANCED_BROWSER_STATE_LOADING:
            status_text = "Loading...";
            break;
        case ENHANCED_BROWSER_STATE_SEARCHING:
            status_text = "Searching...";
            break;
        case ENHANCED_BROWSER_STATE_PROCESSING:
            status_text = "Processing...";
            break;
        case ENHANCED_BROWSER_STATE_MULTI_SELECT:
            status_text = "Multi-select";
            break;
        case ENHANCED_BROWSER_STATE_ERROR:
            status_text = "Error";
            break;
        default:
            break;
    }
    lv_label_set_text(browser->status_label, status_text);
}

int enhanced_logo_browser_scan_directory(lv_obj_t* browser_obj, const char* directory) {
    enhanced_logo_browser_data_t* browser = (enhanced_logo_browser_data_t*)lv_obj_get_user_data(browser_obj);
    if (!browser || !directory) return 0;
    
    // Set state to loading
    browser->state = ENHANCED_BROWSER_STATE_LOADING;
    
    // Get total logo count from LogoBrowser backend
    browser->total_logos = logo_browser_get_total_logos();
    
    // Update pagination
    browser->total_pages = (browser->total_logos + ENHANCED_LOGOS_PER_PAGE - 1) / ENHANCED_LOGOS_PER_PAGE;
    browser->current_page = 0;
    
    // Update display
    update_enhanced_display(browser);
    
    // Start background thumbnail loading if available
    if (browser->bg_manager.task_handle) {
        background_task_t task = {
            .type = TASK_LOAD_THUMBNAILS,
            .page_index = 0,
            .user_data = browser_obj
        };
        strncpy(task.path, directory, sizeof(task.path) - 1);
        xQueueSend(browser->bg_manager.task_queue, &task, 0);
    }
    
    // Set state back to idle
    browser->state = ENHANCED_BROWSER_STATE_IDLE;
    
    ESP_LOGI(TAG, "Scanned directory '%s', found %d logos", directory, browser->total_logos);
    return browser->total_logos;
}
