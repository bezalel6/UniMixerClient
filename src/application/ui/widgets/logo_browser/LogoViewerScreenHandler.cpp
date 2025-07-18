#include "LogoViewerScreenHandler.h"
#include "LogoBrowser.h"
#include "../../../../ui/ui.h"
#include "../../../../logo/SimpleLogoManager.h"
#include <esp_log.h>

extern "C" {
    // Functions from LogoBrowserWidget.c
    lv_obj_t* logo_browser_create(lv_obj_t* parent);
    int logo_browser_scan_directory(lv_obj_t* browser_obj, const char* logo_directory);
    void logo_browser_cleanup(lv_obj_t* browser);
}

namespace Application {
namespace UI {
namespace Widgets {

static const char* TAG = "LogoViewerScreenHandler";

// Static member definitions
lv_obj_t* LogoViewerScreenHandler::logoBrowserInstance = nullptr;
lv_obj_t* LogoViewerScreenHandler::titleLabel = nullptr;
bool LogoViewerScreenHandler::initialized = false;

void LogoViewerScreenHandler::init() {
    ESP_LOGI(TAG, "Initializing Logo Viewer screen handler");
    initialized = true;
}

void LogoViewerScreenHandler::cleanup() {
    ESP_LOGI(TAG, "Cleaning up Logo Viewer screen handler");
    cleanupLogoBrowser();
    initialized = false;
}

void LogoViewerScreenHandler::checkAndInitializeScreen(lv_obj_t* screen) {
    if (!initialized || !screen) return;
    
    // Check if this is the Logo Viewer screen
    if (screen == ui_screenLogoViewer) {
        ESP_LOGI(TAG, "Logo Viewer screen detected, initializing logo browser");
        initializeLogoBrowser(screen);
    } else {
        // If we're leaving the Logo Viewer screen, clean up
        if (logoBrowserInstance != nullptr) {
            cleanupLogoBrowser();
        }
    }
}

void LogoViewerScreenHandler::initializeLogoBrowser(lv_obj_t* screen) {
    if (!screen) return;
    
    // Clean up any existing instance
    cleanupLogoBrowser();
    
    // Add title label
    titleLabel = lv_label_create(screen);
    lv_label_set_text(titleLabel, "Logo Explorer");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 10);
    
    // Create logo browser
    logoBrowserInstance = logo_browser_create(screen);
    if (logoBrowserInstance) {
        // Position it below the title and BACK button
        lv_obj_set_pos(logoBrowserInstance, 0, 60);
        
        // Scan for logos
        SimpleLogoManager::getInstance().scanLogosOnce();
        int logoCount = logo_browser_scan_directory(logoBrowserInstance, "/logos");
        
        ESP_LOGI(TAG, "Logo browser initialized with %d logos", logoCount);
    } else {
        ESP_LOGE(TAG, "Failed to create logo browser");
    }
}

void LogoViewerScreenHandler::cleanupLogoBrowser() {
    if (logoBrowserInstance) {
        ESP_LOGI(TAG, "Cleaning up logo browser instance");
        logo_browser_cleanup(logoBrowserInstance);
        lv_obj_delete(logoBrowserInstance);
        logoBrowserInstance = nullptr;
    }
    
    if (titleLabel) {
        lv_obj_delete(titleLabel);
        titleLabel = nullptr;
    }
}

} // namespace Widgets
} // namespace UI
} // namespace Application