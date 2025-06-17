#ifndef MESSAGE_HANDLER_REGISTRY_H
#define MESSAGE_HANDLER_REGISTRY_H

#include <vector>
#include "MessageBus.h"
#include "Messages.h"

namespace Messaging {

class MessageHandlerRegistry {
public:
    // Register all message handlers used by the application
    static bool RegisterAllHandlers();
    
    // Unregister all handlers (for cleanup)
    static void UnregisterAllHandlers();
    
    // Individual handler registration methods
    static bool RegisterAudioStatusHandlers();
    
private:
    // Individual typed message handlers
    static void HandleAudioStatusResponse(const Messages::AudioStatusResponse& message);
    
    // Keep track of registered handler identifiers for cleanup
    static std::vector<String> registeredHandlers;
};

} // namespace Messaging

#endif // MESSAGE_HANDLER_REGISTRY_H 