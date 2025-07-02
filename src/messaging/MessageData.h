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
InternalMessage createDebugUILogMessage(const String& logMessage);

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
 * FIXED: Handles both "messageType" and "MessageType" field names using constants
 */
inline MessageProtocol::ExternalMessageType parseExternalMessageType(const String& jsonPayload) {
    JsonDocument doc;  // Use default dynamic allocation
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        ESP_LOGW("MessageParser", "Failed to parse messageType from JSON: %s", jsonPayload.c_str());
        return MessageProtocol::ExternalMessageType::INVALID;
    }

    // Try both field name cases using constants
    if (doc[MessageProtocol::JsonFields::OUTGOING_MESSAGE_TYPE].is<int>()) {
        return DESERIALIZE_EXTERNAL_MSG_TYPE(doc, MessageProtocol::JsonFields::OUTGOING_MESSAGE_TYPE, MessageProtocol::ExternalMessageType::INVALID);
    } else if (doc[MessageProtocol::JsonFields::INCOMING_MESSAGE_TYPE].is<int>()) {
        return DESERIALIZE_EXTERNAL_MSG_TYPE(doc, MessageProtocol::JsonFields::INCOMING_MESSAGE_TYPE, MessageProtocol::ExternalMessageType::INVALID);
    }

    ESP_LOGW("MessageParser", "No valid messageType field found in JSON");
    return MessageProtocol::ExternalMessageType::INVALID;
}

/**
 * Parse complete external message from JSON payload
 * EFFICIENT: Single JSON parse, direct field extraction
 * FIXED: Uses constants for field names + Unicode support
 */
inline ExternalMessage parseExternalMessage(const String& jsonPayload) {
    using namespace MessageProtocol::JsonFields;

    // Use default JsonDocument with automatic memory allocation for Unicode support
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, jsonPayload);
    if (error) {
        ESP_LOGW("MessageParser", "JSON deserialization failed: %s", error.c_str());
        ESP_LOGW("MessageParser", "Failed payload: %s", jsonPayload.c_str());
        return ExternalMessage();
    }

    // Parse messageType using constants (handle both formats)
    MessageProtocol::ExternalMessageType type = MessageProtocol::ExternalMessageType::INVALID;

    // First try uppercase format (outgoing standard)
    if (doc[OUTGOING_MESSAGE_TYPE].is<int>()) {
        type = SAFE_DESERIALIZE_EXTERNAL_MSG_TYPE(doc, OUTGOING_MESSAGE_TYPE);
    }
    // Fall back to lowercase format (incoming format)
    else if (doc[INCOMING_MESSAGE_TYPE].is<int>()) {
        int typeNum = doc[INCOMING_MESSAGE_TYPE] | static_cast<int>(MessageProtocol::ExternalMessageType::INVALID);
        auto parsedType = static_cast<MessageProtocol::ExternalMessageType>(typeNum);
        type = MessageProtocol::isValidExternalMessageType(parsedType) ? parsedType : MessageProtocol::ExternalMessageType::INVALID;
    }

    if (type == MessageProtocol::ExternalMessageType::INVALID) {
        ESP_LOGW("MessageParser", "Invalid or missing messageType field in JSON");
        ESP_LOGW("MessageParser", "Available fields in JSON:");
        for (JsonPair kv : doc.as<JsonObject>()) {
            ESP_LOGW("MessageParser", "  - %s", kv.key().c_str());
        }
        return ExternalMessage();
    }

    // Parse other core fields using constants
    String requestId = GET_STRING_FIELD_BOTH_CASES(doc, INCOMING_REQUEST_ID, OUTGOING_REQUEST_ID);
    String deviceId = GET_STRING_FIELD_BOTH_CASES(doc, INCOMING_DEVICE_ID, OUTGOING_DEVICE_ID);
    String originatingDeviceId = GET_STRING_FIELD_BOTH_CASES(doc, INCOMING_ORIGINATING_DEVICE_ID, OUTGOING_ORIGINATING_DEVICE_ID);
    unsigned long timestamp = GET_INT_FIELD_BOTH_CASES(doc, INCOMING_TIMESTAMP, OUTGOING_TIMESTAMP, millis());

    ExternalMessage message(type, requestId, deviceId);
    message.originatingDeviceId = originatingDeviceId;
    message.timestamp = timestamp;
    message.parsedData = doc;  // Store the parsed JSON data

    ESP_LOGD("MessageParser", "Successfully parsed external message: type=%d, deviceId=%s",
             static_cast<int>(type), deviceId.c_str());

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
 * UPDATED: Uses constants for field names instead of hardcoded strings
 */
