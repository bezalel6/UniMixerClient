#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <memory>
#include <functional>
#include <esp_log.h>
#include "../../application/audio/AudioData.h"
#include "MessageConfig.h"
#include <MessageProtocol.h>

namespace Messaging {

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

struct ExternalMessage;
struct InternalMessage;

// =============================================================================
// TYPE-SAFE MESSAGE PARSING RESULT
// =============================================================================

/**
 * Result wrapper for type-safe message parsing
 */
template<typename T>
struct ParseResult {
    bool success = false;
    T data;
    String error;

    ParseResult() = default;

    // Factory function for success case
    static ParseResult<T> createSuccess(const T& value) {
        ParseResult<T> result;
        result.success = true;
        result.data = value;
        return result;
    }

    // Factory function for error case
    static ParseResult<T> createError(const String& err) {
        ParseResult<T> result;
        result.success = false;
        result.error = err;
        return result;
    }

    bool isValid() const { return success; }
    const T& getValue() const { return data; }
    const String& getError() const { return error; }
};

// =============================================================================
// EXTERNAL MESSAGE TYPES - For messages received over transports
// =============================================================================

/**
 * EXTERNAL MESSAGE - Received over available transports (Serial in normal mode)
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
    JsonDocument parsedData;

    ExternalMessage() : parsedData(1024) {
        messageType = MessageProtocol::ExternalMessageType::INVALID;
        timestamp = millis();
    }

    ExternalMessage(MessageProtocol::ExternalMessageType type, const String& reqId = "", const String& devId = "")
        : messageType(type), requestId(reqId), deviceId(devId), parsedData(1024) {
        timestamp = millis();
    }

    // Direct access to parsed data with type safety
    template <typename T>
    T get(const String& field, const T& defaultValue = T{}) const {
        auto value = parsedData[field];
        if (value.isNull()) {
            return defaultValue;
        }
        return value.as<T>();
    }

    // Check if field exists
    bool hasField(const String& field) const {
        return !parsedData[field].isNull();
    }

    // Validation and security methods (implemented in MessageData.cpp)
    bool isSelfOriginated() const;
    bool requiresResponse() const;
    MessageProtocol::ExternalMessageCategory getCategory() const {
        return MessageProtocol::getExternalMessageCategory(messageType);
    }
    MessageProtocol::MessagePriority getPriority() const {
        return MessageProtocol::getExternalMessagePriority(messageType);
    }
};

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

/**
 * Asset response data - extracted from external message for internal routing
 */
struct AssetResponseData {
    String requestId;
    String deviceId;
    String processName;
    bool success = false;
    String errorMessage;
    String assetDataBase64;  // Base64 encoded asset data
    int width = 0;
    int height = 0;
    String format;
    unsigned long timestamp = 0;

    AssetResponseData() = default;
    AssetResponseData(const ExternalMessage& external);
};

// =============================================================================
// TRANSPORT INTERFACE - For External Message Transport
// =============================================================================

/**
 * Transport interface for external message communication
 * Handles sending and receiving messages across transport boundaries
 */
class TransportInterface {
public:
    std::function<bool(const String& payload)> sendRaw;
    std::function<bool()> isConnected;
    std::function<void()> update;
    std::function<String()> getStatus;
    std::function<bool()> init;
    std::function<void()> deinit;

    TransportInterface() = default;
};

// =============================================================================
// CALLBACK TYPE DEFINITIONS
// =============================================================================

using ExternalMessageCallback = std::function<void(const ExternalMessage& message)>;
using InternalMessageCallback = std::function<void(const InternalMessage& message)>;
using AudioStatusCallback = std::function<void(const AudioStatusData& data)>;
using NetworkStatusCallback = std::function<void(const String& status, bool connected)>;
using SDStatusCallback = std::function<void(const String& status, bool mounted)>;

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
    std::shared_ptr<uint8_t[]> data;
    size_t dataSize = 0;
    unsigned long timestamp;
    uint8_t priority;
    bool requiresResponse = false;

    InternalMessage();
    InternalMessage(MessageProtocol::InternalMessageType type, const void* payload = nullptr, size_t size = 0);

    // Type-safe data accessors
    template <typename T>
    T* getTypedData() const {
        if (data && dataSize >= sizeof(T)) {
            return reinterpret_cast<T*>(data.get());
        }
        return nullptr;
    }

