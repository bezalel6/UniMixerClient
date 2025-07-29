#ifndef MESSAGE_HANDLER_REGISTRY_H
#define MESSAGE_HANDLER_REGISTRY_H

#include "LVGLMessageHandler.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace Application {
namespace UI {

using Application::LVGLMessageHandler::LVGLMessage_t;
using Application::LVGLMessageHandler::LVGLMessageType_t;

/**
 * MessageHandlerRegistry - Central registry for all message handlers
 * 
 * This registry provides a centralized location for registering and dispatching
 * message handlers. It supports auto-registration and provides O(1) lookup
 * performance for message routing.
 */
class MessageHandlerRegistry {
public:
    // Message handler callback type
    using MessageHandler = std::function<void(const LVGLMessage_t*)>;
    
    // Get singleton instance
    static MessageHandlerRegistry& getInstance();
    
    // Register a handler for a specific message type
    void registerHandler(LVGLMessageType_t type, MessageHandler handler);
    
    // Register multiple handlers at once
    void registerHandlers(const std::vector<std::pair<LVGLMessageType_t, MessageHandler>>& handlers);
    
    // Dispatch a message to its registered handler
    bool dispatch(const LVGLMessage_t* message);
    
    // Initialize all handlers
    void initializeAllHandlers();
    
    // Check if a handler is registered for a message type
    bool hasHandler(LVGLMessageType_t type) const;
    
    // Get handler count for debugging
    size_t getHandlerCount() const { return handlers.size(); }
    
    // Get message type name for debugging
    static const char* getMessageTypeName(int messageType);

private:
    MessageHandlerRegistry() = default;
    ~MessageHandlerRegistry() = default;
    MessageHandlerRegistry(const MessageHandlerRegistry&) = delete;
    MessageHandlerRegistry& operator=(const MessageHandlerRegistry&) = delete;
    
    // Handler storage
    std::unordered_map<int, MessageHandler> handlers;
    
    // Message type names for debugging
    static const char* messageTypeNames[];
    
    // Initialization flag
    bool initialized = false;
};

} // namespace UI
} // namespace Application

#endif // MESSAGE_HANDLER_REGISTRY_H