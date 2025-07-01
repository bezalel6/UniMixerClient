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
 * Default audio device data (matches C# DefaultAudioDevice)
 */
struct DefaultAudioDeviceData {
    String friendlyName;
    float volume = 0.0f;
    bool isMuted = false;
    String dataFlow;    // "Render" or "Capture"
    String deviceRole;  // "Console", "Multimedia", "Communications"

    DefaultAudioDeviceData() = default;
    DefaultAudioDeviceData(const String& name, float vol, bool muted)
        : friendlyName(name), volume(vol), isMuted(muted) {}

    void clear() {
        friendlyName = "";
        volume = 0.0f;
        isMuted = false;
        dataFlow = "";
        deviceRole = "";
    }
};

/**
 * Session status data (matches C# SessionStatus)
 */
struct SessionStatusData {
    int processId = 0;
    String processName;
    String displayName;
    float volume = 0.0f;
    bool isMuted = false;
    String state;

    SessionStatusData() = default;
    SessionStatusData(const String& process, const String& display, float vol, bool muted)
        : processName(process), displayName(display), volume(vol), isMuted(muted) {}
};

/**
 * Audio status data - updated to match new C# protocol structure
 */
struct AudioStatusData {
    std::vector<SessionStatusData> sessions;
    DefaultAudioDeviceData defaultDevice;
    bool hasDefaultDevice = false;
    unsigned long timestamp = 0;
    String reason;
    String originatingDeviceId;
    String originatingRequestId;
    int activeSessionCount = 0;

    void clear() {
        sessions.clear();
        defaultDevice.clear();
        hasDefaultDevice = false;
        timestamp = 0;
        reason = "";
        originatingDeviceId = "";
        originatingRequestId = "";
        activeSessionCount = 0;
    }

    bool isEmpty() const {
        return sessions.empty() && !hasDefaultDevice;
    }

    // Compatibility method to convert to old AudioLevel format for existing code
    std::vector<Application::Audio::AudioLevel> getCompatibleAudioLevels() const {
        std::vector<Application::Audio::AudioLevel> levels;

        // Convert sessions to AudioLevel format
        for (const auto& session : sessions) {
            Application::Audio::AudioLevel level;
            level.processName = session.processName;
            level.friendlyName = session.displayName.isEmpty() ? session.processName : session.displayName;
            level.volume = static_cast<int>(session.volume);
            level.isMuted = session.isMuted;
            level.state = session.state;
            level.lastUpdate = timestamp;
            levels.push_back(level);
        }

        return levels;
    }

    // Get default device as AudioLevel for compatibility
    Application::Audio::AudioLevel getCompatibleDefaultDevice() const {
        Application::Audio::AudioLevel level;
        level.processName = "DefaultDevice";
        level.friendlyName = defaultDevice.friendlyName;
        level.volume = static_cast<int>(defaultDevice.volume);
        level.isMuted = defaultDevice.isMuted;
        level.state = defaultDevice.dataFlow + "/" + defaultDevice.deviceRole;
        level.lastUpdate = timestamp;
        return level;
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
// TRANSPORT INTERFACE - For External Message Transport
// =============================================================================

/**
 * Transport interface for external message communication
 * Handles sending and receiving messages across transport boundaries
 */
struct TransportInterface {
    std::function<bool(const String& payload)> sendRaw;  // Send raw JSON payload
    std::function<bool()> isConnected;                   // Check connection status
    std::function<void()> update;                        // Update transport state
    std::function<String()> getStatus;                   // Get transport status
    std::function<bool()> init;                          // Initialize transport
    std::function<void()> deinit;                        // Cleanup transport
};

// Forward Declarations
struct ExternalMessage;
struct InternalMessage;

// =============================================================================
// CALLBACK TYPE DEFINITIONS
// =============================================================================

using ExternalMessageCallback = std::function<void(const ExternalMessage& message)>;
using InternalMessageCallback = std::function<void(const InternalMessage& message)>;

// Specific callback types for better type safety
using AudioStatusCallback = std::function<void(const AudioStatusData& data)>;
using NetworkStatusCallback = std::function<void(const String& status, bool connected)>;
using SDStatusCallback = std::function<void(const String& status, bool mounted)>;

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
 * Parse external message type from JSON payload
 * EFFICIENT: Direct string comparison with enum mapping
 */
inline MessageProtocol::ExternalMessageType parseExternalMessageType(const String& jsonPayload) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return MessageProtocol::ExternalMessageType::INVALID;
    }

    String typeStr = doc["MessageType"] | "";
    return MessageProtocol::stringToExternalMessageType(typeStr);
}

/**
 * Parse complete external message from JSON payload
 * EFFICIENT: Single JSON parse, direct field extraction
 */
inline ExternalMessage parseExternalMessage(const String& jsonPayload) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return ExternalMessage();
    }

    String typeStr = doc["MessageType"] | "";
    MessageProtocol::ExternalMessageType type = MessageProtocol::stringToExternalMessageType(typeStr);

    if (type == MessageProtocol::ExternalMessageType::INVALID) {
        return ExternalMessage();
    }

    ExternalMessage message(type,
                            doc["RequestId"] | "",
                            doc["DeviceId"] | "");
    message.originatingDeviceId = doc["OriginatingDeviceId"] | "";
    message.timestamp = doc["Timestamp"] | millis();
    message.parsedData = doc;  // Store the parsed JSON data

    return message;
}

