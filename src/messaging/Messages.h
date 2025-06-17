#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "../application/AudioTypes.h"
#include "../include/MessageProtocol.h"

namespace Messaging {
namespace Messages {

// Base message interface for type safety
struct BaseMessage {
    virtual ~BaseMessage() = default;
    virtual String toJson() const = 0;
    virtual String getMessageType() const = 0;
};

// Audio status request message (sent to "STATUS_REQUEST" topic)
struct AudioStatusRequest : public BaseMessage {
    String messageType = Messaging::Protocol::MESSAGE_GET_STATUS;
    String requestId;
    
    AudioStatusRequest() {
        requestId = Messaging::Protocol::generateRequestId();
    }
    
    String toJson() const override {
        JsonDocument doc;
        doc["messageType"] = messageType;
        doc["requestId"] = requestId;
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    String getMessageType() const override {
        return messageType;
    }
    
    static AudioStatusRequest fromJson(const char* json) {
        AudioStatusRequest result;
        
        JsonDocument doc;
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            result.messageType = doc["messageType"] | Messaging::Protocol::MESSAGE_GET_STATUS;
            result.requestId = doc["requestId"] | "";
        }
        
        return result;
    }
};

// Audio status response/update message (received from "STATUS" topic or sent to "STATUS_UPDATE" topic)
struct AudioStatusResponse : public BaseMessage {
    String messageType = Messaging::Protocol::MESSAGE_STATUS_UPDATE;
    String requestId;
    std::vector<Application::Audio::AudioLevel> sessions;
    Application::Audio::AudioDevice defaultDevice;
    bool hasDefaultDevice = false;
    unsigned long timestamp;
    
    AudioStatusResponse() {
        timestamp = millis();
    }
    
    String toJson() const override {
        JsonDocument doc;
        doc["messageType"] = messageType;
        doc["requestId"] = requestId;
        doc["timestamp"] = timestamp;
        
        // Add sessions array
        JsonArray sessionsArray = doc["sessions"].to<JsonArray>();
        for (const auto& session : sessions) {
            JsonObject sessionObj = sessionsArray.add<JsonObject>();
            sessionObj["processName"] = session.processName;
            sessionObj["volume"] = session.volume / 100.0f; // Convert to 0.0-1.0 range
            sessionObj["isMuted"] = session.isMuted;
            sessionObj["state"] = "Active";
        }
        
        // Add default device if available
        if (hasDefaultDevice) {
            JsonObject defaultDeviceObj = doc["defaultDevice"].to<JsonObject>();
            defaultDeviceObj["friendlyName"] = defaultDevice.friendlyName;
            defaultDeviceObj["volume"] = defaultDevice.volume;
            defaultDeviceObj["isMuted"] = defaultDevice.isMuted;
            defaultDeviceObj["dataFlow"] = defaultDevice.state;
            defaultDeviceObj["deviceRole"] = "Console";
        }
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    String getMessageType() const override {
        return messageType;
    }
    
    static AudioStatusResponse fromJson(const char* json) {
        AudioStatusResponse result;
        
        if (!json) {
            return result;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);
        
        if (error) {
            return result;
        }
        
        JsonObject root = doc.as<JsonObject>();
        result.timestamp = millis();
        result.messageType = root["messageType"] | Messaging::Protocol::MESSAGE_STATUS_UPDATE;
        result.requestId = root["requestId"] | "";
        
        // Parse default device if present
        JsonObject defaultDeviceObj = root["defaultDevice"];
        if (!defaultDeviceObj.isNull()) {
            String friendlyName = defaultDeviceObj["friendlyName"] | "";
            float volume = defaultDeviceObj["volume"] | 0.0f;
            bool isMuted = defaultDeviceObj["isMuted"] | false;
            String dataFlow = defaultDeviceObj["dataFlow"] | "";
            String deviceRole = defaultDeviceObj["deviceRole"] | "";
            
            if (friendlyName.length() > 0) {
                result.defaultDevice.friendlyName = friendlyName;
                result.defaultDevice.volume = volume;
                result.defaultDevice.isMuted = isMuted;
                result.defaultDevice.state = dataFlow + "/" + deviceRole;
                result.hasDefaultDevice = true;
            }
        }
        
        // Parse sessions array
        JsonArray sessions = root["sessions"];
        if (!sessions.isNull()) {
            for (JsonObject session : sessions) {
                String processName = session["processName"] | "";
                String displayName = session["displayName"] | "";
                float volume = session["volume"] | 0.0f;
                bool isMuted = session["isMuted"] | false;
                String state = session["state"] | "";
                
                if (processName.length() > 0) {
                    Application::Audio::AudioLevel level;
                    level.processName = processName;
                    level.volume = (int)(volume * 100.0f); // Convert from 0.0-1.0 to 0-100
                    level.isMuted = isMuted;
                    level.lastUpdate = result.timestamp;
                    level.stale = false;
                    
                    result.sessions.push_back(level);
                }
            }
        }
        
        return result;
    }
};

} // namespace Messages
} // namespace Messaging

#endif // MESSAGES_H 