#pragma once

#include "../protocol/MessageData.h"
#include <MessageProtocol.h>  // Direct import instead of relative path
#include <map>
#include <unordered_map>
#include <functional>

namespace Messaging {

/**
 * Core messaging system - DUAL ARCHITECTURE
 *
 * EXTERNAL MESSAGES: Full parsing, validation, security (Core 1 processing)
 * INTERNAL MESSAGES: Lightweight, zero-cost routing (Core-aware routing)
 *
 * Design principles:
 * - External messages: Security first, route to Core 1 for processing
 * - Internal messages: Performance first, smart core routing
 * - Clear separation between external input and internal communication
 * - Enable Core 1 communications engine for external message processing
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
    // TRANSPORT MANAGEMENT (External Messages)
    // =============================================================================

    /**
     * Register a transport (Serial for normal mode, network transports for OTA mode)
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
    // EXTERNAL MESSAGE HANDLING (From Transports)
    // =============================================================================

    /**
     * Handle incoming parsed message from external transport
     * EFFICIENT: Transport has already parsed JSON, just validate and route
     */
    void handleExternalMessage(const ExternalMessage& message);

    /**
     * Publish external message to all transports
     * Used when ESP32 needs to send messages to external systems
     */
    bool publishExternal(const ExternalMessage& message);



    // =============================================================================
    // INTERNAL MESSAGE HANDLING (ESP32 Internal Communication)
    // =============================================================================

    /**
     * Publish internal message with smart core routing
     * PERFORMANCE: Zero-cost enum routing, no JSON parsing
     */
    bool publishInternal(const InternalMessage& message);

    /**
     * Subscribe to internal messages by type - ENUM OPTIMIZED
     */
    void subscribeToInternal(MessageProtocol::InternalMessageType messageType, InternalMessageCallback callback);
    void unsubscribeFromInternal(MessageProtocol::InternalMessageType messageType);

    /**
     * Subscribe to all internal messages (wildcard)
     */
    void subscribeToAllInternal(InternalMessageCallback callback);

    // =============================================================================
    // CONVENIENCE METHODS (Common Operations)
    // =============================================================================

    /**
     * Send audio status request to external system
     */
    bool requestAudioStatus();

    /**
     * Send audio command to external system
     */
    bool sendAudioCommand(MessageProtocol::ExternalMessageType commandType, const String& target = "", int value = -1);

    /**
     * Publish internal UI update
     */
    bool publishUIUpdate(const String& component, const String& data);

    /**
     * Publish internal audio volume update
     */
    bool publishAudioVolumeUpdate(const String& processName, int volume);

    // =============================================================================
    // STATUS & DIAGNOSTICS
    // =============================================================================

    /**
     * Get total number of active subscriptions
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

    // Transport management
    std::map<String, TransportInterface> transports;



    // Internal message subscriptions (by type)
    std::unordered_map<MessageProtocol::InternalMessageType, std::vector<InternalMessageCallback>> internalSubscriptions;

    // Internal wildcard subscribers
    std::vector<InternalMessageCallback> internalWildcardSubscribers;

    // Statistics
    unsigned long externalMessagesReceived = 0;
    unsigned long externalMessagesPublished = 0;
    unsigned long internalMessagesPublished = 0;
    unsigned long invalidMessagesReceived = 0;
    unsigned long lastActivityTime = 0;

    // =============================================================================
    // INTERNAL HELPER METHODS
    // =============================================================================

    /**
     * Convert external message to internal messages and route them
     */
    void convertAndRouteExternal(const ExternalMessage& external);

    /**
     * Route internal message to appropriate core/subscribers
     */
    void routeInternalMessage(const InternalMessage& message);

    /**
     * Update activity timestamp
     */
    void updateActivity();

    /**
     * Log external message for debugging
     */
    void logExternalMessage(const char* direction, const ExternalMessage& message);

    /**
     * Log internal message for debugging
     */
    void logInternalMessage(const char* direction, const InternalMessage& message);
};

}  // namespace Messaging
