#ifndef UI_MESSAGE_HANDLER_H
#define UI_MESSAGE_HANDLER_H

#include "../LVGLMessageHandler.h"
#include <lvgl.h>

namespace Application {
namespace UI {
namespace Handlers {

using Application::LVGLMessageHandler::LVGLMessage_t;

/**
 * UIMessageHandler - Handles general UI-related messages
 * 
 * This handler manages FPS display, build time display, screen changes,
 * and general UI updates.
 */
class UIMessageHandler {
public:
    // Register this handler with the message system
    static void registerHandler();
    
    // Message handlers
    static void handleFpsDisplay(const LVGLMessage_t* msg);
    static void handleBuildTimeDisplay(const LVGLMessage_t* msg);
    static void handleScreenChange(const LVGLMessage_t* msg);
    static void handleRequestData(const LVGLMessage_t* msg);
    
private:
    // Helper to get build time string
    static const char* getBuildTimeAndDate();
};

} // namespace Handlers
} // namespace UI
} // namespace Application

#endif // UI_MESSAGE_HANDLER_H