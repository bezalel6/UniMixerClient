#include "ScreenManager.h"
#include "widgets/logo_browser/LogoViewerScreenHandler.h"
#include "../../ui/ui.h"
#include <esp_log.h>

static const char* TAG = "ScreenManager";

namespace Application {
namespace UI {

ScreenManager& ScreenManager::getInstance() {
    static ScreenManager instance;
    return instance;
}

void ScreenManager::init() {
    ESP_LOGI(TAG, "Initializing ScreenManager");
    initialized = true;
    
    // Initialize widget handlers
    Widgets::LogoViewerScreenHandler::init();
    
    // Register screen callbacks for Logo Viewer
    registerScreenLoadCallback(ui_screenLogoViewer, [](lv_obj_t* screen) {
        Widgets::LogoViewerScreenHandler::checkAndInitializeScreen(screen);
    });
    
    registerScreenUnloadCallback(ui_screenLogoViewer, [](lv_obj_t* screen) {
        // The handler will clean up when checking a different screen
        Widgets::LogoViewerScreenHandler::checkAndInitializeScreen(nullptr);
    });
}

void ScreenManager::cleanup() {
    ESP_LOGI(TAG, "Cleaning up ScreenManager");
    
    // Cleanup widget handlers
    Widgets::LogoViewerScreenHandler::cleanup();
    
    loadCallbacks.clear();
    unloadCallbacks.clear();
    currentScreen = nullptr;
    initialized = false;
}

void ScreenManager::registerScreenLoadCallback(lv_obj_t* screen, ScreenCallback callback) {
    if (!screen || !callback) return;
    loadCallbacks[screen] = callback;
    ESP_LOGI(TAG, "Registered load callback for screen %p", screen);
}

void ScreenManager::registerScreenUnloadCallback(lv_obj_t* screen, ScreenCallback callback) {
    if (!screen || !callback) return;
    unloadCallbacks[screen] = callback;
    ESP_LOGI(TAG, "Registered unload callback for screen %p", screen);
}

void ScreenManager::update() {
    if (!initialized) return;
    
    // Check if the active screen has changed
    lv_obj_t* activeScreen = lv_scr_act();
    if (activeScreen != currentScreen) {
        ESP_LOGI(TAG, "Screen change detected: %p -> %p", currentScreen, activeScreen);
        
        // Call unload callbacks for the previous screen
        if (currentScreen != nullptr) {
            auto it = unloadCallbacks.find(currentScreen);
            if (it != unloadCallbacks.end()) {
                ESP_LOGI(TAG, "Calling unload callback for screen %p", currentScreen);
                it->second(currentScreen);
            }
        }
        
        // Update current screen
        currentScreen = activeScreen;
        
        // Call load callbacks for the new screen
        if (currentScreen != nullptr) {
            auto it = loadCallbacks.find(currentScreen);
            if (it != loadCallbacks.end()) {
                ESP_LOGI(TAG, "Calling load callback for screen %p", currentScreen);
                it->second(currentScreen);
            }
        }
    }
}

void ScreenManager::notifyScreenChange(lv_obj_t* newScreen) {
    // This can be called before the actual screen change to prepare
    ESP_LOGI(TAG, "Screen change notification for screen %p", newScreen);
    // The actual handling will happen in update() when LVGL changes the screen
}

} // namespace UI
} // namespace Application