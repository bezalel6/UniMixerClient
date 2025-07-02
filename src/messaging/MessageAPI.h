#pragma once

#include "system/MessageCore.h"
#include "protocol/MessageData.h"
#include "protocol/MessageConfig.h"

namespace Messaging {

/**
 * Clean API interface for the dual architecture messaging system
 *
 * This is the ONLY header that application components should include.
 * It provides a simple, stable interface while hiding implementation details.
 *
 * Usage example:
 *
 *   // Initialize
 *   MessageAPI::init();
 *
 *   // Subscribe to external messages (cross-transport boundaries)
 *   MessageAPI::subscribeToExternal(MessageProtocol::ExternalMessageType::STATUS_UPDATE,
 *       [](const ExternalMessage& msg) {
 *           // Handle external status update
 *   });
 *
 *   // Subscribe to internal messages (ESP32 internal communication)
 *   MessageAPI::subscribeToInternal(MessageProtocol::InternalMessageType::UI_UPDATE,
 *       [](const InternalMessage& msg) {
 *           // Handle internal UI update
 *   });
 *
 *   // Request audio status
 *   MessageAPI::requestAudioStatus();
 *
 *   // Update in main loop
 *   MessageAPI::update();
 */
class MessageAPI {
   public:
    // =============================================================================
    // SYSTEM LIFECYCLE
    // =============================================================================

    /**
     * Initialize the messaging system
     */
    static bool init() {
        return MessageCore::getInstance().init();
    }

    /**
     * Shutdown the messaging system
     */
    static void shutdown() {
        MessageCore::getInstance().deinit();
    }

    /**
     * Update the messaging system (call from main loop)
     */
    static void update() {
        MessageCore::getInstance().update();
    }

    /**
     * Check if system is healthy and operational
     */
    static bool isHealthy() {
        return MessageCore::getInstance().isHealthy();
    }

    /**
     * Get detailed status information
     */
    static String getStatus() {
        return MessageCore::getInstance().getStatusInfo();
    }

    // =============================================================================
    // TRANSPORT MANAGEMENT
    // =============================================================================

    // MQTT transport removed - network-free architecture

    /**
     * Register Serial transport
     */
    static void registerSerialTransport(
        std::function<bool(const String& payload)> sendFunction,
        std::function<bool()> isConnectedFunction,
        std::function<void()> updateFunction = nullptr) {
        TransportInterface transport;
        transport.sendRaw = sendFunction;
        transport.isConnected = isConnectedFunction;
        transport.update = updateFunction;

        MessageCore::getInstance().registerTransport(Config::TRANSPORT_NAME_SERIAL, transport);
    }

    /**
     * Remove a transport
     */
    static void unregisterTransport(const String& name) {
        MessageCore::getInstance().unregisterTransport(name);
    }

    /**
     * Get transport status summary
     */
    static String getTransportStatus() {
        return MessageCore::getInstance().getTransportStatus();
    }

    // =============================================================================
    // DUAL MESSAGE TYPE SYSTEM - External/Internal Separation
    // =============================================================================

    /**
     * Subscribe to external messages (cross-transport boundaries)
     */
    static void subscribeToExternal(MessageProtocol::ExternalMessageType messageType,
                                    std::function<void(const ExternalMessage&)> callback) {
        MessageCore::getInstance().subscribeToExternal(messageType, callback);
    }

    /**
     * Subscribe to internal messages (ESP32 internal communication)
     */
    static void subscribeToInternal(MessageProtocol::InternalMessageType messageType,
                                    std::function<void(const InternalMessage&)> callback) {
        MessageCore::getInstance().subscribeToInternal(messageType, callback);
    }

    /**
     * Subscribe to all internal messages (wildcard)
     */
    static void subscribeToAllInternal(std::function<void(const InternalMessage&)> callback) {
        MessageCore::getInstance().subscribeToAllInternal(callback);
    }

    /**
     * Publish external message (cross-transport boundaries)
     */
    static bool publishExternal(const ExternalMessage& message) {
        return MessageCore::getInstance().publishExternal(message);
    }

    /**
     * Publish internal message (ESP32 internal communication)
     */
    static bool publishInternal(const InternalMessage& message) {
        return MessageCore::getInstance().publishInternal(message);
    }

    /**
     * Unsubscribe from external message type
     */
    static void unsubscribeFromExternal(MessageProtocol::ExternalMessageType messageType) {
        MessageCore::getInstance().unsubscribeFromExternal(messageType);
    }

    /**
     * Unsubscribe from internal message type
     */
    static void unsubscribeFromInternal(MessageProtocol::InternalMessageType messageType) {
        MessageCore::getInstance().unsubscribeFromInternal(messageType);
    }

    // =============================================================================
    // CONVENIENCE METHODS
    // =============================================================================

    /**
     * Request audio status from external system
     */
    static bool requestAudioStatus() {
        return MessageCore::getInstance().requestAudioStatus();
    }

    /**
     * Send audio command to external system
     */
    static bool sendAudioCommand(MessageProtocol::ExternalMessageType commandType,
                                 const String& target = "", int value = -1) {
        return MessageCore::getInstance().sendAudioCommand(commandType, target, value);
    }

    /**
     * Publish internal UI update
     */
    static bool publishUIUpdate(const String& component, const String& data) {
        return MessageCore::getInstance().publishUIUpdate(component, data);
    }

    /**
     * Publish internal audio volume update
     */
    static bool publishAudioVolumeUpdate(const String& processName, int volume) {
        return MessageCore::getInstance().publishAudioVolumeUpdate(processName, volume);
    }

    /**
     * Send debug message to UI debug log area
     */
    static bool publishDebugUILog(const String& logMessage) {
        InternalMessage msg = MessageConverter::createDebugUILogMessage(logMessage);
        return MessageCore::getInstance().publishInternal(msg);
    }

    // =============================================================================
    // STATISTICS & DIAGNOSTICS
    // =============================================================================

    /**
     * Get total number of active subscriptions
     */
    static size_t getSubscriptionCount() {
        return MessageCore::getInstance().getSubscriptionCount();
    }

    /**
     * Get number of registered transports
     */
    static size_t getTransportCount() {
        return MessageCore::getInstance().getTransportCount();
    }

    // =============================================================================
    // MESSAGE PARSING UTILITIES
    // =============================================================================

    /**
     * Parse external message from JSON payload
     */
    static ExternalMessage parseExternalMessage(const String& jsonPayload) {
        return MessageParser::parseExternalMessage(jsonPayload);
    }

    /**
     * Parse external message type from JSON payload
     */
    static MessageProtocol::ExternalMessageType parseExternalMessageType(const String& jsonPayload) {
        return MessageParser::parseExternalMessageType(jsonPayload);
    }

    /**
     * Check if external message should be ignored
     */
    static bool shouldIgnoreMessage(const ExternalMessage& message) {
        return MessageParser::shouldIgnoreMessage(message);
    }

    /**
     * Parse audio status response from external message
     */
    static AudioStatusData parseAudioStatus(const ExternalMessage& message) {
        return parseStatusResponse(message);
    }

    /**
     * Create status response JSON from audio status data
     */
    static String createStatusResponse(const AudioStatusData& data) {
        return Messaging::createStatusResponse(data);
    }

   private:
    // Static-only class
    MessageAPI() = delete;
    ~MessageAPI() = delete;
    MessageAPI(const MessageAPI&) = delete;
    MessageAPI& operator=(const MessageAPI&) = delete;
};

}  // namespace Messaging
