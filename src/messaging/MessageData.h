#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include <esp_log.h>
#include "../application/AudioData.h"
#include "MessageConfig.h"
#include <MessageProtocol.h>  // Direct import instead of relative path

namespace Messaging {

// =============================================================================
// AUDIO DATA STRUCTURES
// =============================================================================

/**
 * Audio status data - simplified from complex AudioStatusResponse
 */
struct AudioStatusData {
    std::vector<Application::Audio::AudioLevel> audioLevels;
    Application::Audio::AudioLevel defaultDevice;
    bool hasDefaultDevice = false;
    unsigned long timestamp = 0;
    String reason;
    String originatingDeviceId;

    void clear() {
        audioLevels.clear();
        defaultDevice = Application::Audio::AudioLevel();
        hasDefaultDevice = false;
        timestamp = 0;
        reason = "";
        originatingDeviceId = "";
    }

    bool isEmpty() const {
        return audioLevels.empty() && !hasDefaultDevice;
    }
};

/**
 * Audio device data for device lists
 */
struct AudioDeviceData {
    String deviceId;
    String friendlyName;
    String state;
    bool isDefault = false;

    AudioDeviceData() = default;
    AudioDeviceData(const String& id, const String& name, const String& deviceState = "Active")
        : deviceId(id), friendlyName(name), state(deviceState) {}
};

// =============================================================================
// EXTERNAL MESSAGE TYPES - For messages received over transports
// =============================================================================

/**
 * EXTERNAL MESSAGE - Received over Serial/MQTT/Network
 * EFFICIENT: Pre-parsed by transport, no raw payload storage
 * SECURITY: Validation and sanitization required
 */
struct ExternalMessage {
    MessageProtocol::ExternalMessageType messageType;
    String requestId;
    String deviceId;
    String originatingDeviceId;
    unsigned long timestamp;
    bool validated = false;

    // Type-specific parsed data (transport provides this)
    JsonDocument parsedData;  // Only the specific data fields, not entire payload

    ExternalMessage() {
        messageType = MessageProtocol::ExternalMessageType::INVALID;
        timestamp = millis();
    }

    ExternalMessage(MessageProtocol::ExternalMessageType type, const String& reqId = "", const String& devId = "")
        : messageType(type), requestId(reqId), deviceId(devId) {
        timestamp = millis();
    }

    // Direct access to parsed data (no lazy loading needed)
    template <typename T>
    T get(const String& field, T defaultValue = T{}) const {
        return parsedData[field] | defaultValue;
    }

    // Validation and security (no JSON parsing overhead)
    bool validate();
    bool isSelfOriginated() const;

    // Utility methods for external message handling
    bool requiresResponse() const;
    MessageProtocol::ExternalMessageCategory getCategory() const {
        return MessageProtocol::getExternalMessageCategory(messageType);
    }
    MessageProtocol::MessagePriority getPriority() const {
        return MessageProtocol::getExternalMessagePriority(messageType);
    }
};

// =============================================================================
// INTERNAL MESSAGE TYPES - For ESP32 internal communication
// =============================================================================

/**
 * INTERNAL MESSAGE - ESP32 internal communication
 * Lightweight, zero-cost abstractions, maximum performance
 * CORE-AWARE: Smart routing between Core 0 and Core 1
 */
struct InternalMessage {
    MessageProtocol::InternalMessageType messageType;
    void* data = nullptr;  // Type-safe data payload
    size_t dataSize = 0;
    unsigned long timestamp;
    uint8_t priority;  // For Core 1 processing queue
    bool requiresResponse = false;

    InternalMessage() {
        messageType = MessageProtocol::InternalMessageType::INVALID;
        timestamp = millis();
        priority = static_cast<uint8_t>(MessageProtocol::getInternalMessagePriority(messageType));
    }

    InternalMessage(MessageProtocol::InternalMessageType type, void* payload = nullptr, size_t size = 0)
        : messageType(type), data(payload), dataSize(size) {
        timestamp = millis();
        priority = static_cast<uint8_t>(MessageProtocol::getInternalMessagePriority(type));
    }

