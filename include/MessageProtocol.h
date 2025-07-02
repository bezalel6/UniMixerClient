#pragma once

#include <Arduino.h>
#include <unordered_map>
#include <array>
#include <string>

/**
 * DUAL MESSAGE TYPE SYSTEM - Clear Separation Architecture
 *
 * This system provides separate, dedicated enums for external vs internal messages
 * to ensure unmistakable separation and type safety:
 *
 * EXTERNAL MESSAGES: Cross transport boundaries (Serial in normal mode, network in OTA mode)
 * - Security validation required
 * - JSON serialization/deserialization
 * - Cross-device communication
 * - Performance: O(1) integer comparisons instead of strings
 *
 * INTERNAL MESSAGES: ESP32 internal communication only
 * - Maximum performance, zero-cost abstractions
 * - Core-aware routing
 * - Local hardware control
 * - UI updates and system status
 */

namespace MessageProtocol {

// =============================================================================
// ALL ENUM DECLARATIONS FIRST
// =============================================================================

/**
 * External Message Types for messages that cross transport boundaries
 * These are the actual message types used in the system
 */
enum class ExternalMessageType : int16_t {
    /// Invalid or unknown message type
    INVALID = -1,

    /// Status update message containing session information
    /// Maps to: "StatusUpdate"
    STATUS_UPDATE = 0,

    /// Status broadcast message
    /// Maps to: "StatusMessage"
    STATUS_MESSAGE = 1,

    /// Request for device status
    /// Maps to: "GetStatus"
    GET_STATUS = 2,

    /// Request for asset data (e.g., process icons)
    /// Maps to: "GetAssets"
    GET_ASSETS = 3,

    /// Response containing asset data
    /// Maps to: "AssetResponse"
    ASSET_RESPONSE = 4,

    /// Individual session update (used within StatusUpdate)
    /// Maps to: "SessionUpdate"
    SESSION_UPDATE = 5
};

/**
 * Internal Message Types for ESP32 internal communication only
 * These messages are for local hardware control and UI updates
 */
enum class InternalMessageType : uint16_t {
    // === INVALID/UNKNOWN ===
    INVALID = 0,
    UNKNOWN = 1,

    // === NETWORK/CONNECTIVITY (Internal Status) ===
    WIFI_STATUS = 100,
    NETWORK_INFO = 101,
    CONNECTION_STATUS = 102,

    // === UI/DISPLAY (Internal Control) ===
    SCREEN_CHANGE = 200,
    UI_UPDATE = 201,
    BUTTON_PRESS = 202,
    UI_REFRESH = 203,
    DEBUG_UI_LOG = 204,

    // === FILE SYSTEM (Internal Hardware) ===
    SD_STATUS = 300,
    SD_FORMAT = 301,
    SD_MOUNT = 302,

    // === AUDIO SYSTEM (Internal Updates) ===
    AUDIO_DEVICE_CHANGE = 400,
    AUDIO_STATE_UPDATE = 401,
    AUDIO_UI_REFRESH = 402,

    // === SYSTEM MONITORING (Internal Status) ===
    MEMORY_STATUS = 500,
    TASK_STATUS = 501,
    PERFORMANCE_MONITOR = 502,

    // === HARDWARE CONTROL (Internal Operations) ===
    LED_UPDATE = 600,
    DISPLAY_BRIGHTNESS = 601,

