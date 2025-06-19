#pragma once

#include "MessageCore.h"
#include "MessageData.h"
#include "MessageConfig.h"

namespace Messaging {

/**
 * Clean API interface for the messaging system
 *
 * This is the ONLY header that application components should include.
 * It provides a simple, stable interface while hiding implementation details.
 *
 * Usage example:
 *
 *   // Initialize
 *   MessageAPI::init();
 *
 *   // Subscribe to audio updates
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
     * Register MQTT transport
     */
    static void registerMqttTransport(
        std::function<bool(const String& topic, const String& payload)> sendFunction,
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
     * Register Serial transport
     */
    static void registerSerialTransport(
        std::function<bool(const String& topic, const String& payload)> sendFunction,
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
    // AUDIO MESSAGING (Simplified)
    // =============================================================================

    /**
     * Subscribe to audio status updates
     */
    static void onAudioStatus(AudioStatusCallback callback) {
        MessageCore::getInstance().subscribeToAudioStatus(callback);
    }

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

        return MessageCore::getInstance().publish(Config::TOPIC_AUDIO_CONTROL, payload);
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
    // GENERIC MESSAGING
    // =============================================================================

    /**
     * Subscribe to messages on a topic
     */
    static void subscribe(const String& topic, MessageCallback callback) {
        MessageCore::getInstance().subscribe(topic, callback);
    }

    /**
     * Subscribe to all messages (wildcard)
     */
    static void subscribeToAll(MessageCallback callback) {
        MessageCore::getInstance().subscribe(Config::TOPIC_WILDCARD, callback);
    }

    /**
     * Unsubscribe from a topic
     */
    static void unsubscribe(const String& topic) {
        MessageCore::getInstance().unsubscribe(topic);
    }

    /**
     * Publish a message
     */
    static bool publish(const String& topic, const String& payload) {
        return MessageCore::getInstance().publish(topic, payload);
    }

    /**
     * Handle incoming message from external source (e.g., MQTT callback)
     */
    static void handleIncomingMessage(const String& topic, const String& payload) {
        MessageCore::getInstance().handleIncomingMessage(topic, payload);
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
     * Create simple message
     */
    static Message createMessage(const String& topic, const String& payload) {
        return Message(topic, payload);
    }

    /**
     * Parse JSON safely
     */
    static AudioStatusData parseAudioStatus(const String& jsonString) {
        return Json::parseStatusResponse(jsonString);
    }

    /**
     * Check if message should be ignored
     */
    static bool shouldIgnoreMessage(const String& jsonString) {
        return Json::shouldIgnoreMessage(jsonString);
    }

   private:
    // Static-only class
    MessageAPI() = delete;
    ~MessageAPI() = delete;
    MessageAPI(const MessageAPI&) = delete;
    MessageAPI& operator=(const MessageAPI&) = delete;
};

}  // namespace Messaging