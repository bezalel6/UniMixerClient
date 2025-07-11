#include "MessageData.h"
#include <esp_log.h>

namespace Messaging {

// =============================================================================
// SAFE JSON PARSING UTILITIES
// =============================================================================

/**
 * Safely extract a value from a JSON object with fallback
 * Handles missing fields, null values, and type mismatches
 */
template <typename T>
T safeGetJsonValue(const JsonObject &obj, const char *field, const T &fallback) {
    JsonVariant variant = obj[field];
    if (variant.isNull() || !variant.is<T>()) {
        return fallback;
    }
    return variant.as<T>();
}

/**
 * Overload for JsonDocument (treats as JsonObject)
 */
template <typename T>
T safeGetJsonValue(const JsonDocument &doc, const char *field, const T &fallback) {
    JsonVariantConst variant = doc[field];
    if (variant.isNull() || !variant.is<T>()) {
        return fallback;
    }
    return variant.as<T>();
}

/**
 * Specialized version for String type to handle null and empty cases
 */
template <>
String safeGetJsonValue<String>(const JsonObject &obj, const char *field, const String &fallback) {
    JsonVariant variant = obj[field];
    if (variant.isNull()) {
        return fallback;
    }

    // Handle both string and numeric types that can be converted to string
    if (variant.is<const char *>() || variant.is<String>()) {
        return variant.as<String>();
    } else if (variant.is<int>() || variant.is<long>() || variant.is<double>()) {
        return variant.as<String>();
    }

    return fallback;
}

/**
 * String specialization for JsonDocument
 */
template <>
String safeGetJsonValue<String>(const JsonDocument &doc, const char *field, const String &fallback) {
    JsonVariantConst variant = doc[field];
    if (variant.isNull()) {
        return fallback;
    }

    // Handle both string and numeric types that can be converted to string
    if (variant.is<const char *>() || variant.is<String>()) {
        return variant.as<String>();
    } else if (variant.is<int>() || variant.is<long>() || variant.is<double>()) {
        return variant.as<String>();
    }

    return fallback;
}

/**
 * Convenience macro for safe JSON field extraction
 */
#define SAFE_GET_JSON(obj, field, type, fallback) \
    safeGetJsonValue<type>(obj, MessageProtocol::JsonFields::field, fallback)

// =============================================================================
// EXTERNAL MESSAGE METHODS
// =============================================================================

bool ExternalMessage::isSelfOriginated() const {
    return deviceId == Config::getDeviceId() ||
           originatingDeviceId == Config::getDeviceId();
}

bool ExternalMessage::requiresResponse() const {
    switch (messageType) {
        case MessageProtocol::ExternalMessageType::GET_STATUS:
        case MessageProtocol::ExternalMessageType::GET_ASSETS:
            return true;
        default:
            return false;
    }
}

// =============================================================================
// INTERNAL MESSAGE CONSTRUCTORS
// =============================================================================

InternalMessage::InternalMessage() {
    messageType = MessageProtocol::InternalMessageType::INVALID;
    timestamp = millis();
    priority = static_cast<uint8_t>(
        MessageProtocol::getInternalMessagePriority(messageType));
}

InternalMessage::InternalMessage(MessageProtocol::InternalMessageType type,
                                 const void *payload, size_t size)
    : messageType(type), dataSize(size) {
    timestamp = millis();
    priority =
        static_cast<uint8_t>(MessageProtocol::getInternalMessagePriority(type));

    if (payload && size > 0) {
        data.reset(new uint8_t[size]);
        memcpy(data.get(), payload, size);
    }
}

// =============================================================================
// ASSET RESPONSE DATA CONSTRUCTOR
// =============================================================================

AssetResponseData::AssetResponseData(const ExternalMessage &external) {
    requestId = external.requestId;
    deviceId = external.deviceId;
    timestamp = external.timestamp;

    // Extract asset-specific fields from parsed data
    processName = external.getString("processName", "");
    success = external.getBool("success", false);
    errorMessage = external.getString("errorMessage", "");
    assetDataBase64 = external.getString("assetData", "");

    if (external.hasField("metadata")) {
        // In ArduinoJson v7, use safe access with explicit String copying
        if (external.isObject("metadata")) {
            // Safe access to the fields
            width = external.parsedData["metadata"]["width"].as<int>();
            height = external.parsedData["metadata"]["height"].as<int>();

            // Create explicit String copy to avoid reference issues
            const char *formatPtr = external.parsedData["metadata"]["format"].as<const char *>();
            format = formatPtr ? String(formatPtr) : String("");
        }
    }
}
// =============================================================================
// MESSAGE FACTORY IMPLEMENTATIONS - EXTERNAL MESSAGES ONLY
// =============================================================================
// Internal message factories are now generated by macros in MessageData.h

ExternalMessage MessageFactory::createStatusRequest(const string &deviceId) {
    string devId = STRING_IS_EMPTY(deviceId) ? Config::getDeviceId() : deviceId;
    ExternalMessage message(MessageProtocol::ExternalMessageType::GET_STATUS,
                            Config::generateRequestId(), devId);
    return message;
}

ExternalMessage MessageFactory::createAssetRequest(const string &processName,
                                                   const string &deviceId) {
    string devId = STRING_IS_EMPTY(deviceId) ? Config::getDeviceId() : deviceId;
    ExternalMessage message(MessageProtocol::ExternalMessageType::GET_ASSETS,
                            Config::generateRequestId(), devId);

    // Add process name to parsed data
    message.parsedData["processName"] = STRING_C_STR(processName);
    return message;
}

// =============================================================================
// MESSAGE PARSER IMPLEMENTATIONS
// =============================================================================

ParseResult<MessageProtocol::ExternalMessageType>
MessageParser::parseExternalMessageType(const string &jsonPayload) {
    const char *TAG = "MessageParser";

    if (STRING_IS_EMPTY(jsonPayload)) {
        return ParseResult<MessageProtocol::ExternalMessageType>::createError(
            STRING_FROM_LITERAL("Empty JSON payload"));
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, STRING_C_STR(jsonPayload));

    if (error) {
        string errorMsg = STRING_FROM_LITERAL("JSON deserialization failed: ") + STRING_FROM_CSTR(error.c_str());
        ESP_LOGW(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<MessageProtocol::ExternalMessageType>::createError(
            errorMsg);
    }

    JsonObject obj = doc.as<JsonObject>();
    if (!obj[MessageProtocol::JsonFields::MESSAGE_TYPE].is<int>()) {
        return ParseResult<MessageProtocol::ExternalMessageType>::createError(
            STRING_FROM_LITERAL("Missing messageType field"));
    }

    int typeValue = obj[MessageProtocol::JsonFields::MESSAGE_TYPE];
    auto messageType =
        static_cast<MessageProtocol::ExternalMessageType>(typeValue);

    if (!MessageProtocol::isValidExternalMessageType(messageType)) {
        return ParseResult<MessageProtocol::ExternalMessageType>::createError(
            STRING_FROM_LITERAL("Invalid messageType value"));
    }

    return ParseResult<MessageProtocol::ExternalMessageType>::createSuccess(
        messageType);
}

ParseResult<ExternalMessage>
MessageParser::parseExternalMessage(const string &jsonPayload) {
    const char *TAG = "MessageParser";
    using namespace MessageProtocol::JsonFields;

    if (STRING_IS_EMPTY(jsonPayload)) {
        return ParseResult<ExternalMessage>::createError(STRING_FROM_LITERAL("Empty JSON payload"));
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, STRING_C_STR(jsonPayload));

    if (error) {
        string errorMsg = STRING_FROM_LITERAL("JSON deserialization failed: ") + STRING_FROM_CSTR(error.c_str());
        ESP_LOGW(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<ExternalMessage>::createError(errorMsg);
    }
    ESP_LOGW(TAG, "Got payload: %s", STRING_C_STR(jsonPayload));
    JsonObject obj = doc.as<JsonObject>();

    // Parse message type
    if (!obj[MESSAGE_TYPE].is<int>()) {
        return ParseResult<ExternalMessage>::createError(
            STRING_FROM_LITERAL("Missing messageType field"));
    }

    int typeValue = obj[MESSAGE_TYPE];
    auto messageType =
        static_cast<MessageProtocol::ExternalMessageType>(typeValue);

    if (!MessageProtocol::isValidExternalMessageType(messageType)) {
        return ParseResult<ExternalMessage>::createError(
            STRING_FROM_LITERAL("Invalid messageType value"));
    }

    // Parse other fields using safe extraction
    string requestId = SAFE_GET_JSON(obj, REQUEST_ID, string, STRING_EMPTY);
    string deviceId = SAFE_GET_JSON(obj, DEVICE_ID, string, STRING_EMPTY);
    string originatingDeviceId = SAFE_GET_JSON(obj, ORIGINATING_DEVICE_ID, string, STRING_EMPTY);
    unsigned long timestamp = SAFE_GET_JSON(obj, TIMESTAMP, unsigned long, 0);

    if (timestamp == 0) {
        timestamp = millis();
    }

    // Create message
    ExternalMessage message(messageType, requestId, deviceId);
    message.originatingDeviceId = originatingDeviceId;
    message.timestamp = timestamp;
    message.validated = true;

    // FIXED: Instead of deep copying, deserialize directly into message.parsedData
    deserializeJson(message.parsedData, STRING_C_STR(jsonPayload));

    ESP_LOGD(TAG, "Successfully parsed external message: type=%d, deviceId=%s",
             static_cast<int>(messageType), STRING_C_STR(deviceId));

    return ParseResult<ExternalMessage>::createSuccess(message);
}

bool MessageParser::shouldIgnoreMessage(const ExternalMessage &message,
                                        const string &myDeviceId) {
    // Ignore invalid messages
    if (message.messageType == MessageProtocol::ExternalMessageType::INVALID) {
        return true;
    }

    // Ignore self-originated messages
    if (message.deviceId == myDeviceId) {
        return true;
    }

    // Ignore messages from our own device ID in originatingDeviceId
    if (!STRING_IS_EMPTY(message.originatingDeviceId) &&
        message.originatingDeviceId == myDeviceId) {
        return true;
    }

    return false;
}

ParseResult<AudioStatusData>
MessageParser::parseAudioStatusData(const ExternalMessage &message) {
    const char *TAG = "MessageParser";
    using namespace MessageProtocol::JsonFields;

    AudioStatusData data;

    if (message.messageType !=
            MessageProtocol::ExternalMessageType::STATUS_UPDATE &&
        message.messageType !=
            MessageProtocol::ExternalMessageType::STATUS_MESSAGE) {
        return ParseResult<AudioStatusData>::createError(
            STRING_FROM_LITERAL("Invalid message type for audio status"));
    }

    try {
        // Extract sessions
        if (message.parsedData[SESSIONS].is<JsonArray>()) {
            // Workaround for ArduinoJson v7 false positive: use size-based iteration
            auto sessionsArray = message.parsedData[SESSIONS];
            for (size_t i = 0; i < sessionsArray.size(); i++) {
                auto sessionVariant = sessionsArray[i];
                if (sessionVariant.is<JsonObject>()) {
                    // Safe field access with explicit String copying
                    SessionStatusData session;
                    session.processId = sessionVariant[PROCESS_ID].as<int>();

                    // Create explicit String copies to avoid reference issues
                    const char *processNamePtr = sessionVariant[PROCESS_NAME].as<const char *>();
                    session.processName = processNamePtr ? String(processNamePtr) : String("");

                    const char *displayNamePtr = sessionVariant[DISPLAY_NAME].as<const char *>();
                    session.displayName = displayNamePtr ? String(displayNamePtr) : String("");

                    session.volume = sessionVariant[VOLUME].as<float>();
                    session.isMuted = sessionVariant[IS_MUTED].as<bool>();

                    const char *statePtr = sessionVariant[STATE].as<const char *>();
                    session.state = statePtr ? String(statePtr) : String("");

                    data.sessions.push_back(session);
                }
            }
        }

        // Extract default device
        if (message.parsedData[DEFAULT_DEVICE].is<JsonObject>()) {
            // Use safe field access with explicit String copying
            JsonVariantConst defaultDeviceVar = message.parsedData[DEFAULT_DEVICE];

            // Create explicit String copies to avoid reference issues
            const char *friendlyNamePtr = defaultDeviceVar[FRIENDLY_NAME].as<const char *>();
            data.defaultDevice.friendlyName = friendlyNamePtr ? String(friendlyNamePtr) : String("");

            data.defaultDevice.volume = defaultDeviceVar[VOLUME].as<float>();
            data.defaultDevice.isMuted = defaultDeviceVar[IS_MUTED].as<bool>();

            const char *dataFlowPtr = defaultDeviceVar[DATA_FLOW].as<const char *>();
            data.defaultDevice.dataFlow = dataFlowPtr ? String(dataFlowPtr) : String("");

            const char *deviceRolePtr = defaultDeviceVar[DEVICE_ROLE].as<const char *>();
            data.defaultDevice.deviceRole = deviceRolePtr ? String(deviceRolePtr) : String("");

            data.hasDefaultDevice = true;
        }

        // Extract metadata using safe extraction
        data.timestamp = message.timestamp;
        data.reason = SAFE_GET_JSON(message.parsedData, REASON, String, "");
        data.originatingDeviceId = SAFE_GET_JSON(message.parsedData, ORIGINATING_DEVICE_ID, String, "");
        data.originatingRequestId = SAFE_GET_JSON(message.parsedData, ORIGINATING_REQUEST_ID, String, "");
        data.activeSessionCount = SAFE_GET_JSON(message.parsedData, ACTIVE_SESSION_COUNT, int, 0);

    } catch (const std::exception &e) {
        string errorMsg = STRING_FROM_LITERAL("Error parsing audio status data: ") + STRING_FROM_CSTR(e.what());
        ESP_LOGE(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<AudioStatusData>::createError(errorMsg);
    }

    return ParseResult<AudioStatusData>::createSuccess(data);
}

ParseResult<AssetResponseData>
MessageParser::parseAssetResponseData(const ExternalMessage &message) {
    const char *TAG = "MessageParser";

    if (message.messageType !=
        MessageProtocol::ExternalMessageType::ASSET_RESPONSE) {
        return ParseResult<AssetResponseData>::createError(
            STRING_FROM_LITERAL("Invalid message type for asset response"));
    }

    try {
        AssetResponseData data(message);
        return ParseResult<AssetResponseData>::createSuccess(data);
    } catch (const std::exception &e) {
        string errorMsg = STRING_FROM_LITERAL("Error parsing asset response data: ") + STRING_FROM_CSTR(e.what());
        ESP_LOGE(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<AssetResponseData>::createError(errorMsg);
    }
}

// =============================================================================
// MESSAGE SERIALIZER IMPLEMENTATIONS
// ===========================================================================
//

ParseResult<string>
MessageSerializer::serializeInternalMessage(const InternalMessage &message) {
    const char *TAG = "MessageSerializer";

    try {
        JsonDocument doc;

        doc["messageType"] = static_cast<int>(message.messageType);
        doc["timestamp"] = message.timestamp;
        doc["priority"] = message.priority;
        doc["dataSize"] = message.dataSize;
        doc["requiresResponse"] = message.requiresResponse;

        string jsonString;
        serializeJson(doc, jsonString);

        return ParseResult<string>::createSuccess(jsonString);

    } catch (const std::exception &e) {
        string errorMsg = STRING_FROM_LITERAL("Error serializing internal message: ") + STRING_FROM_CSTR(e.what());
        ESP_LOGE(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<string>::createError(errorMsg);
    }
}

ParseResult<string>
MessageSerializer::createStatusResponse(const AudioStatusData &data) {
    const char *TAG = "MessageSerializer";
    using namespace MessageProtocol::JsonFields;

    try {
        JsonDocument doc;

        doc[MESSAGE_TYPE] =
            static_cast<int>(MessageProtocol::ExternalMessageType::STATUS_MESSAGE);
        doc[DEVICE_ID] = Config::getDeviceId();
        doc[TIMESTAMP] = data.timestamp;
        doc[ACTIVE_SESSION_COUNT] = data.activeSessionCount;

        if (!STRING_IS_EMPTY(data.reason)) {
            doc[REASON] = STRING_C_STR(data.reason);
        }

        if (!STRING_IS_EMPTY(data.originatingDeviceId)) {
            doc[ORIGINATING_DEVICE_ID] = STRING_C_STR(data.originatingDeviceId);
        }

        if (!STRING_IS_EMPTY(data.originatingRequestId)) {
            doc[ORIGINATING_REQUEST_ID] = STRING_C_STR(data.originatingRequestId);
        }

        // Serialize sessions
        JsonArray sessionsArray = doc[SESSIONS].to<JsonArray>();
        for (const auto &session : data.sessions) {
            JsonObject sessionObj = sessionsArray.add<JsonObject>();
            sessionObj[PROCESS_ID] = session.processId;
            sessionObj[PROCESS_NAME] = STRING_C_STR(session.processName);
            sessionObj[DISPLAY_NAME] = STRING_C_STR(session.displayName);
            sessionObj[VOLUME] = session.volume;
            sessionObj[IS_MUTED] = session.isMuted;
            sessionObj[STATE] = STRING_C_STR(session.state);
        }

        // Serialize default device
        if (data.hasDefaultDevice) {
            JsonObject defaultObj = doc[DEFAULT_DEVICE].to<JsonObject>();
            defaultObj[FRIENDLY_NAME] = STRING_C_STR(data.defaultDevice.friendlyName);
            defaultObj[VOLUME] = data.defaultDevice.volume;
            defaultObj[IS_MUTED] = data.defaultDevice.isMuted;
            defaultObj[DATA_FLOW] = STRING_C_STR(data.defaultDevice.dataFlow);
            defaultObj[DEVICE_ROLE] = STRING_C_STR(data.defaultDevice.deviceRole);
        }

        string jsonString;
        serializeJson(doc, jsonString);

        return ParseResult<string>::createSuccess(jsonString);

    } catch (const std::exception &e) {
        string errorMsg = STRING_FROM_LITERAL("Error creating status response: ") + STRING_FROM_CSTR(e.what());
        ESP_LOGE(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<string>::createError(errorMsg);
    }
}

ParseResult<string>
MessageSerializer::createAssetRequest(const string &processName,
                                      const string &deviceId) {
    const char *TAG = "MessageSerializer";

    try {
        JsonDocument doc;

        string devId = STRING_IS_EMPTY(deviceId) ? Config::getDeviceId() : deviceId;

        doc[MessageProtocol::JsonFields::MESSAGE_TYPE] =
            static_cast<int>(MessageProtocol::ExternalMessageType::GET_ASSETS);
        doc[MessageProtocol::JsonFields::REQUEST_ID] = Config::generateRequestId();
        doc[MessageProtocol::JsonFields::DEVICE_ID] = STRING_C_STR(devId);
        doc[MessageProtocol::JsonFields::TIMESTAMP] = millis();
        doc["processName"] = STRING_C_STR(processName);

        string jsonString;
        serializeJson(doc, jsonString);

        return ParseResult<string>::createSuccess(jsonString);

    } catch (const std::exception &e) {
        string errorMsg = STRING_FROM_LITERAL("Error creating asset request: ") + STRING_FROM_CSTR(e.what());
        ESP_LOGE(TAG, "%s", STRING_C_STR(errorMsg));
        return ParseResult<string>::createError(errorMsg);
    }
}

// =============================================================================
// MESSAGE CONVERTER IMPLEMENTATIONS
// =============================================================================

std::vector<InternalMessage>
MessageConverter::externalToInternal(const ExternalMessage &external) {
    std::vector<InternalMessage> internalMessages;

    switch (external.messageType) {
        case MessageProtocol::ExternalMessageType::STATUS_UPDATE:
        case MessageProtocol::ExternalMessageType::STATUS_MESSAGE: {
            auto parseResult = MessageParser::parseAudioStatusData(external);
            if (parseResult.isValid()) {
                InternalMessage msg(
                    MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE,
                    &parseResult.getValue(), sizeof(AudioStatusData));
                internalMessages.push_back(msg);
            }
            break;
        }

        case MessageProtocol::ExternalMessageType::ASSET_RESPONSE: {
            auto parseResult = MessageParser::parseAssetResponseData(external);
            if (parseResult.isValid()) {
                InternalMessage msg(MessageProtocol::InternalMessageType::ASSET_RESPONSE,
                                    &parseResult.getValue(), sizeof(AssetResponseData));
                internalMessages.push_back(msg);
            }
            break;
        }

        default:
            // For other message types, create generic internal message
            break;
    }

    return internalMessages;
}

ExternalMessage
MessageConverter::internalToExternal(const InternalMessage &internal) {
    ExternalMessage external;

    switch (internal.messageType) {
        case MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE: {
            external.messageType = MessageProtocol::ExternalMessageType::STATUS_MESSAGE;
            external.requestId = Config::generateRequestId();
            external.deviceId = Config::getDeviceId();
            external.timestamp = internal.timestamp;
            break;
        }

        default:
            external.messageType = MessageProtocol::ExternalMessageType::INVALID;
            break;
    }

    return external;
}

}  // namespace Messaging
