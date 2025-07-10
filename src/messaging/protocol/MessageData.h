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
// MESSAGE FACTORY - Type-safe message creation
// =============================================================================

class MessageFactory {
public:
    // Create typed external messages
    static ExternalMessage createStatusRequest(const String& deviceId = "");
    static ExternalMessage createAssetRequest(const String& processName, const String& deviceId = "");

    // Create typed internal messages
    static InternalMessage createAudioVolumeMessage(const String& processName, int volume);
    static InternalMessage createUIUpdateMessage(const String& component, const String& data);
    static InternalMessage createSystemStatusMessage(const String& status);
    static InternalMessage createWifiStatusMessage(const String& status, bool connected);
    static InternalMessage createNetworkInfoMessage(const String& ssid, const String& ip);
    static InternalMessage createSDStatusMessage(const String& status, bool mounted);
    static InternalMessage createAudioDeviceChangeMessage(const String& deviceName);
    static InternalMessage createCoreToCoreSyncMessage(uint8_t fromCore, uint8_t toCore);
    static InternalMessage createDebugUILogMessage(const String& logMessage);
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
