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
// ENHANCED MESSAGE FACTORY SYSTEM - Project-Adapted Version
// =============================================================================

/**
 * ENHANCED SAFE MESSAGE FACTORY SYSTEM
 * 
 * Improvements while maintaining project patterns:
 * - Arduino String compatibility (not std::string_view)
 * - ParseResult<T> error handling (existing pattern)
 * - Compile-time validation where possible
 * - Memory safety improvements
 * - ESP32-optimized operations
 * - Backward compatibility maintained
 */

// Compile-time string length validation
template<size_t MaxSize>
constexpr bool validateStringLength(const String& str) {
    return str.length() < MaxSize;
}

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

// Enhanced data structure macros with validation and accessors
#define DEFINE_ENHANCED_STRING_DATA_STRUCT(StructName, fieldName, fieldSize) \
    struct StructName { \
        char fieldName[fieldSize]; \
        static constexpr size_t fieldName##_MAX_SIZE = fieldSize; \
        \
        StructName() { \
            memset(fieldName, 0, fieldSize); \
        } \
        \
        bool set##fieldName(const String& value) { \
            return enhancedStringCopy(fieldName, value, #fieldName); \
        } \
        \
        String get##fieldName() const { \
            return String(fieldName); \
        } \
        \
        bool isValid() const { \
            return strlen(fieldName) < fieldSize; \
        } \
    }

#define DEFINE_ENHANCED_STRING_BOOL_DATA_STRUCT(StructName, strField, strSize, boolField) \
    struct StructName { \
        char strField[strSize]; \
        bool boolField; \
        static constexpr size_t strField##_MAX_SIZE = strSize; \
        \
        StructName() : boolField(false) { \
            memset(strField, 0, strSize); \
        } \
        \
        bool set##strField(const String& value) { \
            return enhancedStringCopy(strField, value, #strField); \
        } \
        \
        String get##strField() const { \
            return String(strField); \
        } \
        \
        bool isValid() const { \
            return strlen(strField) < strSize; \
        } \
    }

#define DEFINE_ENHANCED_STRING_INT_DATA_STRUCT(StructName, strField, strSize, intField) \
    struct StructName { \
        char strField[strSize]; \
        int intField; \
        static constexpr size_t strField##_MAX_SIZE = strSize; \
        \
        StructName() : intField(0) { \
            memset(strField, 0, strSize); \
        } \
        \
        bool set##strField(const String& value) { \
            return enhancedStringCopy(strField, value, #strField); \
        } \
        \
        String get##strField() const { \
            return String(strField); \
        } \
        \
        bool isValid() const { \
            return strlen(strField) < strSize; \
        } \
    }

#define DEFINE_ENHANCED_DUAL_STRING_DATA_STRUCT(StructName, field1, size1, field2, size2) \
    struct StructName { \
        char field1[size1]; \
        char field2[size2]; \
        static constexpr size_t field1##_MAX_SIZE = size1; \
        static constexpr size_t field2##_MAX_SIZE = size2; \
        \
        StructName() { \
            memset(field1, 0, size1); \
            memset(field2, 0, size2); \
        } \
        \
        bool set##field1(const String& value) { \
            return enhancedStringCopy(field1, value, #field1); \
        } \
        \
        bool set##field2(const String& value) { \
            return enhancedStringCopy(field2, value, #field2); \
        } \
        \
        String get##field1() const { \
            return String(field1); \
        } \
        \
        String get##field2() const { \
            return String(field2); \
        } \
        \
        bool isValid() const { \
            return strlen(field1) < size1 && strlen(field2) < size2; \
        } \
    }

#define DEFINE_ENHANCED_DUAL_UINT8_DATA_STRUCT(StructName, field1, field2) \
    struct StructName { \
        uint8_t field1; \
        uint8_t field2; \
        \
        StructName() : field1(0), field2(0) {} \
        StructName(uint8_t f1, uint8_t f2) : field1(f1), field2(f2) {} \
        \
        bool isValid() const { \
            return true; /* uint8_t values are always valid */ \
        } \
    }