    // Type-safe data accessors
    template <typename T>
    T* getTypedData() const {
        return static_cast<T*>(data);
    }

    template <typename T>
    bool setTypedData(const T& payload) {
        static T staticPayload = payload;  // Simple static storage
        data = &staticPayload;
        dataSize = sizeof(T);
        return true;
    }

    // Core routing decision
    bool shouldRouteToCore1() const {
        return MessageProtocol::shouldRouteToCore1(messageType);
    }

    // Utility methods for internal message handling
    MessageProtocol::InternalMessageCategory getCategory() const {
        return MessageProtocol::getInternalMessageCategory(messageType);
    }
    MessageProtocol::MessagePriority getPriority() const {
        return MessageProtocol::getInternalMessagePriority(messageType);
    }
};

// =============================================================================
// MESSAGE CONVERSION UTILITIES
// =============================================================================

namespace MessageConverter {

/**
 * Convert validated ExternalMessage to InternalMessage(s)
 * One external message might generate multiple internal messages
 */
std::vector<InternalMessage> externalToInternal(const ExternalMessage& external);

/**
 * Convert InternalMessage to ExternalMessage for transmission
 * Used when ESP32 needs to send messages to external systems
 */
ExternalMessage internalToExternal(const InternalMessage& internal);

/**
 * Create InternalMessage for common operations (convenience functions)
 */
InternalMessage createAudioVolumeMessage(const String& processName, int volume);
InternalMessage createUIUpdateMessage(const String& component, const String& data);
InternalMessage createSystemStatusMessage(const String& status);

// New convenience functions for specific internal message types
InternalMessage createWifiStatusMessage(const String& status, bool connected);
InternalMessage createNetworkInfoMessage(const String& ssid, const String& ip);
InternalMessage createSDStatusMessage(const String& status, bool mounted);
InternalMessage createAudioDeviceChangeMessage(const String& deviceName);
InternalMessage createCoreToCoreSyncMessage(uint8_t fromCore, uint8_t toCore);

}  // namespace MessageConverter

// =============================================================================
// LEGACY COMPATIBILITY - WILL BE REMOVED
// =============================================================================

/**
 * LEGACY: Original Message struct for backward compatibility
 * This will be removed once all code is migrated
 * @deprecated Use ExternalMessage or InternalMessage instead
 */
struct Message {
    MessageProtocol::MessageType messageType;  // LEGACY: Unified enum for compatibility
    String payload;                            // Raw JSON string
    String requestId;
    String deviceId;
    unsigned long timestamp;

    // Parsed content (populated on demand)
    mutable JsonDocument parsedContent;
    mutable bool contentParsed = false;

    Message() {
        messageType = MessageProtocol::MessageType::INVALID;
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    Message(MessageProtocol::MessageType type, const String& data) : messageType(type), payload(data) {
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    // LEGACY: String-based constructor for compatibility
    [[deprecated("Use Message(MessageProtocol::MessageType, String) instead")]]
    Message(const String& messageTypeStr, const String& data) : payload(data) {
        messageType = MessageProtocol::stringToMessageType(messageTypeStr);
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    // Convert to new message types for migration
    ExternalMessage toExternalMessage() const;
    InternalMessage toInternalMessage() const;

    // Legacy methods with deprecation warnings
    [[deprecated("Use get() on ExternalMessage instead")]]
    const String& get(const String& field) const;

    [[deprecated("Use validate() on ExternalMessage instead")]]
    bool isValid() const;

    [[deprecated("Use getCategory() on new message types instead")]]
    String getCategory() const;

    [[deprecated("Use shouldRouteToCore1() on InternalMessage instead")]]
    bool shouldRouteToCore1() const;

   private:
    void ensureParsed() const;
};

// =============================================================================
// NAMESPACE ALIASES FOR CONVENIENCE
// =============================================================================

// Short aliases for common types to reduce typing
using ExtMsg = ExternalMessage;
using IntMsg = InternalMessage;
using ExtMsgType = MessageProtocol::ExternalMessageType;
using IntMsgType = MessageProtocol::InternalMessageType;

// =============================================================================
// MESSAGE PARSING UTILITIES
// =============================================================================

namespace MessageParser {

/**
 * Parse external message type from raw JSON string
 */
inline MessageProtocol::ExternalMessageType parseExternalMessageType(const String& jsonPayload) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return MessageProtocol::ExternalMessageType::INVALID;
    }
    String typeStr = doc["messageType"] | "";
    return MessageProtocol::stringToExternalMessageType(typeStr);
}

/**
 * Create ExternalMessage object from raw JSON payload - SECURE PARSING
 */
inline ExternalMessage parseExternalMessage(const String& jsonPayload) {
    MessageProtocol::ExternalMessageType type = parseExternalMessageType(jsonPayload);
    ExternalMessage message(type);

    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) == DeserializationError::Ok) {
        message.requestId = doc["requestId"] | "";
        message.deviceId = doc["deviceId"] | "";
        message.originatingDeviceId = doc["originatingDeviceId"] | "";
        message.timestamp = doc["timestamp"] | millis();

        // Copy parsed data (everything except metadata)
        message.parsedData = doc["data"];
    }