    // === CORE COMMUNICATION (Inter-Core Messages) ===
    CORE0_TO_CORE1 = 700,
    CORE1_TO_CORE0 = 701,
    TASK_SYNC = 702
};

/**
 * Message categorization for routing and security
 */
enum class ExternalMessageCategory : uint8_t {
    STATUS,   // Status-related messages (STATUS_UPDATE, STATUS_MESSAGE, GET_STATUS)
    ASSETS,   // Asset-related messages (GET_ASSETS, ASSET_RESPONSE)
    SESSION,  // Session-related messages (SESSION_UPDATE)
    UNKNOWN
};

enum class InternalMessageCategory : uint8_t {
    NETWORK,     // WiFi, network connectivity
    UI,          // Screen changes, UI updates
    FILESYSTEM,  // SD card operations
    AUDIO,       // Audio device management
    MONITORING,  // System monitoring
    HARDWARE,    // LED, display controls
    CORE_COMM,   // Inter-core communication
    UNKNOWN
};

/**
 * Priority levels for message processing
 * Using MSG_ prefix to avoid conflicts with Arduino LOW/HIGH macros
 */
enum class MessagePriority : uint8_t {
    MSG_LOW = 0,
    MSG_NORMAL = 1,
    MSG_HIGH = 2,
    MSG_CRITICAL = 3
};

// =============================================================================
// FUNCTION DECLARATIONS (After all enums are declared)
// =============================================================================

/**
 * Convert ExternalMessageType enum to string (for JSON serialization)
 */
const char* externalMessageTypeToString(ExternalMessageType type);

/**
 * Convert string to ExternalMessageType enum (for JSON deserialization)
 */
ExternalMessageType stringToExternalMessageType(const char* str);
ExternalMessageType stringToExternalMessageType(const String& str);

/**
 * Convert InternalMessageType enum to string (for debugging)
 */
const char* internalMessageTypeToString(InternalMessageType type);

/**
 * Convert string to InternalMessageType enum (for debugging/config)
 */
InternalMessageType stringToInternalMessageType(const char* str);
InternalMessageType stringToInternalMessageType(const String& str);

/**
 * Type validation functions
 */
inline bool isValidExternalMessageType(ExternalMessageType type) {
    return type != ExternalMessageType::INVALID &&
           static_cast<int16_t>(type) >= 0 &&
           static_cast<int16_t>(type) <= 5;
}

inline bool isValidInternalMessageType(InternalMessageType type) {
    return type != InternalMessageType::INVALID && type != InternalMessageType::UNKNOWN;
}

/**
 * Category functions
 */
ExternalMessageCategory getExternalMessageCategory(ExternalMessageType type);
InternalMessageCategory getInternalMessageCategory(InternalMessageType type);

/**
 * Priority functions
 */
MessagePriority getExternalMessagePriority(ExternalMessageType type);
MessagePriority getInternalMessagePriority(InternalMessageType type);

/**
 * Core routing decisions for internal messages
 */
inline bool shouldRouteToCore1(InternalMessageType type) {
    InternalMessageCategory category = getInternalMessageCategory(type);
    switch (category) {
        case InternalMessageCategory::NETWORK:
        case InternalMessageCategory::FILESYSTEM:
        case InternalMessageCategory::AUDIO:
        case InternalMessageCategory::MONITORING:
        case InternalMessageCategory::HARDWARE:
            return true;  // Core 1 handles system operations

        case InternalMessageCategory::UI:
        case InternalMessageCategory::CORE_COMM:
            return false;  // Core 0 handles UI operations

        default:
            return false;  // Default to Core 0 for safety
    }
}

// =============================================================================
// CLASS DECLARATIONS (After enums and functions)
// =============================================================================

/**
 * External Message Type Registry for transport serialization
 * Implementation details are hidden to avoid compilation issues with STL containers
 */
class ExternalMessageTypeRegistry {
   public:
    static ExternalMessageTypeRegistry& getInstance();

    ExternalMessageType getMessageType(const char* str) const;
    const char* getString(ExternalMessageType type) const;
    bool init();

   private:
    // Forward declaration - implementation in .cpp file
    ExternalMessageTypeRegistry();
    ~ExternalMessageTypeRegistry() = default;

    // Hide implementation details
    class Impl;
    Impl* pImpl;
};

/**
 * Internal Message Type Registry for debugging and configuration
 * Implementation details are hidden to avoid compilation issues with STL containers
 */
class InternalMessageTypeRegistry {
   public:
    static InternalMessageTypeRegistry& getInstance();

    InternalMessageType getMessageType(const char* str) const;
    const char* getString(InternalMessageType type) const;
    bool init();

   private:
    // Forward declaration - implementation in .cpp file
    InternalMessageTypeRegistry();
    ~InternalMessageTypeRegistry();

