#include "../../include/MessageProtocol.h"
#include <esp_log.h>
#include <unordered_map>
#include <array>
#include <string>

static const char* TAG = "MessageProtocol";

namespace MessageProtocol {

// =============================================================================
// IMPLEMENTATION CLASSES (PIMPL Pattern)
// =============================================================================

class ExternalMessageTypeRegistry::Impl {
   public:
    std::unordered_map<std::string, ExternalMessageType> stringToEnumMap;
    std::array<const char*, 6> enumToStringArray;
    bool initialized = false;

    Impl() : initialized(false) {
        enumToStringArray.fill(nullptr);
    }

    void populateMappings() {
        // Map external message type strings to enums
        stringToEnumMap["StatusUpdate"] = ExternalMessageType::STATUS_UPDATE;
        stringToEnumMap["StatusMessage"] = ExternalMessageType::STATUS_MESSAGE;
        stringToEnumMap["GetStatus"] = ExternalMessageType::GET_STATUS;
        stringToEnumMap["GetAssets"] = ExternalMessageType::GET_ASSETS;
        stringToEnumMap["AssetResponse"] = ExternalMessageType::ASSET_RESPONSE;
        stringToEnumMap["SessionUpdate"] = ExternalMessageType::SESSION_UPDATE;

        // Map enum values to strings
        enumToStringArray[0] = "StatusUpdate";
        enumToStringArray[1] = "StatusMessage";
        enumToStringArray[2] = "GetStatus";
        enumToStringArray[3] = "GetAssets";
        enumToStringArray[4] = "AssetResponse";
        enumToStringArray[5] = "SessionUpdate";

        initialized = true;
    }
};

class InternalMessageTypeRegistry::Impl {
   public:
    std::unordered_map<std::string, InternalMessageType> stringToEnumMap;
    std::array<const char*, 800> enumToStringArray;
    bool initialized = false;

    Impl() : initialized(false) {
        enumToStringArray.fill(nullptr);
    }

