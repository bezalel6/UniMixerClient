#pragma once

#include <lvgl.h>
#include <functional>
#include <unordered_map>

namespace Application {
namespace UI {

/**
 * @brief Manager for screen change detection and custom widget initialization
 * 
 * This class monitors LVGL screen changes and allows registration of callbacks
 * for when specific screens are loaded or unloaded.
 */
class ScreenManager {
public:
    using ScreenCallback = std::function<void(lv_obj_t*)>;
    
    /**
     * Get the singleton instance
     */
    static ScreenManager& getInstance();
    
    /**
     * Initialize the screen manager
     */
    void init();
    
    /**
     * Cleanup the screen manager
     */
    void cleanup();
    
    /**
     * Register a callback for when a specific screen is loaded
     * @param screen The screen to monitor
     * @param callback The callback to execute when the screen is loaded
     */
    void registerScreenLoadCallback(lv_obj_t* screen, ScreenCallback callback);
    
    /**
     * Register a callback for when a specific screen is unloaded
     * @param screen The screen to monitor
     * @param callback The callback to execute when the screen is unloaded
     */
    void registerScreenUnloadCallback(lv_obj_t* screen, ScreenCallback callback);
    
    /**
     * Update function to be called regularly to check for screen changes
     */
    void update();
    
    /**
     * Notify that a screen change is about to happen
     * @param newScreen The screen that will be loaded
     */
    void notifyScreenChange(lv_obj_t* newScreen);

private:
    ScreenManager() = default;
    ~ScreenManager() = default;
    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;
    
    std::unordered_map<lv_obj_t*, ScreenCallback> loadCallbacks;
    std::unordered_map<lv_obj_t*, ScreenCallback> unloadCallbacks;
    lv_obj_t* currentScreen = nullptr;
    bool initialized = false;
};

} // namespace UI
} // namespace Application