    // Hide implementation details
    class Impl;
    Impl* pImpl;
};

// =============================================================================
// CONVENIENCE MACROS - Updated for Dual Types
// =============================================================================

#define EXTERNAL_MSG_TYPE_TO_STRING(type) MessageProtocol::externalMessageTypeToString(type)
#define INTERNAL_MSG_TYPE_TO_STRING(type) MessageProtocol::internalMessageTypeToString(type)
#define STRING_TO_EXTERNAL_MSG_TYPE(str) MessageProtocol::stringToExternalMessageType(str)
#define STRING_TO_INTERNAL_MSG_TYPE(str) MessageProtocol::stringToInternalMessageType(str)
#define IS_VALID_EXTERNAL_MSG_TYPE(type) MessageProtocol::isValidExternalMessageType(type)
#define IS_VALID_INTERNAL_MSG_TYPE(type) MessageProtocol::isValidInternalMessageType(type)
#define EXTERNAL_MSG_PRIORITY(type) MessageProtocol::getExternalMessagePriority(type)
#define INTERNAL_MSG_PRIORITY(type) MessageProtocol::getInternalMessagePriority(type)
#define EXTERNAL_MSG_CATEGORY(type) MessageProtocol::getExternalMessageCategory(type)
#define INTERNAL_MSG_CATEGORY(type) MessageProtocol::getInternalMessageCategory(type)
#define ROUTE_TO_CORE1(type) MessageProtocol::shouldRouteToCore1(type)

// =============================================================================
// NUMERIC-ONLY MACROS - No String Conversions (Performance & Consistency)
// =============================================================================

// JSON Serialization (Enum -> Int)
#define SERIALIZE_EXTERNAL_MSG_TYPE(type) static_cast<int>(type)
#define SERIALIZE_INTERNAL_MSG_TYPE(type) static_cast<int>(type)

// JSON Deserialization (Int -> Enum)
#define DESERIALIZE_EXTERNAL_MSG_TYPE(jsonDoc, field, defaultType) \
    static_cast<MessageProtocol::ExternalMessageType>(             \
        jsonDoc[field] | static_cast<int>(defaultType))

#define DESERIALIZE_INTERNAL_MSG_TYPE(jsonDoc, field, defaultType) \
    static_cast<MessageProtocol::InternalMessageType>(             \
        jsonDoc[field] | static_cast<int>(defaultType))

// Logging (Enum as Numbers)
#define LOG_EXTERNAL_MSG_TYPE(type) static_cast<int>(type)
#define LOG_INTERNAL_MSG_TYPE(type) static_cast<int>(type)

// Type Casting Helpers
#define CAST_TO_EXTERNAL_MSG_TYPE(intValue) static_cast<MessageProtocol::ExternalMessageType>(intValue)
#define CAST_TO_INTERNAL_MSG_TYPE(intValue) static_cast<MessageProtocol::InternalMessageType>(intValue)

// Safe Deserialization with Validation
#define SAFE_DESERIALIZE_EXTERNAL_MSG_TYPE(jsonDoc, field)                                                               \
    ([&]() {                                                                                                             \
        int typeNum = jsonDoc[field] | static_cast<int>(MessageProtocol::ExternalMessageType::INVALID);                  \
        auto type = static_cast<MessageProtocol::ExternalMessageType>(typeNum);                                          \
        return MessageProtocol::isValidExternalMessageType(type) ? type : MessageProtocol::ExternalMessageType::INVALID; \
    })()

#define SAFE_DESERIALIZE_INTERNAL_MSG_TYPE(jsonDoc, field)                                                               \
    ([&]() {                                                                                                             \
        int typeNum = jsonDoc[field] | static_cast<int>(MessageProtocol::InternalMessageType::INVALID);                  \
        auto type = static_cast<MessageProtocol::InternalMessageType>(typeNum);                                          \
        return MessageProtocol::isValidInternalMessageType(type) ? type : MessageProtocol::InternalMessageType::INVALID; \
    })()

}  // namespace MessageProtocol

// =============================================================================
// EXTERNAL JSON FIELD NAME CONSTANTS (camelCase only)
// =============================================================================

namespace MessageProtocol {
namespace JsonFields {

// =============================================================================
// CORE MESSAGE FIELDS - camelCase format
// =============================================================================

// Core message identification fields
constexpr const char* MESSAGE_TYPE = "messageType";
constexpr const char* REQUEST_ID = "requestId";
constexpr const char* DEVICE_ID = "deviceId";
constexpr const char* ORIGINATING_DEVICE_ID = "originatingDeviceId";
constexpr const char* TIMESTAMP = "timestamp";

// =============================================================================
// AUDIO STATUS MESSAGE FIELDS - camelCase format
// =============================================================================

// Session data fields
constexpr const char* SESSIONS = "sessions";
constexpr const char* ACTIVE_SESSION_COUNT = "activeSessionCount";

// Individual session fields
constexpr const char* PROCESS_ID = "processId";
constexpr const char* PROCESS_NAME = "processName";
constexpr const char* DISPLAY_NAME = "displayName";
constexpr const char* VOLUME = "volume";
constexpr const char* IS_MUTED = "isMuted";
constexpr const char* STATE = "state";

// Default device fields
constexpr const char* DEFAULT_DEVICE = "defaultDevice";
constexpr const char* FRIENDLY_NAME = "friendlyName";
constexpr const char* DATA_FLOW = "dataFlow";
constexpr const char* DEVICE_ROLE = "deviceRole";

// Message metadata fields
constexpr const char* REASON = "reason";
constexpr const char* ORIGINATING_REQUEST_ID = "originatingRequestId";

// =============================================================================
// ASSET REQUEST/RESPONSE FIELDS - camelCase format
// =============================================================================

constexpr const char* SUCCESS = "success";
constexpr const char* ERROR_MESSAGE = "errorMessage";
constexpr const char* METADATA = "metadata";
constexpr const char* WIDTH = "width";
constexpr const char* HEIGHT = "height";
constexpr const char* FORMAT = "format";
constexpr const char* ASSET_DATA = "assetData";

}  // namespace JsonFields
}  // namespace MessageProtocol