/**
 * Check if message should be ignored (self-originated, invalid, etc.)
 */
inline bool shouldIgnoreMessage(const ExternalMessage& message, const String& myDeviceId = Config::DEVICE_ID) {
    // Ignore invalid messages
    if (message.messageType == MessageProtocol::ExternalMessageType::INVALID) {
        return true;
    }

    // Ignore self-originated messages
    if (message.deviceId == myDeviceId) {
        return true;
    }

    // Ignore messages from our own device ID in originatingDeviceId
    if (!message.originatingDeviceId.isEmpty() && message.originatingDeviceId == myDeviceId) {
        return true;
    }

    return false;
}

}  // namespace MessageParser

// =============================================================================
// MESSAGE SERIALIZATION UTILITIES
// =============================================================================

/**
 * Serialize ExternalMessage to JSON string
 */
String serializeExternalMessage(const ExternalMessage& message);

/**
 * Serialize InternalMessage to JSON string (for debugging/logging)
 */
String serializeInternalMessage(const InternalMessage& message);

// =============================================================================
// AUDIO DATA PARSING UTILITIES
// =============================================================================

/**
 * Parse audio status response from external message
 */
inline AudioStatusData parseStatusResponse(const ExternalMessage& message) {
    AudioStatusData data;

    if (message.messageType != MessageProtocol::ExternalMessageType::STATUS_UPDATE &&
        message.messageType != MessageProtocol::ExternalMessageType::STATUS_MESSAGE) {
        return data;
    }

    // Extract sessions from parsed data (matches C# Sessions field)
    if (message.parsedData["Sessions"].is<JsonArray>()) {
        size_t sessionCount = message.parsedData["Sessions"].size();
        for (size_t i = 0; i < sessionCount; i++) {
            auto sessionVar = message.parsedData["Sessions"][i];
            SessionStatusData session;
            session.processId = sessionVar["ProcessId"] | 0;
            session.processName = sessionVar["ProcessName"] | "";
            session.displayName = sessionVar["DisplayName"] | "";
            session.volume = sessionVar["Volume"] | 0.0f;
            session.isMuted = sessionVar["IsMuted"] | false;
            session.state = sessionVar["State"] | "";
            data.sessions.push_back(session);
        }
    }

    // Extract default device information (matches C# DefaultDevice field)
    if (message.parsedData["DefaultDevice"].is<JsonObject>()) {
        auto defaultVar = message.parsedData["DefaultDevice"];
        data.defaultDevice.friendlyName = defaultVar["FriendlyName"] | "";
        data.defaultDevice.volume = defaultVar["Volume"] | 0.0f;
        data.defaultDevice.isMuted = defaultVar["IsMuted"] | false;
        data.defaultDevice.dataFlow = defaultVar["DataFlow"] | "";
        data.defaultDevice.deviceRole = defaultVar["DeviceRole"] | "";
        data.hasDefaultDevice = true;
    }

    // Extract metadata
    data.timestamp = message.timestamp;
    data.reason = message.get<String>("Reason", "");
    data.originatingDeviceId = message.get<String>("OriginatingDeviceId", "");
    data.originatingRequestId = message.get<String>("OriginatingRequestId", "");
    data.activeSessionCount = message.get<int>("ActiveSessionCount", 0);

    return data;
}

/**
 * Create status response JSON from audio status data
 */
inline String createStatusResponse(const AudioStatusData& data) {
    JsonDocument doc;
    doc["MessageType"] = MessageProtocol::externalMessageTypeToString(MessageProtocol::ExternalMessageType::STATUS_MESSAGE);
    doc["DeviceId"] = Config::getDeviceId();
    doc["Timestamp"] = data.timestamp;
    doc["ActiveSessionCount"] = data.activeSessionCount;

    if (!data.reason.isEmpty()) {
        doc["Reason"] = data.reason;
    }

    if (!data.originatingDeviceId.isEmpty()) {
        doc["OriginatingDeviceId"] = data.originatingDeviceId;
    }

    if (!data.originatingRequestId.isEmpty()) {
        doc["OriginatingRequestId"] = data.originatingRequestId;
    }

    // Serialize sessions (matches C# Sessions structure)
    JsonArray sessionsArray = doc["Sessions"].to<JsonArray>();
    for (const auto& session : data.sessions) {
        JsonObject sessionObj = sessionsArray.add<JsonObject>();
        sessionObj["ProcessId"] = session.processId;
        sessionObj["ProcessName"] = session.processName;
        sessionObj["DisplayName"] = session.displayName;
        sessionObj["Volume"] = session.volume;
        sessionObj["IsMuted"] = session.isMuted;
        sessionObj["State"] = session.state;
    }

    // Serialize default device (matches C# DefaultDevice structure)
    if (data.hasDefaultDevice) {
        JsonObject defaultObj = doc["DefaultDevice"].to<JsonObject>();
        defaultObj["FriendlyName"] = data.defaultDevice.friendlyName;
        defaultObj["Volume"] = data.defaultDevice.volume;
        defaultObj["IsMuted"] = data.defaultDevice.isMuted;
        defaultObj["DataFlow"] = data.defaultDevice.dataFlow;
        defaultObj["DeviceRole"] = data.defaultDevice.deviceRole;
    }

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

}  // namespace Messaging