inline AudioStatusData parseStatusResponse(const ExternalMessage& message) {
    using namespace MessageProtocol::JsonFields;

    AudioStatusData data;

    if (message.messageType != MessageProtocol::ExternalMessageType::STATUS_UPDATE &&
        message.messageType != MessageProtocol::ExternalMessageType::STATUS_MESSAGE) {
        return data;
    }

    // Extract sessions from parsed data using constants (handle both cases)
    if (HAS_FIELD_EITHER_CASE(message.parsedData, SESSIONS, SESSIONS_CAPS)) {
        JsonArray sessionsArray;
        if (message.parsedData[SESSIONS].is<JsonArray>()) {
            sessionsArray = message.parsedData[SESSIONS];
        } else if (message.parsedData[SESSIONS_CAPS].is<JsonArray>()) {
            sessionsArray = message.parsedData[SESSIONS_CAPS];
        }

        if (!sessionsArray.isNull()) {
            size_t sessionCount = sessionsArray.size();
            for (size_t i = 0; i < sessionCount; i++) {
                auto sessionVar = sessionsArray[i];
                SessionStatusData session;
                session.processId = GET_INT_FIELD_BOTH_CASES(sessionVar, PROCESS_ID, PROCESS_ID_CAPS, 0);
                session.processName = GET_STRING_FIELD_BOTH_CASES(sessionVar, PROCESS_NAME, PROCESS_NAME_CAPS);
                session.displayName = GET_STRING_FIELD_BOTH_CASES(sessionVar, DISPLAY_NAME, DISPLAY_NAME_CAPS);
                session.volume = GET_FLOAT_FIELD_BOTH_CASES(sessionVar, VOLUME, VOLUME_CAPS);
                session.isMuted = GET_BOOL_FIELD_BOTH_CASES(sessionVar, IS_MUTED, IS_MUTED_CAPS);
                session.state = GET_STRING_FIELD_BOTH_CASES(sessionVar, STATE, STATE_CAPS);
                data.sessions.push_back(session);
            }
        }
    }

    // Extract default device information using constants (handle both cases)
    if (HAS_FIELD_EITHER_CASE(message.parsedData, DEFAULT_DEVICE, DEFAULT_DEVICE_CAPS)) {
        JsonObject defaultVar;
        if (message.parsedData[DEFAULT_DEVICE].is<JsonObject>()) {
            defaultVar = message.parsedData[DEFAULT_DEVICE];
        } else if (message.parsedData[DEFAULT_DEVICE_CAPS].is<JsonObject>()) {
            defaultVar = message.parsedData[DEFAULT_DEVICE_CAPS];
        }

        if (!defaultVar.isNull()) {
            data.defaultDevice.friendlyName = GET_STRING_FIELD_BOTH_CASES(defaultVar, FRIENDLY_NAME, FRIENDLY_NAME_CAPS);
            data.defaultDevice.volume = GET_FLOAT_FIELD_BOTH_CASES(defaultVar, VOLUME, VOLUME_CAPS);
            data.defaultDevice.isMuted = GET_BOOL_FIELD_BOTH_CASES(defaultVar, IS_MUTED, IS_MUTED_CAPS);
            data.defaultDevice.dataFlow = GET_STRING_FIELD_BOTH_CASES(defaultVar, DATA_FLOW, DATA_FLOW_CAPS);
            data.defaultDevice.deviceRole = GET_STRING_FIELD_BOTH_CASES(defaultVar, DEVICE_ROLE, DEVICE_ROLE_CAPS);
            data.hasDefaultDevice = true;
        }
    }

    // Extract metadata using constants
    data.timestamp = message.timestamp;
    data.reason = GET_STRING_FIELD_BOTH_CASES(message.parsedData, REASON, REASON_CAPS);
    data.originatingDeviceId = GET_STRING_FIELD_BOTH_CASES(message.parsedData, INCOMING_ORIGINATING_DEVICE_ID, OUTGOING_ORIGINATING_DEVICE_ID);
    data.originatingRequestId = GET_STRING_FIELD_BOTH_CASES(message.parsedData, ORIGINATING_REQUEST_ID, ORIGINATING_REQUEST_ID_CAPS);
    data.activeSessionCount = GET_INT_FIELD_BOTH_CASES(message.parsedData, ACTIVE_SESSION_COUNT, ACTIVE_SESSION_COUNT_CAPS, 0);

    return data;
}

/**
 * Create status response JSON from audio status data
 * UPDATED: Uses constants for field names instead of hardcoded strings
 */
inline String createStatusResponse(const AudioStatusData& data) {
    using namespace MessageProtocol::JsonFields;

    JsonDocument doc;
    doc[OUTGOING_MESSAGE_TYPE] = SERIALIZE_EXTERNAL_MSG_TYPE(MessageProtocol::ExternalMessageType::STATUS_MESSAGE);
    doc[OUTGOING_DEVICE_ID] = Config::getDeviceId();
    doc[OUTGOING_TIMESTAMP] = data.timestamp;
    doc[ACTIVE_SESSION_COUNT_CAPS] = data.activeSessionCount;

    if (!data.reason.isEmpty()) {
        doc[REASON_CAPS] = data.reason;
    }

    if (!data.originatingDeviceId.isEmpty()) {
        doc[OUTGOING_ORIGINATING_DEVICE_ID] = data.originatingDeviceId;
    }

    if (!data.originatingRequestId.isEmpty()) {
        doc[ORIGINATING_REQUEST_ID_CAPS] = data.originatingRequestId;
    }

    // Serialize sessions using constants (use capitalized version for outgoing)
    JsonArray sessionsArray = doc[SESSIONS_CAPS].to<JsonArray>();
    for (const auto& session : data.sessions) {
        JsonObject sessionObj = sessionsArray.add<JsonObject>();
        sessionObj[PROCESS_ID_CAPS] = session.processId;
        sessionObj[PROCESS_NAME_CAPS] = session.processName;
        sessionObj[DISPLAY_NAME_CAPS] = session.displayName;
        sessionObj[VOLUME_CAPS] = session.volume;
        sessionObj[IS_MUTED_CAPS] = session.isMuted;
        sessionObj[STATE_CAPS] = session.state;
    }

    // Serialize default device using constants (use capitalized version for outgoing)
    if (data.hasDefaultDevice) {
        JsonObject defaultObj = doc[DEFAULT_DEVICE_CAPS].to<JsonObject>();
        defaultObj[FRIENDLY_NAME_CAPS] = data.defaultDevice.friendlyName;
        defaultObj[VOLUME_CAPS] = data.defaultDevice.volume;
        defaultObj[IS_MUTED_CAPS] = data.defaultDevice.isMuted;
        defaultObj[DATA_FLOW_CAPS] = data.defaultDevice.dataFlow;
        defaultObj[DEVICE_ROLE_CAPS] = data.defaultDevice.deviceRole;
    }

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

}  // namespace Messaging