    template <typename T>
    bool setTypedData(const T& payload) {
        dataSize = sizeof(T);
        data.reset(new uint8_t[dataSize]);
        memcpy(data.get(), &payload, dataSize);
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
// ENHANCED MESSAGE FACTORY SYSTEM - Project-Adapted Version (Fixed)
// =============================================================================

/**
 * ENHANCED SAFE MESSAGE FACTORY SYSTEM - Compilation Fixed
 * 
 * Simplified approach that avoids C++ local class restrictions:
 * - No static members in local structs
 * - Simplified data structures
 * - Enhanced safety without complex macros
 * - Backward compatibility maintained
 */

// Enhanced safe string copy with validation and logging
template<size_t BufferSize>
bool enhancedStringCopy(char (&dest)[BufferSize], const String& src, const char* fieldName = "field") {
    if (src.length() >= BufferSize) {
        ESP_LOGW("MessageFactory", "String truncated in %s: %u chars to %zu bytes", 
                 fieldName, src.length(), BufferSize - 1);
        // Still copy what we can, but truncated
        strncpy(dest, src.c_str(), BufferSize - 1);
        dest[BufferSize - 1] = '\0';
        return false;
    }
    
    if (src.isEmpty()) {
        dest[0] = '\0';
        return true;
    }
    
    // Use optimized copy for ESP32
    memcpy(dest, src.c_str(), src.length());
    dest[src.length()] = '\0';
    return true;
}

// Simple but safe message factory macros
#define SAFE_STRING_MESSAGE_FACTORY(funcName, msgType, strField, strSize) \
    static InternalMessage funcName(const String& param) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (param.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", param.length(), strSize); \
        } \
        if (param.isEmpty()) { \
            ESP_LOGD(TAG, "Empty string provided"); \
        } \
        \
        struct LocalData { \
            char strField[strSize]; \
        }; \
        \
        LocalData msgData; \
        memset(&msgData, 0, sizeof(msgData)); \
        enhancedStringCopy(msgData.strField, param, #strField); \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &msgData, sizeof(msgData)); \
    }

#define SAFE_STRING_BOOL_MESSAGE_FACTORY(funcName, msgType, strField, strSize) \
    static InternalMessage funcName(const String& strParam, bool boolParam) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (strParam.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", strParam.length(), strSize); \
        } \
        \
        struct LocalData { \
            char strField[strSize]; \
            bool flag; \
        }; \
        \
        LocalData msgData; \
        memset(&msgData, 0, sizeof(msgData)); \
        enhancedStringCopy(msgData.strField, strParam, #strField); \
        msgData.flag = boolParam; \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &msgData, sizeof(msgData)); \
    }

#define SAFE_STRING_INT_MESSAGE_FACTORY(funcName, msgType, strField, strSize) \
    static InternalMessage funcName(const String& strParam, int intParam) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (strParam.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", strParam.length(), strSize); \
        } \
        \
        struct LocalData { \
            char strField[strSize]; \
            int value; \
        }; \
        \
        LocalData msgData; \
        memset(&msgData, 0, sizeof(msgData)); \
        enhancedStringCopy(msgData.strField, strParam, #strField); \
        msgData.value = intParam; \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &msgData, sizeof(msgData)); \
    }

#define SAFE_DUAL_STRING_MESSAGE_FACTORY(funcName, msgType, field1, size1, field2, size2) \
    static InternalMessage funcName(const String& param1, const String& param2) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (param1.length() >= size1) { \
            ESP_LOGE(TAG, #field1 " too long: %u >= %zu, truncating", param1.length(), size1); \
        } \
        if (param2.length() >= size2) { \
            ESP_LOGE(TAG, #field2 " too long: %u >= %zu, truncating", param2.length(), size2); \
        } \
        \
        struct LocalData { \
            char field1[size1]; \
            char field2[size2]; \
        }; \
        \
        LocalData msgData; \
        memset(&msgData, 0, sizeof(msgData)); \
        enhancedStringCopy(msgData.field1, param1, #field1); \
        enhancedStringCopy(msgData.field2, param2, #field2); \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &msgData, sizeof(msgData)); \
    }

#define SAFE_DUAL_UINT8_MESSAGE_FACTORY(funcName, msgType, field1, field2) \
    static InternalMessage funcName(uint8_t param1, uint8_t param2) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        struct LocalData { \
            uint8_t field1; \
            uint8_t field2; \
        }; \
        \
        LocalData msgData; \
        msgData.field1 = param1; \
        msgData.field2 = param2; \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &msgData, sizeof(msgData)); \
    }

// =============================================================================
// MESSAGE FACTORY - Enhanced Type-safe message creation
// =============================================================================

class MessageFactory {
public:
    // Create typed external messages (unchanged for compatibility)
    static ExternalMessage createStatusRequest(const String& deviceId = "");
    static ExternalMessage createAssetRequest(const String& processName, const String& deviceId = "");

    // ENHANCED SAFE INTERNAL MESSAGE FACTORIES
    // Same method signatures for backward compatibility, but now with enhanced safety

    // Single string message factories
    SAFE_STRING_MESSAGE_FACTORY(createSystemStatusMessage, MEMORY_STATUS, status, 64)
    SAFE_STRING_MESSAGE_FACTORY(createAudioDeviceChangeMessage, AUDIO_DEVICE_CHANGE, deviceName, 64)
    SAFE_STRING_MESSAGE_FACTORY(createDebugUILogMessage, DEBUG_UI_LOG, logMessage, 256)

    // String + boolean message factories
    SAFE_STRING_BOOL_MESSAGE_FACTORY(createWifiStatusMessage, WIFI_STATUS, status, 32)
    SAFE_STRING_BOOL_MESSAGE_FACTORY(createSDStatusMessage, SD_STATUS, status, 32)

    // String + integer message factories
    SAFE_STRING_INT_MESSAGE_FACTORY(createAudioVolumeMessage, AUDIO_STATE_UPDATE, processName, 64)

    // Dual string message factories (fixed naming conflicts)
    SAFE_DUAL_STRING_MESSAGE_FACTORY(createNetworkInfoMessage, NETWORK_INFO, ssid, 64, ip, 16)
    SAFE_DUAL_STRING_MESSAGE_FACTORY(createUIUpdateMessage, UI_UPDATE, component, 32, uiData, 128)

    // Dual uint8_t message factories
    SAFE_DUAL_UINT8_MESSAGE_FACTORY(createCoreToCoreSyncMessage, TASK_SYNC, fromCore, toCore)

    // COMPILE-TIME VALIDATION HELPERS
    template<size_t MaxSize>
    static constexpr bool wouldStringFit(const char* str) {
        return strlen(str) < MaxSize;
    }
    
    // SIZE CONSTANTS for validation
    static constexpr size_t SYSTEM_STATUS_MAX_SIZE = 64;
    static constexpr size_t AUDIO_DEVICE_NAME_MAX_SIZE = 64;
    static constexpr size_t DEBUG_LOG_MAX_SIZE = 256;
    static constexpr size_t WIFI_STATUS_MAX_SIZE = 32;
    static constexpr size_t SD_STATUS_MAX_SIZE = 32;
    static constexpr size_t AUDIO_PROCESS_NAME_MAX_SIZE = 64;
    static constexpr size_t NETWORK_SSID_MAX_SIZE = 64;
    static constexpr size_t NETWORK_IP_MAX_SIZE = 16;
    static constexpr size_t UI_COMPONENT_MAX_SIZE = 32;
    static constexpr size_t UI_DATA_MAX_SIZE = 128;
    
    // VALIDATION METHODS
    static bool validateSystemStatus(const String& status) {
        return status.length() < SYSTEM_STATUS_MAX_SIZE;
    }
    
    static bool validateAudioDeviceName(const String& deviceName) {
        return deviceName.length() < AUDIO_DEVICE_NAME_MAX_SIZE;
    }
    
    static bool validateDebugLog(const String& logMessage) {
        return logMessage.length() < DEBUG_LOG_MAX_SIZE;
    }
    
    static bool validateWifiStatus(const String& status) {
        return status.length() < WIFI_STATUS_MAX_SIZE;
    }
    
    static bool validateNetworkSSID(const String& ssid) {
        return ssid.length() < NETWORK_SSID_MAX_SIZE;
    }
    
    static bool validateNetworkIP(const String& ip) {
        return ip.length() < NETWORK_IP_MAX_SIZE;
    }
    
    static bool validateUIComponent(const String& component) {
        return component.length() < UI_COMPONENT_MAX_SIZE;
    }
    
    static bool validateUIData(const String& data) {
        return data.length() < UI_DATA_MAX_SIZE;
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

}  // namespace MessageConverter

// =============================================================================
// NAMESPACE ALIASES FOR CONVENIENCE
// =============================================================================

using ExtMsg = ExternalMessage;
using IntMsg = InternalMessage;
using ExtMsgType = MessageProtocol::ExternalMessageType;
using IntMsgType = MessageProtocol::InternalMessageType;

// =============================================================================
// TYPE-SAFE MESSAGE PARSING UTILITIES
// =============================================================================

namespace MessageParser {

/**
 * Parse external message type from JSON payload with error handling
 */
ParseResult<MessageProtocol::ExternalMessageType> parseExternalMessageType(const String& jsonPayload);

/**
 * Parse complete external message from JSON payload with comprehensive error handling
 */
ParseResult<ExternalMessage> parseExternalMessage(const String& jsonPayload);

/**
 * Check if message should be ignored (self-originated, invalid, etc.)
 */
bool shouldIgnoreMessage(const ExternalMessage& message, const String& myDeviceId = Config::getDeviceId());

/**
 * Type-safe audio status parsing
 */
ParseResult<AudioStatusData> parseAudioStatusData(const ExternalMessage& message);

/**
 * Type-safe asset response parsing
 */
ParseResult<AssetResponseData> parseAssetResponseData(const ExternalMessage& message);

}  // namespace MessageParser

// =============================================================================
// MESSAGE SERIALIZATION UTILITIES
// =============================================================================

namespace MessageSerializer {

/**
 * Serialize ExternalMessage to JSON string with error handling
 */
ParseResult<String> serializeExternalMessage(const ExternalMessage& message);

/**
 * Serialize InternalMessage to JSON string (for debugging/logging)
 */
ParseResult<String> serializeInternalMessage(const InternalMessage& message);

/**
 * Create status response JSON from audio status data
 */
ParseResult<String> createStatusResponse(const AudioStatusData& data);

/**
 * Create asset request JSON
 */
ParseResult<String> createAssetRequest(const String& processName, const String& deviceId = "");

}  // namespace MessageSerializer

}  // namespace Messaging