// Enhanced message factory macros with comprehensive error handling
#define SAFE_STRING_MESSAGE_FACTORY(funcName, msgType, dataStruct, strField, strSize, strParam) \
    static InternalMessage funcName(const String& strParam) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (strParam.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", strParam.length(), strSize); \
        } \
        \
        if (strParam.isEmpty()) { \
            ESP_LOGD(TAG, "Empty string provided for " #strField); \
        } \
        \
        DEFINE_ENHANCED_STRING_DATA_STRUCT(dataStruct, strField, strSize); \
        dataStruct data; \
        \
        if (!data.set##strField(strParam)) { \
            ESP_LOGW(TAG, "String was truncated during copy"); \
        } \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &data, sizeof(data)); \
    }

#define SAFE_STRING_BOOL_MESSAGE_FACTORY(funcName, msgType, dataStruct, strField, strSize, strParam, boolField, boolParam) \
    static InternalMessage funcName(const String& strParam, bool boolParam) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (strParam.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", strParam.length(), strSize); \
        } \
        \
        DEFINE_ENHANCED_STRING_BOOL_DATA_STRUCT(dataStruct, strField, strSize, boolField); \
        dataStruct data; \
        \
        if (!data.set##strField(strParam)) { \
            ESP_LOGW(TAG, "String was truncated during copy"); \
        } \
        \
        data.boolField = boolParam; \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &data, sizeof(data)); \
    }

#define SAFE_STRING_INT_MESSAGE_FACTORY(funcName, msgType, dataStruct, strField, strSize, strParam, intField, intParam) \
    static InternalMessage funcName(const String& strParam, int intParam) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        if (strParam.length() >= strSize) { \
            ESP_LOGE(TAG, "String too long: %u >= %zu, truncating", strParam.length(), strSize); \
        } \
        \
        DEFINE_ENHANCED_STRING_INT_DATA_STRUCT(dataStruct, strField, strSize, intField); \
        dataStruct data; \
        \
        if (!data.set##strField(strParam)) { \
            ESP_LOGW(TAG, "String was truncated during copy"); \
        } \
        \
        data.intField = intParam; \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &data, sizeof(data)); \
    }

#define SAFE_DUAL_STRING_MESSAGE_FACTORY(funcName, msgType, dataStruct, field1, size1, param1, field2, size2, param2) \
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
        DEFINE_ENHANCED_DUAL_STRING_DATA_STRUCT(dataStruct, field1, size1, field2, size2); \
        dataStruct data; \
        \
        bool field1Ok = data.set##field1(param1); \
        bool field2Ok = data.set##field2(param2); \
        \
        if (!field1Ok || !field2Ok) { \
            ESP_LOGW(TAG, "One or more strings were truncated during copy"); \
        } \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &data, sizeof(data)); \
    }

#define SAFE_DUAL_UINT8_MESSAGE_FACTORY(funcName, msgType, dataStruct, field1, field2, param1, param2) \
    static InternalMessage funcName(uint8_t param1, uint8_t param2) { \
        static const char* TAG = "MessageFactory::" #funcName; \
        \
        DEFINE_ENHANCED_DUAL_UINT8_DATA_STRUCT(dataStruct, field1, field2); \
        dataStruct data(param1, param2); \
        \
        ESP_LOGD(TAG, "Created message successfully"); \
        return InternalMessage(MessageProtocol::InternalMessageType::msgType, &data, sizeof(data)); \
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
    SAFE_STRING_MESSAGE_FACTORY(createSystemStatusMessage, MEMORY_STATUS, SystemStatusData, status, 64, status)
    SAFE_STRING_MESSAGE_FACTORY(createAudioDeviceChangeMessage, AUDIO_DEVICE_CHANGE, AudioDeviceChangeData, deviceName, 64, deviceName)
    SAFE_STRING_MESSAGE_FACTORY(createDebugUILogMessage, DEBUG_UI_LOG, DebugLogData, logMessage, 256, logMessage)

    // String + boolean message factories
    SAFE_STRING_BOOL_MESSAGE_FACTORY(createWifiStatusMessage, WIFI_STATUS, WifiStatusData, status, 32, status, connected, connected)
    SAFE_STRING_BOOL_MESSAGE_FACTORY(createSDStatusMessage, SD_STATUS, SDStatusData, status, 32, status, mounted, mounted)

    // String + integer message factories
    SAFE_STRING_INT_MESSAGE_FACTORY(createAudioVolumeMessage, AUDIO_STATE_UPDATE, AudioVolumeData, processName, 64, processName, volume, volume)

    // Dual string message factories
    SAFE_DUAL_STRING_MESSAGE_FACTORY(createNetworkInfoMessage, NETWORK_INFO, NetworkInfoData, ssid, 64, ssid, ip, 16, ip)
    SAFE_DUAL_STRING_MESSAGE_FACTORY(createUIUpdateMessage, UI_UPDATE, UIUpdateData, component, 32, component, data, 128, data)

    // Dual uint8_t message factories
    SAFE_DUAL_UINT8_MESSAGE_FACTORY(createCoreToCoreSyncMessage, TASK_SYNC, CoreSyncData, fromCore, toCore, fromCore, toCore)

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
