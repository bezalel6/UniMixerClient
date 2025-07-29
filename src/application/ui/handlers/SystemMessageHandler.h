#ifndef SYSTEM_MESSAGE_HANDLER_H
#define SYSTEM_MESSAGE_HANDLER_H

#include "../LVGLMessageHandler.h"
#include "../system/SystemStateOverlay.h"
#include "../../system/SDCardOperations.h"
#include <lvgl.h>

namespace Application {
namespace UI {
namespace Handlers {

using Application::LVGLMessageHandler::LVGLMessage_t;

/**
 * SystemMessageHandler - Handles all system and debug-related UI messages
 * 
 * This handler manages system state overlay, SD card operations,
 * and debug information updates.
 */
class SystemMessageHandler {
public:
    // Register this handler with the message system
    static void registerHandler();
    
    // Message handlers
    static void handleShowStateOverview(const LVGLMessage_t* msg);
    static void handleUpdateStateOverview(const LVGLMessage_t* msg);
    static void handleHideStateOverview(const LVGLMessage_t* msg);
    static void handleSDStatus(const LVGLMessage_t* msg);
    static void handleFormatSDRequest(const LVGLMessage_t* msg);
    static void handleFormatSDConfirm(const LVGLMessage_t* msg);
    static void handleFormatSDProgress(const LVGLMessage_t* msg);
    static void handleFormatSDComplete(const LVGLMessage_t* msg);
    
private:
    // Helper to convert message data to overlay state data
    static System::SystemStateOverlay::StateData convertToStateData(const LVGLMessage_t* msg);
};

} // namespace Handlers
} // namespace UI
} // namespace Application

#endif // SYSTEM_MESSAGE_HANDLER_H