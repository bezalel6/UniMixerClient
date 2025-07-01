#pragma once

#include "MessageCore.h"
#include "MessageData.h"
#include "MessageConfig.h"

namespace Messaging {

/**
 * Clean API interface for the messageType-based messaging system
 *
 * This is the ONLY header that application components should include.
 * It provides a simple, stable interface while hiding implementation details.
 *
 * Usage example:
 *
 *   // Initialize
 *   MessageAPI::init();
 *
 *   // Subscribe to specific message types
 *   MessageAPI::subscribeToType("StatusUpdate", [](const Message& msg) {
 *       // Handle status update
 *   });
 *
 *   // Subscribe to audio updates (specialized)
 *   MessageAPI::onAudioStatus([](const AudioStatusData& data) {
 *       // Handle audio data
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

    /**
     * Register MQTT transport (simplified - no topics)
     */
    static void registerMqttTransport(
        std::function<bool(const String& payload)> sendFunction,
        std::function<bool()> isConnectedFunction,
        std::function<void()> updateFunction = nullptr,
        std::function<String()> getStatusFunction = nullptr) {
        TransportInterface transport;
        transport.send = sendFunction;
        transport.isConnected = isConnectedFunction;
        transport.update = updateFunction;
        transport.getStatus = getStatusFunction;

        MessageCore::getInstance().registerTransport(Config::TRANSPORT_NAME_MQTT, transport);
    }

    /**
     * Register Serial transport (simplified - no topics)
     */
    static void registerSerialTransport(
        std::function<bool(const String& payload)> sendFunction,
        std::function<bool()> isConnectedFunction,
        std::function<void()> updateFunction = nullptr) {
        TransportInterface transport;
        transport.send = sendFunction;
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
    // NEW DUAL MESSAGE TYPE SYSTEM - External/Internal Separation
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
    // LEGACY MESSAGE HANDLING (Deprecated)
    // =============================================================================

    /**
     * @deprecated Use subscribeToExternal or subscribeToInternal instead
     * Subscribe to messages by messageType
     */
    [[deprecated("Use subscribeToExternal or subscribeToInternal instead")]]
    static void subscribeToType(const String& messageType, MessageCallback callback) {
        MessageCore::getInstance().subscribeToType(messageType, callback);
    }

    /**
     * Subscribe to all messages (wildcard)
     */
    static void subscribeToAll(MessageCallback callback) {
        MessageCore::getInstance().subscribeToAll(callback);
    }

    /**
     * Unsubscribe from a messageType
     */
    static void unsubscribeFromType(const String& messageType) {
        MessageCore::getInstance().unsubscribeFromType(messageType);
    }

    /**
     * Publish a message object
     */
    static bool publish(const Message& message) {
        return MessageCore::getInstance().publish(message);
    }

    /**
     * Publish raw JSON payload (messageType will be extracted)
     */
    static bool publish(const String& jsonPayload) {
        return MessageCore::getInstance().publish(jsonPayload);
    }

    /**
     * Create and publish a message
     */
    static bool publishMessage(const String& messageType, const String& jsonPayload) {
        return MessageCore::getInstance().publishMessage(messageType, jsonPayload);
    }

    /**
     * Handle incoming message from external source (e.g., MQTT callback, Serial)
     */
    static void handleIncomingMessage(const String& jsonPayload) {
        MessageCore::getInstance().handleIncomingMessage(jsonPayload);
    }

    // =============================================================================
    // AUDIO MESSAGING (Specialized)
    // =============================================================================

    /**
     * Request current audio status from the PC
     */
    static bool requestAudioStatus() {
        return MessageCore::getInstance().requestAudioStatus();
    }

    /**
     * Send audio control command to PC
     */
    static bool sendAudioCommand(const String& command, const String& target = "", int value = -1) {
        JsonDocument doc;
        doc["messageType"] = command;
        doc["requestId"] = Config::generateRequestId();
        doc["deviceId"] = Config::DEVICE_ID;

        if (!target.isEmpty()) {
            doc["target"] = target;
        }

        if (value >= 0) {
            doc["value"] = value;
        }

        String payload;
        serializeJson(doc, payload);

        return MessageCore::getInstance().publish(payload);
    }

    /**
     * Set volume for a specific process
     */
    static bool setProcessVolume(const String& processName, int volume) {
        return sendAudioCommand(Config::MESSAGE_TYPE_SET_VOLUME, processName, volume);
    }

    /**
     * Mute/unmute a specific process
     */
    static bool setProcessMute(const String& processName, bool muted) {
        return sendAudioCommand(muted ? Config::MESSAGE_TYPE_MUTE_PROCESS : Config::MESSAGE_TYPE_UNMUTE_PROCESS, processName);
    }

    /**
     * Set master volume
     */
    static bool setMasterVolume(int volume) {
        return sendAudioCommand(Config::MESSAGE_TYPE_SET_MASTER_VOLUME, "", volume);
    }

    /**
     * Mute/unmute master
     */
    static bool setMasterMute(bool muted) {
        return sendAudioCommand(muted ? Config::MESSAGE_TYPE_MUTE_MASTER : Config::MESSAGE_TYPE_UNMUTE_MASTER);
    }

    // =============================================================================
    // UTILITIES
    // =============================================================================

    /**
     * Get subscription count
     */
    static size_t getSubscriptionCount() {
        return MessageCore::getInstance().getSubscriptionCount();
    }

    /**
     * Get transport count
     */
    static size_t getTransportCount() {
        return MessageCore::getInstance().getTransportCount();
    }

    /**
     * Create message object
     */
    static Message createMessage(const String& messageType, const String& jsonPayload) {
        return Message(messageType, jsonPayload);
    }

    /**
     * Parse JSON safely to Message object
     */
    static Message parseMessage(const String& jsonPayload) {
        return MessageParser::parseMessage(jsonPayload);
    }

    /**
     * Parse audio status from Message
     */
    static AudioStatusData parseAudioStatus(const Message& message) {
        return Json::parseStatusResponse(message);
    }

    /**
     * Check if message should be ignored
     */
    static bool shouldIgnoreMessage(const Message& message) {
        return MessageParser::shouldIgnoreMessage(message);
    }

   private:
    // Static-only class
    MessageAPI() = delete;
    ~MessageAPI() = delete;
    MessageAPI(const MessageAPI&) = delete;
    MessageAPI& operator=(const MessageAPI&) = delete;
};

}  // namespace Messaging
