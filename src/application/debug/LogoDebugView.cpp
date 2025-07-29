#include "LogoDebugView.h"
#include "../../logo/LogoManager.h"
#include "../../hardware/SDManager.h"
#include <SD.h>
#include <esp_log.h>

static const char* TAG = "LogoDebugView";

namespace Application {
namespace Debug {

LogoDebugView& LogoDebugView::getInstance() {
    static LogoDebugView instance;
    return instance;
}

void LogoDebugView::show() {
    if (container != nullptr) {
        ESP_LOGW(TAG, "Debug view already visible");
        return;
    }
    
    ESP_LOGI(TAG, "Showing logo debug view");
    createUI();
    loadLogos();
}

void LogoDebugView::hide() {
    if (container == nullptr) {
        return;
    }
    
    ESP_LOGI(TAG, "Hiding logo debug view");
    destroyUI();
}

void LogoDebugView::createUI() {
    // Create semi-transparent background
    background = lv_obj_create(lv_screen_active());
    lv_obj_set_size(background, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(background, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(background, 180, 0);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_SCROLLABLE);
    
    // Main container
    container = lv_obj_create(background);
    lv_obj_set_size(container, 750, 430);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(container, 2, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title bar
    lv_obj_t* titleBar = lv_obj_create(container);
    lv_obj_set_size(titleBar, LV_PCT(100), 50);
    lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(titleBar, 0, 0);
    lv_obj_set_style_radius(titleBar, 10, 0);
    lv_obj_set_style_radius(titleBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(titleBar, 0, 0);
    lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title label
    titleLabel = lv_label_create(titleBar);
    lv_label_set_text(titleLabel, "Logo Debug View");
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
    
    // Close button
    closeBtn = lv_button_create(titleBar);
    lv_obj_set_size(closeBtn, 80, 35);
    lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(closeBtn, onCloseClicked, LV_EVENT_CLICKED, this);
    
    lv_obj_t* closeBtnLabel = lv_label_create(closeBtn);
    lv_label_set_text(closeBtnLabel, "Close");
    lv_obj_center(closeBtnLabel);
    
    // Info label
    infoLabel = lv_label_create(container);
    lv_label_set_text(infoLabel, "Loading logos...");
    lv_obj_align(infoLabel, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_obj_set_style_text_color(infoLabel, lv_color_hex(0xcccccc), 0);
    
    // Scrollable container for logos
    scrollContainer = lv_obj_create(container);
    lv_obj_set_size(scrollContainer, 710, 300);
    lv_obj_align(scrollContainer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(scrollContainer, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(scrollContainer, 1, 0);
    lv_obj_set_style_border_color(scrollContainer, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(scrollContainer, 5, 0);
    lv_obj_set_style_pad_all(scrollContainer, 10, 0);
    
    // Set up grid layout
    static int32_t col_dsc[GRID_COLS + 1];
    static int32_t row_dsc[2] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    
    for (int i = 0; i < GRID_COLS; i++) {
        col_dsc[i] = LOGO_SIZE + LOGO_SPACING;
    }
    col_dsc[GRID_COLS] = LV_GRID_TEMPLATE_LAST;
    
    lv_obj_set_grid_dsc_array(scrollContainer, col_dsc, row_dsc);
    lv_obj_set_layout(scrollContainer, LV_LAYOUT_GRID);
}

void LogoDebugView::destroyUI() {
    if (background) {
        lv_obj_del(background);
        background = nullptr;
        container = nullptr;
        panel = nullptr;
        titleLabel = nullptr;
        closeBtn = nullptr;
        scrollContainer = nullptr;
        infoLabel = nullptr;
    }
}

void LogoDebugView::loadLogos() {
    if (!scrollContainer) return;
    
    // Check SD card
    if (!Hardware::SD::isMounted()) {
        lv_label_set_text(infoLabel, "Error: SD card not mounted!");
        lv_obj_set_style_text_color(infoLabel, lv_color_hex(0xff4444), 0);
        return;
    }
    
    // Open logos directory
    File root = SD.open("/logos");
    if (!root || !root.isDirectory()) {
        lv_label_set_text(infoLabel, "Error: /logos directory not found!");
        lv_obj_set_style_text_color(infoLabel, lv_color_hex(0xff4444), 0);
        return;
    }
    
    int logoCount = 0;
    int col = 0;
    int row = 0;
    
    // Clear existing content
    lv_obj_clean(scrollContainer);
    
    // Read all files
    File file = root.openNextFile();
    while (file) {
        String filename = file.name();
        
        // Only show PNG files
        if (filename.endsWith(".png") || filename.endsWith(".PNG")) {
            lv_obj_t* logoItem = createLogoItem(scrollContainer, filename);
            lv_obj_set_grid_cell(logoItem, LV_GRID_ALIGN_CENTER, col, 1,
                                LV_GRID_ALIGN_START, row, 1);
            
            logoCount++;
            col++;
            if (col >= GRID_COLS) {
                col = 0;
                row++;
            }
        }
        
        file.close();
        file = root.openNextFile();
    }
    
    root.close();
    
    // Update info label
    char info[128];
    snprintf(info, sizeof(info), "Found %d logos in /logos directory", logoCount);
    lv_label_set_text(infoLabel, info);
    lv_obj_set_style_text_color(infoLabel, lv_color_hex(0x44ff44), 0);
    
    ESP_LOGI(TAG, "Loaded %d logos", logoCount);
}

lv_obj_t* LogoDebugView::createLogoItem(lv_obj_t* parent, const String& filename) {
    // Container for logo + label
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_size(item, LOGO_SIZE, LOGO_SIZE + 25);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(item, 5, 0);
    lv_obj_set_style_pad_all(item, 5, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    
    // Logo image
    lv_obj_t* img = lv_image_create(item);
    lv_obj_set_size(img, LOGO_SIZE - 10, LOGO_SIZE - 30);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
    
    // Build LVGL path (S:/logos/filename.png)
    String lvglPath = "S:/logos/" + filename;
    lv_image_set_src(img, lvglPath.c_str());
    
    // Make it fit within bounds
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
    
    // Label with filename (truncated if needed)
    lv_obj_t* label = lv_label_create(item);
    lv_obj_set_width(label, LOGO_SIZE - 10);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xaaaaaa), 0);
    
    // Remove extension for display
    String displayName = filename;
    int dotIndex = displayName.lastIndexOf('.');
    if (dotIndex > 0) {
        displayName = displayName.substring(0, dotIndex);
    }
    lv_label_set_text(label, displayName.c_str());
    
    // Store filename in user data for click handler
    lv_obj_set_user_data(img, (void*)strdup(filename.c_str()));
    
    // Add click handler
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(img, onLogoClicked, LV_EVENT_CLICKED, nullptr);
    
    return item;
}

void LogoDebugView::onCloseClicked(lv_event_t* e) {
    LogoDebugView* view = static_cast<LogoDebugView*>(lv_event_get_user_data(e));
    if (view) {
        view->hide();
    }
}

void LogoDebugView::onLogoClicked(lv_event_t* e) {
    lv_obj_t* img = (lv_obj_t*)lv_event_get_target(e);
    char* filename = (char*)lv_obj_get_user_data(img);
    
    if (filename) {
        ESP_LOGI(TAG, "Logo clicked: %s", filename);
        
        // Could show a popup with full path or other debug info
        // For now, just log it
    }
}

} // namespace Debug
} // namespace Application