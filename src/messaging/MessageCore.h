#pragma once

#include "MessageData.h"
#include <map>
#include <functional>

namespace Messaging {

/**
 * Core messaging system - messageType-based routing
 *
 * Design principles:
 * - Route messages based on messageType field in JSON
 * - Work with Message objects, not raw strings
 * - Simple subscribe/publish pattern by message type
 * - Transport-agnostic (transports just send/receive strings)
 */
class MessageCore {
   public:
    // =============================================================================
    // CORE INTERFACE
    // =============================================================================

    static MessageCore& getInstance();

    /**
     * Initialize the messaging system
     */
    bool init();

    /**
     * Cleanup and shutdown
     */
    void deinit();

    /**
     * Process pending messages and update transports
     */
    void update();

    // =============================================================================
    // TRANSPORT MANAGEMENT
    // =============================================================================

    /**
     * Register a transport (MQTT, Serial, etc.)
     */
    void registerTransport(const String& name, TransportInterface transport);

    /**
     * Remove a transport
     */
    void unregisterTransport(const String& name);

    /**
     * Get transport status
     */
    String getTransportStatus() const;

    // =============================================================================
    // MESSAGE HANDLING (Type-Based)
    // =============================================================================

    /**
     * Subscribe to messages by messageType
     */
    void subscribeToType(const String& messageType, MessageCallback callback);

    /**
     * Subscribe to all messages (wildcard)
     */
    void subscribeToAll(MessageCallback callback);

    /**
     * Unsubscribe from a messageType
     */
    void unsubscribeFromType(const String& messageType);

    /**
     * Publish a message object to all subscribers and transports
     */
    bool publish(const Message& message);

    /**
     * Publish raw JSON payload (will be parsed to extract messageType)
     */
    bool publish(const String& jsonPayload);

    /**
     * Create and publish a message
     */
    bool publishMessage(const String& messageType, const String& jsonPayload);

    /**
     * Publish audio status request
     */
    bool requestAudioStatus();

    /**
     * Handle incoming raw JSON message from transport
     */
    void handleIncomingMessage(const String& jsonPayload);

    // =============================================================================
    // STATUS & DIAGNOSTICS
    // =============================================================================

    /**
     * Get number of active subscriptions
     */
    size_t getSubscriptionCount() const;

    /**
     * Get number of registered transports
     */
    size_t getTransportCount() const;

    /**
     * Check if system is initialized and healthy
     */
    bool isHealthy() const;

    /**
     * Get detailed status information
     */
    String getStatusInfo() const;

   private:
    MessageCore() = default;
    ~MessageCore() = default;
    MessageCore(const MessageCore&) = delete;
    MessageCore& operator=(const MessageCore&) = delete;

    // =============================================================================
    // INTERNAL STATE
    // =============================================================================

    bool initialized = false;

    // Type-based subscriptions
    std::map<String, std::vector<MessageCallback>> typeSubscriptions;
    std::vector<MessageCallback> wildcardSubscribers;

    // Transport management
    std::map<String, TransportInterface> transports;

    // Message statistics
    unsigned long messagesPublished = 0;
    unsigned long messagesReceived = 0;
    unsigned long lastActivityTime = 0;

    // =============================================================================
    // INTERNAL HELPERS
    // =============================================================================

    /**
     * Update last activity timestamp
     */
    void updateActivity();

    /**
     * Log message for debugging
     */
    void logMessage(const String& direction, const Message& message);
};

}  // namespace Messaging