    void populateMappings() {
        // Network messages
        stringToEnumMap["WIFI_STATUS"] = InternalMessageType::WIFI_STATUS;
        stringToEnumMap["NETWORK_INFO"] = InternalMessageType::NETWORK_INFO;
        stringToEnumMap["CONNECTION_STATUS"] = InternalMessageType::CONNECTION_STATUS;

        // UI messages
        stringToEnumMap["SCREEN_CHANGE"] = InternalMessageType::SCREEN_CHANGE;
        stringToEnumMap["UI_UPDATE"] = InternalMessageType::UI_UPDATE;
        stringToEnumMap["BUTTON_PRESS"] = InternalMessageType::BUTTON_PRESS;
        stringToEnumMap["UI_REFRESH"] = InternalMessageType::UI_REFRESH;

        // File system messages
        stringToEnumMap["SD_STATUS"] = InternalMessageType::SD_STATUS;
        stringToEnumMap["SD_FORMAT"] = InternalMessageType::SD_FORMAT;
        stringToEnumMap["SD_MOUNT"] = InternalMessageType::SD_MOUNT;

        // Audio messages
        stringToEnumMap["AUDIO_DEVICE_CHANGE"] = InternalMessageType::AUDIO_DEVICE_CHANGE;
        stringToEnumMap["AUDIO_STATE_UPDATE"] = InternalMessageType::AUDIO_STATE_UPDATE;
        stringToEnumMap["AUDIO_UI_REFRESH"] = InternalMessageType::AUDIO_UI_REFRESH;

        // System monitoring
        stringToEnumMap["MEMORY_STATUS"] = InternalMessageType::MEMORY_STATUS;
        stringToEnumMap["TASK_STATUS"] = InternalMessageType::TASK_STATUS;
        stringToEnumMap["PERFORMANCE_MONITOR"] = InternalMessageType::PERFORMANCE_MONITOR;

        // Hardware control
        stringToEnumMap["LED_UPDATE"] = InternalMessageType::LED_UPDATE;
        stringToEnumMap["DISPLAY_BRIGHTNESS"] = InternalMessageType::DISPLAY_BRIGHTNESS;

        // Core communication
        stringToEnumMap["CORE0_TO_CORE1"] = InternalMessageType::CORE0_TO_CORE1;
        stringToEnumMap["CORE1_TO_CORE0"] = InternalMessageType::CORE1_TO_CORE0;
        stringToEnumMap["TASK_SYNC"] = InternalMessageType::TASK_SYNC;

        // Populate reverse mapping (sample - add more as needed)
        enumToStringArray[static_cast<size_t>(InternalMessageType::WIFI_STATUS)] = "WIFI_STATUS";
        enumToStringArray[static_cast<size_t>(InternalMessageType::NETWORK_INFO)] = "NETWORK_INFO";
        enumToStringArray[static_cast<size_t>(InternalMessageType::CONNECTION_STATUS)] = "CONNECTION_STATUS";
        enumToStringArray[static_cast<size_t>(InternalMessageType::SCREEN_CHANGE)] = "SCREEN_CHANGE";
        enumToStringArray[static_cast<size_t>(InternalMessageType::UI_UPDATE)] = "UI_UPDATE";
        enumToStringArray[static_cast<size_t>(InternalMessageType::BUTTON_PRESS)] = "BUTTON_PRESS";
        enumToStringArray[static_cast<size_t>(InternalMessageType::UI_REFRESH)] = "UI_REFRESH";

        initialized = true;
    }
};

// =============================================================================
// EXTERNAL MESSAGE TYPE FUNCTIONS
// =============================================================================

const char* externalMessageTypeToString(ExternalMessageType type) {
    ExternalMessageTypeRegistry& registry = ExternalMessageTypeRegistry::getInstance();
    return registry.getString(type);
}

ExternalMessageType stringToExternalMessageType(const char* str) {
    if (!str) return ExternalMessageType::INVALID;
    ExternalMessageTypeRegistry& registry = ExternalMessageTypeRegistry::getInstance();
    return registry.getMessageType(str);
}

ExternalMessageType stringToExternalMessageType(const String& str) {
    return stringToExternalMessageType(str.c_str());
}

// =============================================================================
// INTERNAL MESSAGE TYPE FUNCTIONS
// =============================================================================

const char* internalMessageTypeToString(InternalMessageType type) {
    InternalMessageTypeRegistry& registry = InternalMessageTypeRegistry::getInstance();
    return registry.getString(type);
}

InternalMessageType stringToInternalMessageType(const char* str) {
    if (!str) return InternalMessageType::INVALID;
    InternalMessageTypeRegistry& registry = InternalMessageTypeRegistry::getInstance();
    return registry.getMessageType(str);
}

InternalMessageType stringToInternalMessageType(const String& str) {
    return stringToInternalMessageType(str.c_str());
}

// =============================================================================
// CATEGORY FUNCTIONS
// =============================================================================

ExternalMessageCategory getExternalMessageCategory(ExternalMessageType type) {
    switch (type) {
        case ExternalMessageType::STATUS_UPDATE:
        case ExternalMessageType::STATUS_MESSAGE:
        case ExternalMessageType::GET_STATUS:
            return ExternalMessageCategory::STATUS;
        case ExternalMessageType::GET_ASSETS:
        case ExternalMessageType::ASSET_RESPONSE:
            return ExternalMessageCategory::ASSETS;
        case ExternalMessageType::SESSION_UPDATE:
            return ExternalMessageCategory::SESSION;
        default:
            return ExternalMessageCategory::UNKNOWN;
    }
}

InternalMessageCategory getInternalMessageCategory(InternalMessageType type) {
    uint16_t value = static_cast<uint16_t>(type);

    if (value >= 100 && value < 200) return InternalMessageCategory::NETWORK;
    if (value >= 200 && value < 300) return InternalMessageCategory::UI;
    if (value >= 300 && value < 400) return InternalMessageCategory::FILESYSTEM;
    if (value >= 400 && value < 500) return InternalMessageCategory::AUDIO;
    if (value >= 500 && value < 600) return InternalMessageCategory::MONITORING;
    if (value >= 600 && value < 700) return InternalMessageCategory::HARDWARE;
    if (value >= 700 && value < 800) return InternalMessageCategory::CORE_COMM;

    return InternalMessageCategory::UNKNOWN;
}

// =============================================================================
// PRIORITY FUNCTIONS
// =============================================================================

MessagePriority getExternalMessagePriority(ExternalMessageType type) {
    switch (type) {
        case ExternalMessageType::STATUS_UPDATE:
        case ExternalMessageType::SESSION_UPDATE:
            return MessagePriority::MSG_HIGH;  // Real-time audio updates
        case ExternalMessageType::STATUS_MESSAGE:
        case ExternalMessageType::GET_STATUS:
            return MessagePriority::MSG_NORMAL;
        case ExternalMessageType::GET_ASSETS:
        case ExternalMessageType::ASSET_RESPONSE:
            return MessagePriority::MSG_LOW;  // Assets can be delayed
        default:
            return MessagePriority::MSG_NORMAL;
    }
}

MessagePriority getInternalMessagePriority(InternalMessageType type) {
    InternalMessageCategory category = getInternalMessageCategory(type);

    switch (category) {
        case InternalMessageCategory::UI:
        case InternalMessageCategory::CORE_COMM:
            return MessagePriority::MSG_HIGH;  // UI responsiveness is critical
        case InternalMessageCategory::AUDIO:
            return MessagePriority::MSG_HIGH;  // Audio updates are time-sensitive
        case InternalMessageCategory::NETWORK:
        case InternalMessageCategory::HARDWARE:
            return MessagePriority::MSG_NORMAL;
        case InternalMessageCategory::MONITORING:
        case InternalMessageCategory::FILESYSTEM:
            return MessagePriority::MSG_LOW;  // Background operations
        default:
            return MessagePriority::MSG_NORMAL;
    }
}

// =============================================================================
// EXTERNAL MESSAGE TYPE REGISTRY IMPLEMENTATION
// =============================================================================

ExternalMessageTypeRegistry::ExternalMessageTypeRegistry() : pImpl(new Impl()) {}

ExternalMessageTypeRegistry& ExternalMessageTypeRegistry::getInstance() {
    static ExternalMessageTypeRegistry instance;
    return instance;
}

ExternalMessageType ExternalMessageTypeRegistry::getMessageType(const char* str) const {
    if (!pImpl->initialized) {
        const_cast<ExternalMessageTypeRegistry*>(this)->init();
    }

    if (!str) return ExternalMessageType::INVALID;

    auto it = pImpl->stringToEnumMap.find(std::string(str));
    return (it != pImpl->stringToEnumMap.end()) ? it->second : ExternalMessageType::INVALID;
}

const char* ExternalMessageTypeRegistry::getString(ExternalMessageType type) const {
    if (!pImpl->initialized) {
        const_cast<ExternalMessageTypeRegistry*>(this)->init();
    }

    int16_t index = static_cast<int16_t>(type);
    if (index >= 0 && index < 6) {
        return pImpl->enumToStringArray[index];
    }
    return "INVALID";
}

bool ExternalMessageTypeRegistry::init() {
    if (!pImpl->initialized) {
        pImpl->populateMappings();
    }
    return pImpl->initialized;
}

// =============================================================================
// INTERNAL MESSAGE TYPE REGISTRY IMPLEMENTATION
// =============================================================================

InternalMessageTypeRegistry::InternalMessageTypeRegistry() : pImpl(new Impl()) {}

InternalMessageTypeRegistry::~InternalMessageTypeRegistry() {
    delete pImpl;
}

InternalMessageTypeRegistry& InternalMessageTypeRegistry::getInstance() {
    static InternalMessageTypeRegistry instance;
    return instance;
}

InternalMessageType InternalMessageTypeRegistry::getMessageType(const char* str) const {
    if (!pImpl->initialized) {
        const_cast<InternalMessageTypeRegistry*>(this)->init();
    }

    if (!str) return InternalMessageType::INVALID;

    auto it = pImpl->stringToEnumMap.find(std::string(str));
    return (it != pImpl->stringToEnumMap.end()) ? it->second : InternalMessageType::INVALID;
}

const char* InternalMessageTypeRegistry::getString(InternalMessageType type) const {
    if (!pImpl->initialized) {
        const_cast<InternalMessageTypeRegistry*>(this)->init();
    }

    uint16_t index = static_cast<uint16_t>(type);
    if (index < 800 && pImpl->enumToStringArray[index] != nullptr) {
        return pImpl->enumToStringArray[index];
    }
    return "UNKNOWN";
}

bool InternalMessageTypeRegistry::init() {
    if (!pImpl->initialized) {
        pImpl->populateMappings();
    }
    return pImpl->initialized;
}

// All legacy functions and MessageTypeRegistry implementation removed

}  // namespace MessageProtocol
