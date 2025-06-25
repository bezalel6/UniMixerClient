#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include <esp_log.h>
#include "../application/AudioData.h"
#include "MessageConfig.h"

namespace Messaging {

// =============================================================================
// CORE MESSAGE TYPES
// =============================================================================

/**
 * Core message container - messageType-based routing
 */
struct Message {
    String messageType;  // Primary routing field
    String payload;      // Raw JSON string
    String requestId;
    String deviceId;
    unsigned long timestamp;

    // Parsed content (populated on demand)
    mutable JsonDocument parsedContent;
    mutable bool contentParsed = false;

    Message() {
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    Message(const String& type, const String& data) : messageType(type), payload(data) {
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    // Get parsed JSON content (lazy parsing)
    const JsonDocument& getParsedContent() const {
        if (!contentParsed) {
            deserializeJson(parsedContent, payload);
            contentParsed = true;
        }
        return parsedContent;
    }

    // Helper to get JSON field
    template <typename T>
    T get(const String& field, T defaultValue = T{}) const {
        const JsonDocument& doc = getParsedContent();
        return doc[field] | defaultValue;
    }
};

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

// =============================================================================
// MESSAGE PARSING UTILITIES
// =============================================================================

namespace MessageParser {

/**
 * Parse messageType from raw JSON string
 */
inline String parseMessageType(const String& jsonPayload) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        return "";
    }
    return doc["messageType"] | "";
}

/**
 * Create Message object from raw JSON payload
 */
inline Message parseMessage(const String& jsonPayload) {
    Message message;
    message.payload = jsonPayload;

    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload.c_str()) == DeserializationError::Ok) {
        message.messageType = doc["messageType"] | "";
        message.requestId = doc["requestId"] | "";
        message.deviceId = doc["deviceId"] | "";
        message.timestamp = doc["timestamp"] | millis();
    }

    return message;
}

/**
 * Check if message should be ignored (self-originated)
 */
inline bool shouldIgnoreMessage(const Message& message, const String& myDeviceId = Config::DEVICE_ID) {
    String originatingDevice = message.get<String>("originatingDeviceId");
    String reason = message.get<String>("reason");
    return (reason == Config::REASON_UPDATE_RESPONSE && originatingDevice == myDeviceId);
}

}  // namespace MessageParser

// =============================================================================
// JSON UTILITIES (Simplified)
// =============================================================================

namespace Json {

/**
 * Create audio status request JSON
 */
inline String createStatusRequest(const String& deviceId = Config::DEVICE_ID) {
    JsonDocument doc;
    doc["messageType"] = Config::MESSAGE_TYPE_GET_STATUS;
    doc["requestId"] = Config::generateRequestId();
    doc["deviceId"] = deviceId;

    String result;
    serializeJson(doc, result);
    return result;
}

/**
 * Create audio status response JSON
 */
inline String createStatusResponse(const AudioStatusData& data) {
    JsonDocument doc;
    doc["messageType"] = Config::MESSAGE_TYPE_STATUS_UPDATE;
    doc["requestId"] = Config::generateRequestId();
    doc["deviceId"] = Config::DEVICE_ID;
    doc["timestamp"] = data.timestamp;
    doc["reason"] = data.reason;

    if (!data.originatingDeviceId.isEmpty()) {
        doc["originatingDeviceId"] = data.originatingDeviceId;
    }

    // Add audio sessions
    JsonArray sessionsArray = doc["sessions"].to<JsonArray>();
    for (const auto& level : data.audioLevels) {
        JsonObject sessionObj = sessionsArray.add<JsonObject>();
        sessionObj["processName"] = level.processName;
        sessionObj["volume"] = level.volume / 100.0f;  // Convert to 0.0-1.0
        sessionObj["isMuted"] = level.isMuted;
        sessionObj["state"] = "Active";
    }

    // Add default device if available
    if (data.hasDefaultDevice) {
        JsonObject defaultObj = doc["defaultDevice"].to<JsonObject>();
        defaultObj["friendlyName"] = data.defaultDevice.friendlyName;
        defaultObj["volume"] = data.defaultDevice.volume / 100.0f;
        defaultObj["isMuted"] = data.defaultDevice.isMuted;
        defaultObj["dataFlow"] = data.defaultDevice.state;
        defaultObj["deviceRole"] = "Console";
    }

    String result;
    serializeJson(doc, result);
    return result;
}

/**
 * Parse audio status response from Message
 */
inline AudioStatusData parseStatusResponse(const Message& message) {
    AudioStatusData result;

    const JsonDocument& doc = message.getParsedContent();
    if (doc.isNull()) {
        return result;
    }

    result.timestamp = doc["timestamp"] | millis();
    result.reason = doc["reason"] | "";
    result.originatingDeviceId = doc["originatingDeviceId"] | "";

    // Parse default device
    JsonObjectConst defaultDevice = doc["defaultDevice"];
    if (!defaultDevice.isNull()) {
        result.defaultDevice.friendlyName = defaultDevice["friendlyName"] | "";
        float rawVolume = defaultDevice["volume"] | 0.0f;
        result.defaultDevice.volume = (int)(rawVolume * 100.0f);  // Convert from 0.0-1.0 to 0-100
        result.defaultDevice.isMuted = defaultDevice["isMuted"] | false;
        result.defaultDevice.state = defaultDevice["dataFlow"] | "";
        result.hasDefaultDevice = !result.defaultDevice.friendlyName.isEmpty();
    }

    // Parse audio sessions
    JsonArrayConst sessions = doc["sessions"];
    if (!sessions.isNull()) {
        for (JsonObjectConst session : sessions) {
            String processName = session["processName"] | "";
            if (!processName.isEmpty()) {
                Application::Audio::AudioLevel level;
                level.processName = processName;
                level.friendlyName = processName;
                level.volume = (int)((session["volume"] | 0.0f) * 100.0f);  // Convert to 0-100
                level.isMuted = session["isMuted"] | false;
                level.lastUpdate = result.timestamp;
                level.stale = false;

                result.audioLevels.push_back(level);
            }
        }
    }

    return result;
}

}  // namespace Json

// =============================================================================
// TRANSPORT INTERFACE (Simplified)
// =============================================================================

/**
 * Simplified transport interface - only handles raw message strings
 */
struct TransportInterface {
    std::function<bool(const String& payload)> send;  // Only payload, no topics
    std::function<bool()> isConnected;
    std::function<void()> update;
    std::function<String()> getStatus;
    std::function<bool()> init;
    std::function<void()> deinit;
};

// =============================================================================
// CALLBACK TYPES (Type-Based)
// =============================================================================

using MessageCallback = std::function<void(const Message& message)>;
using AudioStatusCallback = std::function<void(const AudioStatusData& data)>;

}  // namespace Messaging
