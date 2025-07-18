#pragma once

#include <lvgl.h>

namespace Application {
namespace UI {
namespace Widgets {

/**
 * @brief Handler for Logo Viewer screen events
 * 
 * This class manages the dynamic addition of the logo browser widget
 * to the Logo Viewer screen without modifying SquareLine generated files.
 */
class LogoViewerScreenHandler {
public:
    /**
     * Initialize the screen handler and register for screen events
     */
    static void init();
    
    /**
     * Cleanup the screen handler
     */
    static void cleanup();
    
    /**
     * Check if a screen is the Logo Viewer screen and initialize if needed
     * @param screen The screen to check
     */
    static void checkAndInitializeScreen(lv_obj_t* screen);

private:
    static lv_obj_t* logoBrowserInstance;
    static lv_obj_t* titleLabel;
    static bool initialized;
    
    /**
     * Initialize the logo browser on the Logo Viewer screen
     * @param screen The Logo Viewer screen
     */
    static void initializeLogoBrowser(lv_obj_t* screen);
    
    /**
     * Clean up the logo browser when leaving the screen
     */
    static void cleanupLogoBrowser();
};

} // namespace Widgets
} // namespace UI
} // namespace Application