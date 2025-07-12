#include "MessageData.h"
#include "../Message.h"
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "MessageSerializer";

namespace Messaging {

// =============================================================================
// MESSAGE SERIALIZATION UTILITIES - Bridge to new brutal system
// =============================================================================

namespace MessageSerializer {

/**
 * Create status response JSON from audio status data
 * Bridges old API to new brutal system
 */
ParseResult<string> createStatusResponse(const AudioStatusData& data) {
    ESP_LOGD(TAG, "Creating status response (bridging to new system)");
    
    // Create a message using the new brutal system
    Messaging::Message::AudioData audioData;
    memset(&audioData, 0, sizeof(audioData));
    
    // Convert from old format to new format
    audioData.sessionCount = std::min((int)data.sessions.size(), 16);
    audioData.hasDefaultDevice = data.hasDefaultDevice;
    audioData.activeSessionCount = data.activeSessionCount;
    
    // Copy session data
    for (int i = 0; i < audioData.sessionCount; i++) {
        const auto& session = data.sessions[i];
        auto& sessionData = audioData.sessions[i];
        
        sessionData.processId = session.processId;
        strncpy(sessionData.processName, session.processName.c_str(), 
                sizeof(sessionData.processName) - 1);
        strncpy(sessionData.displayName, session.displayName.c_str(), 
                sizeof(sessionData.displayName) - 1);
        sessionData.volume = session.volume;
        sessionData.isMuted = session.isMuted;
        strncpy(sessionData.state, session.state.c_str(), 
                sizeof(sessionData.state) - 1);
    }
    
    // Copy default device data
    if (data.hasDefaultDevice) {
        strncpy(audioData.defaultDevice.friendlyName, 
                data.defaultDevice.friendlyName.c_str(),
                sizeof(audioData.defaultDevice.friendlyName) - 1);
        audioData.defaultDevice.volume = data.defaultDevice.volume;
        audioData.defaultDevice.isMuted = data.defaultDevice.isMuted;
        strncpy(audioData.defaultDevice.dataFlow, 
                data.defaultDevice.dataFlow.c_str(),
                sizeof(audioData.defaultDevice.dataFlow) - 1);
        strncpy(audioData.defaultDevice.deviceRole, 
                data.defaultDevice.deviceRole.c_str(),
                sizeof(audioData.defaultDevice.deviceRole) - 1);
    }
    
    // Copy additional fields
    strncpy(audioData.reason, data.reason.c_str(), sizeof(audioData.reason) - 1);
    strncpy(audioData.originatingRequestId, data.originatingRequestId.c_str(),
            sizeof(audioData.originatingRequestId) - 1);
    strncpy(audioData.originatingDeviceId, data.originatingDeviceId.c_str(),
            sizeof(audioData.originatingDeviceId) - 1);
    
    // Create message and serialize
    auto msg = Messaging::Message::createAudioStatus(audioData, Config::getDeviceId());
    string result = msg.toJson().c_str();
    
    return ParseResult<string>::createSuccess(result);
}

/**
 * Create asset request JSON
 * Bridges old API to new brutal system
 */
ParseResult<string> createAssetRequest(const string& processName, const string& deviceId) {
    ESP_LOGD(TAG, "Creating asset request for process: %s", processName.c_str());
    
    // Use the new brutal system to create the request
    String processNameStr = processName.c_str();
    String deviceIdStr = deviceId.empty() ? Config::getDeviceId() : deviceId.c_str();
    
    auto msg = Messaging::Message::createAssetRequest(processNameStr, deviceIdStr);
    string result = msg.toJson().c_str();
    
    ESP_LOGD(TAG, "Created asset request JSON: %s", result.c_str());
    return ParseResult<string>::createSuccess(result);
}

/**
 * Serialize InternalMessage to JSON string (for debugging/logging)
 */
ParseResult<string> serializeInternalMessage(const InternalMessage& message) {
    ESP_LOGW(TAG, "serializeInternalMessage called - this is deprecated in brutal system");
    return ParseResult<string>::createError("InternalMessage serialization deprecated - use Message::toJson()");
}

}  // namespace MessageSerializer

}  // namespace Messaging