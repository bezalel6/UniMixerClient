#pragma once

#include "MessageData.h"
#include <map>
#include <functional>

namespace Messaging {

/**
 * Core messaging system - simplified replacement for MessageBus + MessageHandlerRegistry
 *
 * Design principles:
 * - Single responsibility: route messages between components
 * - Simple interface: subscribe/publish pattern
 * - No complex templates or inheritance
 * - Clear error handling
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
    // MESSAGE HANDLING
    // =============================================================================

    /**
     * Subscribe to messages on a topic
     */
    void subscribe(const String& topic, MessageCallback callback);

    /**
     * Subscribe specifically to audio status updates
     */
    void subscribeToAudioStatus(AudioStatusCallback callback);

    /**
     * Unsubscribe from a topic
     */
    void unsubscribe(const String& topic);

    /**
     * Publish a message to all subscribers and transports
     */
    bool publish(const String& topic, const String& payload);

    /**
     * Publish a message object
     */
    bool publish(const Message& message);

    /**
     * Publish audio status request
     */
    bool requestAudioStatus();

    /**
     * Handle incoming message from transport
     */
    void handleIncomingMessage(const String& topic, const String& payload);

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

    // Topic subscriptions
    std::map<String, std::vector<MessageCallback>> topicSubscriptions;

    // Audio status subscribers
    std::vector<AudioStatusCallback> audioStatusSubscribers;

    // Transport management
    std::map<String, TransportInterface> transports;

    // Message statistics
    unsigned long messagesPublished = 0;
    unsigned long messagesReceived = 0;
    unsigned long lastActivityTime = 0;

    // Logo request debouncing
    std::map<String, unsigned long> lastLogoCheckTime;
    static const unsigned long LOGO_CHECK_DEBOUNCE_MS = 30000;  // 30 seconds between checks for same process

    // =============================================================================
    // INTERNAL HELPERS
    // =============================================================================

    /**
     * Process audio status message
     */
    void processAudioStatusMessage(const String& payload);

    /**
     * Check and request logos for audio processes
     */
    void checkAndRequestLogosForAudioProcesses(const AudioStatusData& statusData);

    /**
     * Check logo for a single process and request if needed
     */
    void checkSingleProcessLogo(const char* processName);

    /**
     * Update activity timestamp
     */
    void updateActivity();

    /**
     * Log message activity
     */
    void logMessage(const String& direction, const String& topic, const String& payload);
};

}  // namespace Messaging