    message.validate();  // Security validation
    return message;
}

/**
 * LEGACY: Parse messageType from raw JSON string - RETURNS LEGACY ENUM
 * @deprecated Use parseExternalMessageType instead
 */
[[deprecated("Use parseExternalMessageType() instead")]]
inline MessageProtocol::MessageType parseMessageType(const String& jsonPayload) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return MessageProtocol::MessageType::INVALID;
    }
    String typeStr = doc["messageType"] | "";
    return MessageProtocol::stringToMessageType(typeStr);
}

/**
 * LEGACY: Create Message object from raw JSON payload - ENUM OPTIMIZED
 * @deprecated Use parseExternalMessage instead
 */
[[deprecated("Use parseExternalMessage() instead")]]
inline Message parseMessage(const String& jsonPayload) {
    Message message;
    message.payload = jsonPayload;

    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload.c_str()) == DeserializationError::Ok) {
        String typeStr = doc["messageType"] | "";
        message.messageType = MessageProtocol::stringToMessageType(typeStr);
        message.requestId = doc["requestId"] | "";
        message.deviceId = doc["deviceId"] | "";
        message.timestamp = doc["timestamp"] | millis();
    }

    return message;
}

/**
 * Check if external message should be ignored (self-originated)
 */
inline bool shouldIgnoreMessage(const ExternalMessage& message, const String& myDeviceId = Config::DEVICE_ID) {
    return message.isSelfOriginated();
}

/**
 * LEGACY: Check if message should be ignored (self-originated)
 * @deprecated Use shouldIgnoreMessage(ExternalMessage) instead
 */
[[deprecated("Use shouldIgnoreMessage(ExternalMessage) instead")]]
inline bool shouldIgnoreMessage(const Message& message, const String& myDeviceId = Config::DEVICE_ID) {
    return message.deviceId == myDeviceId;
}

}  // namespace MessageParser

// =============================================================================
// JSON UTILITIES - Type-Safe Serialization
// =============================================================================

