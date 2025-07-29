#ifndef DEVICE_MESSAGE_HANDLER_H
#define DEVICE_MESSAGE_HANDLER_H

#include "../LVGLMessageHandler.h"
#include <lvgl.h>
#include <cstring>

namespace Application {
namespace UI {
namespace Handlers {

using Application::LVGLMessageHandler::LVGLMessage_t;

/**
 * DeviceMessageHandler - Handles all device-related UI messages
 * 
 * This handler manages device name updates for all tabs (Master, Single, Balance).
 * Provides centralized device selection UI updates.
 */
class DeviceMessageHandler {
public:
    // Register this handler with the message system
    static void registerHandler();
    
    // Message handlers
    static void handleMasterDevice(const LVGLMessage_t* msg);
    static void handleSingleDevice(const LVGLMessage_t* msg);
    static void handleBalanceDevices(const LVGLMessage_t* msg);
    
private:
    // Helper for safe string copying
    static void safeStringCopy(char* dest, const char* src, size_t destSize);
};

} // namespace Handlers
} // namespace UI
} // namespace Application

#endif // DEVICE_MESSAGE_HANDLER_H