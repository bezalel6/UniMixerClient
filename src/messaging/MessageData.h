#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include "../application/AudioData.h"
#include "MessageConfig.h"

namespace Messaging {

// =============================================================================
// CORE MESSAGE TYPES
// =============================================================================

/**
 * Simple message container - no complex inheritance or templates
 */
struct Message {
    String topic;
    String type;
    String payload;
    String requestId;
    String deviceId;
    unsigned long timestamp;

    Message() {
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
    }

    Message(const String& t, const String& data) : topic(t), payload(data) {
        timestamp = millis();
        deviceId = Config::getDeviceId();
        requestId = Config::generateRequestId();
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
// JSON UTILITIES (Simplified)
// =============================================================================

/**
 * Simple JSON helper functions - no complex template magic
 */
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
 * Parse audio status response from JSON
 */
inline AudioStatusData parseStatusResponse(const String& jsonString) {
    AudioStatusData result;

    JsonDocument doc;
    if (deserializeJson(doc, jsonString) != DeserializationError::Ok) {
        return result;
    }

    JsonObject root = doc.as<JsonObject>();
    result.timestamp = root["timestamp"] | millis();
    result.reason = root["reason"] | "";
    result.originatingDeviceId = root["originatingDeviceId"] | "";

    // Parse default device
    JsonObject defaultDevice = root["defaultDevice"];
    if (!defaultDevice.isNull()) {
        result.defaultDevice.friendlyName = defaultDevice["friendlyName"] | "";
        result.defaultDevice.volume = (int)((defaultDevice["volume"] | 0.0f) * 100.0f);  // Convert from 0.0-1.0 to 0-100
        result.defaultDevice.isMuted = defaultDevice["isMuted"] | false;
        result.defaultDevice.state = defaultDevice["dataFlow"] | "";
        result.hasDefaultDevice = !result.defaultDevice.friendlyName.isEmpty();
    }

    // Parse audio sessions
    JsonArray sessions = root["sessions"];
    if (!sessions.isNull()) {
        for (JsonObject session : sessions) {
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

/**
 * Check if message should be ignored (self-originated)
 */
inline bool shouldIgnoreMessage(const String& jsonString, const String& myDeviceId = Config::DEVICE_ID) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonString) != DeserializationError::Ok) {
        return false;
    }

    String originatingDevice = doc["originatingDeviceId"] | "";
    String reason = doc["reason"] | "";

    return (reason == Config::REASON_UPDATE_RESPONSE && originatingDevice == myDeviceId);
}

}  // namespace Json

// =============================================================================
// TRANSPORT INTERFACE (Simplified)
// =============================================================================

/**
 * Simple transport interface - no complex function pointers
 */
struct TransportInterface {
    std::function<bool(const String& topic, const String& payload)> send;
    std::function<bool()> isConnected;
    std::function<void()> update;
    std::function<String()> getStatus;
    std::function<bool()> init;
    std::function<void()> deinit;
};

// =============================================================================
// CALLBACK TYPES (Simplified)
// =============================================================================

using MessageCallback = std::function<void(const String& topic, const String& payload)>;
using AudioStatusCallback = std::function<void(const AudioStatusData& data)>;

}  // namespace Messaging