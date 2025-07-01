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
 * EXTERNAL MESSAGES: Cross transport boundaries (Serial/MQTT/Network)
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

/**
 * @deprecated Use ExternalMessageType or InternalMessageType instead
 * Legacy unified enum for backward compatibility only
 * This will be removed in a future version
 */
enum class MessageType : uint16_t {
    // Map to external types for compatibility
    INVALID = static_cast<uint16_t>(ExternalMessageType::INVALID),
    STATUS_UPDATE = static_cast<uint16_t>(ExternalMessageType::STATUS_UPDATE),
    STATUS_MESSAGE = static_cast<uint16_t>(ExternalMessageType::STATUS_MESSAGE),
    GET_STATUS = static_cast<uint16_t>(ExternalMessageType::GET_STATUS),
    GET_ASSETS = static_cast<uint16_t>(ExternalMessageType::GET_ASSETS),
    ASSET_RESPONSE = static_cast<uint16_t>(ExternalMessageType::ASSET_RESPONSE),
    SESSION_UPDATE = static_cast<uint16_t>(ExternalMessageType::SESSION_UPDATE),

    // Map some internal types for compatibility
    WIFI_STATUS = static_cast<uint16_t>(InternalMessageType::WIFI_STATUS),
    NETWORK_INFO = static_cast<uint16_t>(InternalMessageType::NETWORK_INFO),
    CONNECTION_STATUS = static_cast<uint16_t>(InternalMessageType::CONNECTION_STATUS),
    SCREEN_CHANGE = static_cast<uint16_t>(InternalMessageType::SCREEN_CHANGE),
    UI_UPDATE = static_cast<uint16_t>(InternalMessageType::UI_UPDATE),
    BUTTON_PRESS = static_cast<uint16_t>(InternalMessageType::BUTTON_PRESS),
    SD_STATUS = static_cast<uint16_t>(InternalMessageType::SD_STATUS),
    SD_FORMAT = static_cast<uint16_t>(InternalMessageType::SD_FORMAT),

    // Legacy aliases for backward compatibility
    HEARTBEAT = static_cast<uint16_t>(ExternalMessageType::STATUS_MESSAGE),  // Map to STATUS_MESSAGE
    SET_VOLUME = 1000,                                                       // Legacy value
    MUTE_PROCESS = 1001,                                                     // Legacy value
    UNMUTE_PROCESS = 1002,                                                   // Legacy value
    SET_MASTER_VOLUME = 1003,                                                // Legacy value
    MUTE_MASTER = 1004,                                                      // Legacy value
    UNMUTE_MASTER = 1005,                                                    // Legacy value
    LOGO_REQUEST = 1006,                                                     // Legacy value
    LOGO_DATA = 1007,                                                        // Legacy value
    OTA_CHECK = 1008,                                                        // Legacy value
    OTA_AVAILABLE = 1009,                                                    // Legacy value
    OTA_START = 1010,                                                        // Legacy value
    OTA_PROGRESS = 1011,                                                     // Legacy value
    OTA_COMPLETE = 1012,                                                     // Legacy value
    OTA_ERROR = 1013,                                                        // Legacy value
    DEVICE_INFO = 1014,                                                      // Legacy value
    DEVICE_CONFIG = 1015,                                                    // Legacy value
    DEVICE_STATUS = 1016,                                                    // Legacy value
    DEBUG_INFO = 1017,                                                       // Legacy value
    LOG_MESSAGE = 1018,                                                      // Legacy value
    PERFORMANCE_STATS = 1019,                                                // Legacy value
    FILE_OPERATION = 1020,                                                   // Legacy value

    MAX_MESSAGE_TYPE = 65535
};

/**
 * @deprecated Legacy category enum
 */
enum class MessageCategory : uint8_t {
    LEGACY_CAT_SYSTEM,
    LEGACY_CAT_AUDIO,
    LEGACY_CAT_ASSET,
    LEGACY_CAT_OTA,
    LEGACY_CAT_DEVICE,
    LEGACY_CAT_NETWORK,
    LEGACY_CAT_UI,
    LEGACY_CAT_FILESYSTEM,
    LEGACY_CAT_DEBUG,
    LEGACY_CAT_UNKNOWN
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

/**
 * @deprecated Legacy conversion functions - use specific type functions instead
 */
[[deprecated("Use externalMessageTypeToString() or internalMessageTypeToString() instead")]]
const char* messageTypeToString(MessageType type);

[[deprecated("Use stringToExternalMessageType() or stringToInternalMessageType() instead")]]
MessageType stringToMessageType(const char* str);

[[deprecated("Use stringToExternalMessageType() or stringToInternalMessageType() instead")]]
MessageType stringToMessageType(const String& str);

[[deprecated("Use isValidExternalMessageType() or isValidInternalMessageType() instead")]]
inline bool isValidMessageType(MessageType type) {
    return type != MessageType::INVALID;
}

[[deprecated("Use getExternalMessageCategory() or getInternalMessageCategory() instead")]]
MessageCategory getMessageCategory(MessageType type);

[[deprecated("Use getExternalMessagePriority() or getInternalMessagePriority() instead")]]
MessagePriority getMessagePriority(MessageType type);

[[deprecated("Use shouldRouteToCore1(InternalMessageType) instead")]]
inline bool shouldRouteToCore1(MessageType type) {
    // Convert to internal type and route accordingly
    if (static_cast<uint16_t>(type) >= 100) {
        InternalMessageType internalType = static_cast<InternalMessageType>(type);
        return shouldRouteToCore1(internalType);
    }
    return false;  // External messages don't route to cores directly
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

// Legacy registry for backward compatibility
class MessageTypeRegistry {
   public:
    static MessageTypeRegistry& getInstance();
    MessageType getMessageType(const char* str) const;
    const char* getString(MessageType type) const;
    bool init();

   private:
    // Forward declaration - implementation in .cpp file
    MessageTypeRegistry();
    ~MessageTypeRegistry() = default;

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

// Legacy macros for backward compatibility (deprecated)
#define MSG_TYPE_TO_STRING(type) MessageProtocol::messageTypeToString(type)
#define STRING_TO_MSG_TYPE(str) MessageProtocol::stringToMessageType(str)
#define IS_VALID_MSG_TYPE(type) MessageProtocol::isValidMessageType(type)
#define MSG_PRIORITY(type) MessageProtocol::getMessagePriority(type)
#define MSG_CATEGORY(type) MessageProtocol::getMessageCategory(type)

}  // namespace MessageProtocol
