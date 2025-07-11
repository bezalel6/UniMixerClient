#include "MessageAPI.h"
#include <esp_log.h>

namespace Messaging {

static const char* TAG = "MessageAPI";

// Static member initialization
bool MessageAPI::initialized = false;
MessageCore* MessageAPI::messageCore = nullptr;

// === CORE MANAGEMENT ===

bool MessageAPI::initialize() {
    if (initialized) {
        return true;
    }

    messageCore = &MessageCore::getInstance();
    initialized = messageCore->init();

    if (initialized) {
        ESP_LOGI(TAG, "MessageAPI initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize MessageAPI");
    }

    return initialized;
}

void MessageAPI::shutdown() {
    if (initialized && messageCore) {
        messageCore->deinit();
        initialized = false;
    }
}

bool MessageAPI::isHealthy() {
    return initialized && messageCore && messageCore->isHealthy();
}

void MessageAPI::update() {
    if (ensureInitialized()) {
        messageCore->update();
    }
}

// === TRANSPORT MANAGEMENT ===

void MessageAPI::registerSerialTransport(
    std::function<bool(const String&)> sendFunction,
    std::function<bool()> isConnectedFunction,
    std::function<void()> updateFunction
) {
    if (!ensureInitialized()) {
        return;
    }

    // Create wrapper functions for function pointer compatibility
    static std::function<bool(const String&)> sendWrapper = sendFunction;
    static std::function<bool()> connectedWrapper = isConnectedFunction;
    static std::function<void()> updateWrapper = updateFunction;

    TransportInterface transport;
    transport.sendRaw = sendWrapper;
    transport.isConnected = connectedWrapper;
    transport.update = updateWrapper;

    messageCore->registerTransport(Config::TRANSPORT_NAME_SERIAL, transport);
}

// === EXTERNAL MESSAGE PUBLISHING ===

bool MessageAPI::publishExternal(const ExternalMessage& message) {
    if (!ensureInitialized()) {
        return false;
    }
    return messageCore->publishExternal(message);
}

bool MessageAPI::requestAudioStatus() {
    if (!ensureInitialized()) {
        return false;
    }
    return messageCore->requestAudioStatus();
}

// === INTERNAL MESSAGE SUBSCRIPTION ===

void MessageAPI::subscribeToInternal(
    MessageProtocol::InternalMessageType messageType,
    std::function<void(const InternalMessage&)> callback
) {
    if (!ensureInitialized()) {
        return;
    }

    // Create wrapper for function pointer compatibility
    static auto callbackWrapper = [callback](const InternalMessage& msg) {
        callback(msg);
    };

    // Note: This may need platform-specific handling for function pointer conversion
    // For now, we'll try direct assignment
    messageCore->subscribeToInternal(messageType, [callback](const InternalMessage& msg) {
        callback(msg);
    });
}

void MessageAPI::unsubscribeFromInternal(
    MessageProtocol::InternalMessageType messageType
) {
    if (!ensureInitialized()) {
        return;
    }

    messageCore->unsubscribeFromInternal(messageType);
}

void MessageAPI::subscribeToAllInternal(
    std::function<void(const InternalMessage&)> callback
) {
    if (!ensureInitialized()) {
        return;
    }

    messageCore->subscribeToAllInternal([callback](const InternalMessage& msg) {
        callback(msg);
    });
}

// === INTERNAL MESSAGE PUBLISHING ===

bool MessageAPI::publishInternal(const InternalMessage& message) {
    if (!ensureInitialized()) {
        return false;
    }
    return messageCore->publishInternal(message);
}

bool MessageAPI::publishWifiStatus(const String& status, bool connected) {
    InternalMessage msg = MessageFactory::createWifiStatusMessage(status, connected);
    return publishInternal(msg);
}

bool MessageAPI::publishNetworkInfo(const String& ssid, const String& ip) {
    InternalMessage msg = MessageFactory::createNetworkInfoMessage(ssid, ip);
    return publishInternal(msg);
}

bool MessageAPI::publishSDStatus(const String& status, bool mounted) {
    InternalMessage msg = MessageFactory::createSDStatusMessage(status, mounted);
    return publishInternal(msg);
}

bool MessageAPI::publishAudioDeviceChange(const String& deviceName) {
    InternalMessage msg = MessageFactory::createAudioDeviceChangeMessage(deviceName);
    return publishInternal(msg);
}

bool MessageAPI::publishUIUpdate(const String& component, const String& data) {
    InternalMessage msg = MessageFactory::createUIUpdateMessage(component, data);
    return publishInternal(msg);
}

bool MessageAPI::publishSystemStatus(const String& status) {
    InternalMessage msg = MessageFactory::createSystemStatusMessage(status);
    return publishInternal(msg);
}

bool MessageAPI::publishDebugUILog(const String& logMessage) {
    InternalMessage msg = MessageFactory::createDebugUILogMessage(logMessage);
    return publishInternal(msg);
}

// === MESSAGE PARSING (Updated for new ParseResult API) ===

ParseResult<ExternalMessage> MessageAPI::parseExternalMessage(const String& jsonPayload) {
    return MessageParser::parseExternalMessage(jsonPayload);
}

ParseResult<MessageProtocol::ExternalMessageType> MessageAPI::parseExternalMessageType(const String& jsonPayload) {
    return MessageParser::parseExternalMessageType(jsonPayload);
}

// === MESSAGE CREATION (Updated for new ParseResult API) ===

ParseResult<String> MessageAPI::createStatusResponse(const AudioStatusData& data) {
    return MessageSerializer::createStatusResponse(data);
}

ParseResult<String> MessageAPI::createAssetRequest(const String& processName, const String& deviceId) {
    return MessageSerializer::createAssetRequest(processName, deviceId);
}

// === AUDIO DATA PARSING (Updated for new ParseResult API) ===

ParseResult<AudioStatusData> MessageAPI::parseAudioStatus(const ExternalMessage& message) {
    return MessageParser::parseAudioStatusData(message);
}

// === STATISTICS AND STATUS ===

String MessageAPI::getStats() {
    if (!ensureInitialized()) {
        return "MessageAPI not initialized";
    }
    return messageCore->getStatusInfo();
}

String MessageAPI::getTransportStatus() {
    if (!ensureInitialized()) {
        return "MessageAPI not initialized";
    }
    return messageCore->getTransportStatus();
}

// === PRIVATE HELPER FUNCTIONS ===

MessageCore& MessageAPI::getMessageCore() {
    if (!messageCore) {
        messageCore = &MessageCore::getInstance();
    }
    return *messageCore;
}

bool MessageAPI::ensureInitialized() {
    if (!initialized) {
        ESP_LOGW(TAG, "MessageAPI not initialized, attempting auto-initialization");
        return initialize();
    }
    return true;
}

} // namespace Messaging
