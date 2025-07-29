#pragma once

#include <lvgl.h>
#include <vector>
#include <Arduino.h>

namespace Application {
namespace Debug {

/**
 * Simple debug view showing all available logos in a grid
 * - No metadata, just shows PNG files from /logos directory
 * - Click any logo to see its full path
 * - Close button to return to main screen
 */
class LogoDebugView {
public:
    static LogoDebugView& getInstance();
    
    // Show/hide the debug view
    void show();
    void hide();
    bool isVisible() const { return container != nullptr; }
    
private:
    LogoDebugView() = default;
    ~LogoDebugView() = default;
    
    // Prevent copying
    LogoDebugView(const LogoDebugView&) = delete;
    LogoDebugView& operator=(const LogoDebugView&) = delete;
    
    // UI elements
    lv_obj_t* container = nullptr;
    lv_obj_t* background = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* closeBtn = nullptr;
    lv_obj_t* scrollContainer = nullptr;
    lv_obj_t* infoLabel = nullptr;
    
    // Grid settings
    static constexpr int LOGO_SIZE = 80;
    static constexpr int LOGO_SPACING = 10;
    static constexpr int GRID_COLS = 8;  // For 800px width display
    
    // Create UI elements
    void createUI();
    void destroyUI();
    void loadLogos();
    
    // Event handlers
    static void onCloseClicked(lv_event_t* e);
    static void onLogoClicked(lv_event_t* e);
    
    // Helper to create a logo item
    lv_obj_t* createLogoItem(lv_obj_t* parent, const String& filename);
};

} // namespace Debug
} // namespace Application