namespace Json {

/**
 * Serialize ExternalMessage to JSON for transport
 */
String serializeExternalMessage(const ExternalMessage& message);

/**
 * Serialize InternalMessage to JSON for debugging
 */
String serializeInternalMessage(const InternalMessage& message);

/**
 * PERFORMANCE: Parse audio status response from external message
 */
inline AudioStatusData parseStatusResponse(const ExternalMessage& message) {
    AudioStatusData data;

    // Use the parsedData from the external message
    const JsonDocument& doc = message.parsedData;

    data.timestamp = doc["timestamp"] | millis();
    data.reason = doc["reason"] | "";
    data.originatingDeviceId = doc["originatingDeviceId"] | "";

    // Parse default device
    if (doc["defaultDevice"].is<JsonObject>()) {
        JsonObject defaultDev = doc["defaultDevice"];
        data.defaultDevice.processName = defaultDev["processName"] | "";
        data.defaultDevice.friendlyName = defaultDev["friendlyName"] | "";
        data.defaultDevice.volume = defaultDev["volume"] | 0;
        data.defaultDevice.isMuted = defaultDev["isMuted"] | false;
        data.hasDefaultDevice = true;
    }

    // Parse audio levels array
    if (doc["audioLevels"].is<JsonArray>()) {
        JsonArray levels = doc["audioLevels"];
        for (JsonObject levelObj : levels) {
            Application::Audio::AudioLevel level;
            level.processName = levelObj["processName"] | "";
            level.friendlyName = levelObj["friendlyName"] | "";
            level.volume = levelObj["volume"] | 0;
            level.isMuted = levelObj["isMuted"] | false;
            level.lastUpdate = millis();
            level.stale = false;
            data.audioLevels.push_back(level);
        }
    }

    return data;
}

/**
 * PERFORMANCE: Parse device list response from external message
 */
std::vector<AudioDeviceData> parseDeviceListResponse(const ExternalMessage& message);

/**
 * Create JSON status response from audio status data
 */
inline String createStatusResponse(const AudioStatusData& data) {
    JsonDocument doc;

    doc["messageType"] = MessageProtocol::externalMessageTypeToString(MessageProtocol::ExternalMessageType::STATUS_UPDATE);
    doc["timestamp"] = data.timestamp;
    doc["reason"] = data.reason;
    doc["originatingDeviceId"] = data.originatingDeviceId;

    // Default device
    if (data.hasDefaultDevice) {
        JsonObject defaultDev = doc["defaultDevice"].to<JsonObject>();
        defaultDev["processName"] = data.defaultDevice.processName;
        defaultDev["friendlyName"] = data.defaultDevice.friendlyName;
        defaultDev["volume"] = data.defaultDevice.volume;
        defaultDev["isMuted"] = data.defaultDevice.isMuted;
    }

    // Audio levels array
    JsonArray levels = doc["audioLevels"].to<JsonArray>();
    for (const auto& level : data.audioLevels) {
        JsonObject levelObj = levels.add<JsonObject>();
        levelObj["processName"] = level.processName;
        levelObj["friendlyName"] = level.friendlyName;
        levelObj["volume"] = level.volume;
        levelObj["isMuted"] = level.isMuted;
    }

    String result;
    serializeJson(doc, result);
    return result;
}

/**
 * LEGACY: JSON utilities for backward compatibility
 * @deprecated Use type-specific functions instead
 */
[[deprecated("Use serializeExternalMessage or serializeInternalMessage instead")]]
String serialize(const Message& message);

[[deprecated("Use parseStatusResponse(ExternalMessage) instead")]]
AudioStatusData parseStatusResponse(const Message& message);

[[deprecated("Use parseDeviceListResponse(ExternalMessage) instead")]]
std::vector<AudioDeviceData> parseDeviceListResponse(const Message& message);

}  // namespace Json

// =============================================================================
// TRANSPORT INTERFACE - Simplified for Dual Architecture
// =============================================================================

/**
 * Simplified transport interface - handles external messages only
 * Internal messages never cross transport boundaries
 */
struct TransportInterface {
    std::function<bool(const ExternalMessage& message)> send;  // Type-safe external message sending
    std::function<bool()> isConnected;
    std::function<void()> update;
    std::function<String()> getStatus;
    std::function<bool()> init;
    std::function<void()> deinit;

    // Legacy support for raw payload sending (deprecated)
    [[deprecated("Use send(ExternalMessage) instead")]]
    std::function<bool(const String& payload)> sendRaw;
};

// =============================================================================
// CALLBACK TYPES - Type-Safe Message Handling
// =============================================================================

using ExternalMessageCallback = std::function<void(const ExternalMessage& message)>;
using InternalMessageCallback = std::function<void(const InternalMessage& message)>;

// Specific callback types for better type safety
using AudioStatusCallback = std::function<void(const AudioStatusData& data)>;
using NetworkStatusCallback = std::function<void(const String& status, bool connected)>;
using SDStatusCallback = std::function<void(const String& status, bool mounted)>;

// LEGACY COMPATIBILITY - Will be removed
//[[deprecated("Use ExternalMessageCallback or InternalMessageCallback instead")]]
using MessageCallback = std::function<void(const Message& message)>;

}  // namespace Messaging
