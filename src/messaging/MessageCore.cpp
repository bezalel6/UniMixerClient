#include "MessageCore.h"
#include "MessageConfig.h"
#include <esp_log.h>
#include "ui/ui.h"
#include "DebugUtils.h"
static const char* TAG = "MessageCore";

namespace Messaging {

// =============================================================================
// SINGLETON IMPLEMENTATION
// =============================================================================

MessageCore& MessageCore::getInstance() {
    static MessageCore instance;
    return instance;
}

// =============================================================================
// CORE INTERFACE
// =============================================================================

bool MessageCore::init() {
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing MessageCore with type-based routing...");

    // Clear any existing state - DUAL ARCHITECTURE
    externalSubscriptions.clear();
    internalSubscriptions.clear();
    internalWildcardSubscribers.clear();

    // LEGACY: Clear legacy subscriptions
    legacyEnumSubscriptions.clear();
    legacyStringSubscriptions.clear();
    legacyWildcardSubscribers.clear();

    transports.clear();

    // Initialize MessageType registry for string<->enum conversion
    MessageProtocol::MessageTypeRegistry::getInstance().init();

    // Reset statistics - DUAL ARCHITECTURE
    externalMessagesReceived = 0;
    externalMessagesPublished = 0;
    internalMessagesPublished = 0;
    invalidMessagesReceived = 0;
    lastActivityTime = millis();

    initialized = true;

    ESP_LOGI(TAG, "MessageCore initialized successfully");
    return true;
}

void MessageCore::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Shutting down MessageCore...");

    // Shutdown all transports
    for (auto& [name, transport] : transports) {
        if (transport.deinit) {
            transport.deinit();
        }
    }

    // Clear all state - DUAL ARCHITECTURE
    externalSubscriptions.clear();
    internalSubscriptions.clear();
    internalWildcardSubscribers.clear();

    // LEGACY: Clear legacy subscriptions
    legacyEnumSubscriptions.clear();
    legacyStringSubscriptions.clear();
    legacyWildcardSubscribers.clear();

    transports.clear();

    initialized = false;

    ESP_LOGI(TAG, "MessageCore shutdown complete");
}

void MessageCore::update() {
    if (!initialized) {
        return;
    }

    // Update all transports
    for (auto& [name, transport] : transports) {
        if (transport.update) {
            transport.update();
        }
    }
}

// =============================================================================
// TRANSPORT MANAGEMENT
// =============================================================================

void MessageCore::registerTransport(const String& name, TransportInterface transport) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot register transport - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Registering transport: %s", name.c_str());

    // Initialize transport if needed
    if (transport.init && !transport.init()) {
        ESP_LOGE(TAG, "Failed to initialize transport: %s", name.c_str());
        return;
    }

    transports[name] = transport;

    ESP_LOGI(TAG, "Transport registered: %s", name.c_str());
}

void MessageCore::unregisterTransport(const String& name) {
    auto it = transports.find(name);
    if (it != transports.end()) {
        ESP_LOGI(TAG, "Unregistering transport: %s", name.c_str());

        // Cleanup transport
        if (it->second.deinit) {
            it->second.deinit();
        }

        transports.erase(it);
    }
}

String MessageCore::getTransportStatus() const {
    String status = "Transports: " + String(transports.size()) + "\n";

    for (const auto& [name, transport] : transports) {
        status += "- " + name + ": ";
        if (transport.isConnected) {
            status += transport.isConnected() ? "Connected" : "Disconnected";
        } else {
            status += "Unknown";
        }
        status += "\n";
    }

    return status;
}

// =============================================================================
// EXTERNAL MESSAGE HANDLING (From Transports)
// =============================================================================

void MessageCore::handleExternalMessage(const ExternalMessage& external) {
    if (!initialized) {
        return;
    }

    updateActivity();
    externalMessagesReceived++;

    // Validate the pre-parsed external message
    if (!const_cast<ExternalMessage&>(external).validate()) {
        invalidMessagesReceived++;
        ESP_LOGW(TAG, "Invalid external message received");
        return;
    }

    logExternalMessage("IN", external);

    // Check if we should ignore self-originated messages
    if (external.isSelfOriginated()) {
        ESP_LOGD(TAG, "Ignoring self-originated external message: %s",
                 MessageProtocol::messageTypeToString(external.messageType));
        return;
    }

    // Process external message (validation + conversion + routing)
    processExternalMessage(external);
}

bool MessageCore::publishExternal(const ExternalMessage& message) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish external - not initialized");
        return false;
    }

    updateActivity();
    externalMessagesPublished++;

    logExternalMessage("OUT", message);

    bool success = true;

    // Convert ExternalMessage to JSON for transport
    JsonDocument doc;
    doc["messageType"] = MessageProtocol::messageTypeToString(message.messageType);
    doc["requestId"] = message.requestId;
    doc["deviceId"] = message.deviceId;
    doc["timestamp"] = message.timestamp;

    if (!message.originatingDeviceId.isEmpty()) {
        doc["originatingDeviceId"] = message.originatingDeviceId;
    }

    // Copy any additional parsed data
    for (JsonPair kv : message.parsedData.as<JsonObject>()) {
        if (strcmp(kv.key().c_str(), "messageType") != 0 &&
            strcmp(kv.key().c_str(), "requestId") != 0 &&
            strcmp(kv.key().c_str(), "deviceId") != 0 &&
            strcmp(kv.key().c_str(), "timestamp") != 0 &&
            strcmp(kv.key().c_str(), "originatingDeviceId") != 0) {
            doc[kv.key()] = kv.value();
        }
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Send to all transports
    for (auto& [name, transport] : transports) {
        if (transport.send) {
            if (!transport.send(jsonPayload)) {
                ESP_LOGW(TAG, "Failed to send via transport: %s", name.c_str());
                success = false;
            }
        }
    }

    // Notify external message subscribers (for raw protocol handling)
    auto it = externalSubscriptions.find(message.messageType);
    if (it != externalSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(message);
            } catch (...) {
                ESP_LOGE(TAG, "External callback exception for messageType: %s",
                         MessageProtocol::messageTypeToString(message.messageType));
            }
        }
    }

    return success;
}

void MessageCore::subscribeToExternal(MessageProtocol::MessageType messageType, ExternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to external - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to external messageType: %s", MessageProtocol::messageTypeToString(messageType));
    externalSubscriptions[messageType].push_back(callback);
}

void MessageCore::unsubscribeFromExternal(MessageProtocol::MessageType messageType) {
    auto it = externalSubscriptions.find(messageType);
    if (it != externalSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from external messageType: %s",
                 MessageProtocol::messageTypeToString(messageType));
        externalSubscriptions.erase(it);
    }
}

// =============================================================================
// INTERNAL MESSAGE HANDLING (ESP32 Internal Communication)
// =============================================================================

bool MessageCore::publishInternal(const InternalMessage& message) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish internal - not initialized");
        return false;
    }

    updateActivity();
    internalMessagesPublished++;

    logInternalMessage("INTERNAL", message);

    // Route internal message to appropriate core/subscribers
    routeInternalMessage(message);

    return true;
}

void MessageCore::subscribeToInternal(MessageProtocol::MessageType messageType, InternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to internal - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to internal messageType: %s", MessageProtocol::messageTypeToString(messageType));
    internalSubscriptions[messageType].push_back(callback);
}

void MessageCore::unsubscribeFromInternal(MessageProtocol::MessageType messageType) {
    auto it = internalSubscriptions.find(messageType);
    if (it != internalSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from internal messageType: %s",
                 MessageProtocol::messageTypeToString(messageType));
        internalSubscriptions.erase(it);
    }
}

void MessageCore::subscribeToAllInternal(InternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to all internal - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to all internal message types (wildcard)");
    internalWildcardSubscribers.push_back(callback);
}

// =============================================================================
// CONVENIENCE METHODS (Common Operations)
// =============================================================================

bool MessageCore::requestAudioStatus() {
    if (!initialized) {
        return false;
    }

    // Create external message for audio status request
    ExternalMessage request(MessageProtocol::ExternalMessageType::GET_STATUS,
                            Config::generateRequestId(),
                            Config::getDeviceId());
    request.validated = true;

    return publishExternal(request);
}

bool MessageCore::sendAudioCommand(MessageProtocol::ExternalMessageType commandType, const String& target, int value) {
    if (!initialized) {
        return false;
    }

    // Create external message for audio command
    ExternalMessage command(commandType,
                            Config::generateRequestId(),
                            Config::getDeviceId());

    // Add command-specific data
    if (!target.isEmpty()) {
        command.parsedData["target"] = target;
    }

    if (value >= 0) {
        command.parsedData["value"] = value;
    }

    command.validated = true;

    return publishExternal(command);
}

bool MessageCore::publishUIUpdate(const String& component, const String& data) {
    InternalMessage msg = MessageConverter::createUIUpdateMessage(component, data);
    return publishInternal(msg);
}

bool MessageCore::publishAudioVolumeUpdate(const String& processName, int volume) {
    InternalMessage msg = MessageConverter::createAudioVolumeMessage(processName, volume);
    return publishInternal(msg);
}

// =============================================================================
// LEGACY COMPATIBILITY (Will be removed)
// =============================================================================

// LEGACY: ENUM-based subscription - PERFORMANCE OPTIMIZED
void MessageCore::subscribeToType(MessageProtocol::MessageType messageType, MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe - not initialized");
        return;
    }

    ESP_LOGI(TAG, "LEGACY: Subscribing to messageType: %s (enum)", MessageProtocol::messageTypeToString(messageType));
    legacyEnumSubscriptions[messageType].push_back(callback);
}

// LEGACY: String-based subscription with conversion
void MessageCore::subscribeToType(const String& messageType, MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe - not initialized");
        return;
    }

    // Convert string to enum and use optimized enum subscription
    MessageProtocol::MessageType enumType = MessageProtocol::stringToMessageType(messageType);
    if (enumType != MessageProtocol::MessageType::INVALID && enumType != MessageProtocol::MessageType::UNKNOWN) {
        subscribeToType(enumType, callback);
    } else {
        ESP_LOGW(TAG, "LEGACY: String subscription for unknown messageType: %s", messageType.c_str());
        legacyStringSubscriptions[messageType].push_back(callback);
    }
}

void MessageCore::subscribeToAll(MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to all - not initialized");
        return;
    }

    ESP_LOGI(TAG, "LEGACY: Subscribing to all message types (wildcard)");
    legacyWildcardSubscribers.push_back(callback);
}

// LEGACY: ENUM-based unsubscription - PERFORMANCE OPTIMIZED
void MessageCore::unsubscribeFromType(MessageProtocol::MessageType messageType) {
    auto it = legacyEnumSubscriptions.find(messageType);
    if (it != legacyEnumSubscriptions.end()) {
        ESP_LOGI(TAG, "LEGACY: Unsubscribing from messageType: %s (enum)", MessageProtocol::messageTypeToString(messageType));
        legacyEnumSubscriptions.erase(it);
    }
}

// LEGACY: String-based unsubscription with conversion
void MessageCore::unsubscribeFromType(const String& messageType) {
    // First try to unsubscribe from enum subscriptions
    MessageProtocol::MessageType enumType = MessageProtocol::stringToMessageType(messageType);
    if (enumType != MessageProtocol::MessageType::INVALID && enumType != MessageProtocol::MessageType::UNKNOWN) {
        unsubscribeFromType(enumType);
    }

    // Also check legacy string subscriptions
    auto it = legacyStringSubscriptions.find(messageType);
    if (it != legacyStringSubscriptions.end()) {
        ESP_LOGI(TAG, "LEGACY: Unsubscribing from messageType: %s", messageType.c_str());
        legacyStringSubscriptions.erase(it);
    }
}

// LEGACY: Publish old Message struct
bool MessageCore::publish(const Message& message) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish - not initialized");
        return false;
    }

    updateActivity();

    logMessage("LEGACY-OUT", message);

    bool success = true;

    // Send to all transports
    for (auto& [name, transport] : transports) {
        if (transport.send) {
            if (!transport.send(message.payload)) {
                ESP_LOGW(TAG, "Failed to send via transport: %s", name.c_str());
                success = false;
            }
        }
    }

    // LEGACY: ENUM-based routing
    auto enumIt = legacyEnumSubscriptions.find(message.messageType);
    if (enumIt != legacyEnumSubscriptions.end()) {
        for (auto& callback : enumIt->second) {
            try {
                callback(message);
            } catch (...) {
                ESP_LOGE(TAG, "LEGACY: Callback exception for messageType: %s", MessageProtocol::messageTypeToString(message.messageType));
            }
        }
    }

    // LEGACY: String-based routing for compatibility
    String messageTypeStr = MessageProtocol::messageTypeToString(message.messageType);
    auto legacyIt = legacyStringSubscriptions.find(messageTypeStr);
    if (legacyIt != legacyStringSubscriptions.end()) {
        for (auto& callback : legacyIt->second) {
            try {
                callback(message);
            } catch (...) {
                ESP_LOGE(TAG, "LEGACY: String callback exception for messageType: %s", messageTypeStr.c_str());
            }
        }
    }

    // Notify legacy wildcard subscribers
    for (auto& callback : legacyWildcardSubscribers) {
        try {
            callback(message);
        } catch (...) {
            ESP_LOGE(TAG, "LEGACY: Wildcard callback exception");
        }
    }

    return success;
}

bool MessageCore::publish(const String& jsonPayload) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish - not initialized");
        return false;
    }

    // Parse the payload to create a Message object
    Message message = MessageParser::parseMessage(jsonPayload);

    if (message.messageType == MessageProtocol::MessageType::INVALID) {
        ESP_LOGW(TAG, "Cannot publish - no messageType found in payload");
        return false;
    }

    return publish(message);
}

bool MessageCore::publishMessage(const String& messageType, const String& jsonPayload) {
    Message message(messageType, jsonPayload);
    return publish(message);
}

// LEGACY: Handle incoming raw JSON message (parses then redirects)
void MessageCore::handleIncomingMessage(const String& jsonPayload) {
    if (!initialized) {
        return;
    }

    ESP_LOGD(TAG, "LEGACY: Parsing JSON payload for external message");

    // Parse JSON to create ExternalMessage (this is what transports should do)
    JsonDocument doc;
    if (deserializeJson(doc, jsonPayload) != DeserializationError::Ok) {
        invalidMessagesReceived++;
        ESP_LOGW(TAG, "LEGACY: Failed to parse JSON payload");
        return;
    }

    // Extract core fields
    String typeStr = doc["messageType"] | "";
    MessageProtocol::MessageType messageType = MessageProtocol::stringToMessageType(typeStr);

    if (messageType == MessageProtocol::MessageType::INVALID ||
        messageType == MessageProtocol::MessageType::UNKNOWN) {
        invalidMessagesReceived++;
        ESP_LOGW(TAG, "LEGACY: Invalid message type in JSON");
        return;
    }

    // Create ExternalMessage with parsed data
    ExternalMessage external(messageType,
                             doc["requestId"] | "",
                             doc["deviceId"] | "");
    external.originatingDeviceId = doc["originatingDeviceId"] | "";
    external.timestamp = doc["timestamp"] | millis();
    external.parsedData = doc;  // Store the parsed JSON data

    // Route to new efficient handler
    handleExternalMessage(external);
}

// =============================================================================
// STATUS & DIAGNOSTICS
// =============================================================================

size_t MessageCore::getSubscriptionCount() const {
    size_t count = 0;

    // Count external subscriptions
    for (const auto& [messageType, callbacks] : externalSubscriptions) {
        count += callbacks.size();
    }

    // Count internal subscriptions
    for (const auto& [messageType, callbacks] : internalSubscriptions) {
        count += callbacks.size();
    }
    count += internalWildcardSubscribers.size();

    // Count legacy subscriptions
    for (const auto& [messageType, callbacks] : legacyEnumSubscriptions) {
        count += callbacks.size();
    }
    for (const auto& [messageType, callbacks] : legacyStringSubscriptions) {
        count += callbacks.size();
    }
    count += legacyWildcardSubscribers.size();

    return count;
}

size_t MessageCore::getTransportCount() const {
    return transports.size();
}

bool MessageCore::isHealthy() const {
    if (!initialized) {
        return false;
    }

    // Check if we have at least one working transport
    bool hasWorkingTransport = false;
    for (const auto& [name, transport] : transports) {
        if (transport.isConnected && transport.isConnected()) {
            hasWorkingTransport = true;
            break;
        }
    }

    // Check recent activity (within configured timeout)
    unsigned long timeSinceActivity = millis() - lastActivityTime;
    bool recentActivity = timeSinceActivity < Config::ACTIVITY_TIMEOUT_MS;

    return hasWorkingTransport || recentActivity;
}

String MessageCore::getStatusInfo() const {
    String info = "MessageCore Status (DUAL ARCHITECTURE):\n";
    info += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    info += "- Total subscriptions: " + String(getSubscriptionCount()) + "\n";

    // EXTERNAL MESSAGE STATS (Core 1 processing)
    info += "- External subscriptions: " + String(externalSubscriptions.size()) + " (Core 1)\n";
    info += "- External received: " + String(externalMessagesReceived) + "\n";
    info += "- External published: " + String(externalMessagesPublished) + "\n";
    info += "- Invalid messages: " + String(invalidMessagesReceived) + "\n";

    // INTERNAL MESSAGE STATS (Core routing)
    info += "- Internal subscriptions: " + String(internalSubscriptions.size()) + " (Smart routing)\n";
    info += "- Internal wildcards: " + String(internalWildcardSubscribers.size()) + "\n";
    info += "- Internal published: " + String(internalMessagesPublished) + "\n";

    // LEGACY STATS (Will be removed)
    info += "- Legacy enum subscriptions: " + String(legacyEnumSubscriptions.size()) + " (LEGACY)\n";
    info += "- Legacy string subscriptions: " + String(legacyStringSubscriptions.size()) + " (LEGACY)\n";
    info += "- Legacy wildcards: " + String(legacyWildcardSubscribers.size()) + " (LEGACY)\n";

    info += "- Last activity: " + String((millis() - lastActivityTime) / 1000) + "s ago\n";
    info += getTransportStatus();

    return info;
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

void MessageCore::updateActivity() {
    lastActivityTime = millis();
}

// =============================================================================
// INTERNAL HELPERS - DUAL ARCHITECTURE
// =============================================================================

void MessageCore::processExternalMessage(const ExternalMessage& external) {
    // Convert external message to internal message(s) for routing
    std::vector<InternalMessage> internalMessages = MessageConverter::externalToInternal(external);

    for (const auto& internal : internalMessages) {
        routeInternalMessage(internal);
    }

    ESP_LOGD(TAG, "Processed external message %s -> %d internal messages",
             MessageProtocol::messageTypeToString(external.messageType), internalMessages.size());
}

void MessageCore::routeInternalMessage(const InternalMessage& internal) {
    // Route to appropriate subscribers
    auto it = internalSubscriptions.find(internal.messageType);
    if (it != internalSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(internal);
            } catch (...) {
                ESP_LOGE(TAG, "Internal callback exception for messageType: %s",
                         MessageProtocol::messageTypeToString(internal.messageType));
            }
        }
    }

    // Notify wildcard subscribers
    for (auto& callback : internalWildcardSubscribers) {
        try {
            callback(internal);
        } catch (...) {
            ESP_LOGE(TAG, "Internal wildcard callback exception");
        }
    }

    ESP_LOGV(TAG, "Routed internal message: %s (Core %d)",
             MessageProtocol::messageTypeToString(internal.messageType),
             internal.shouldRouteToCore1() ? 1 : 0);
}

void MessageCore::logExternalMessage(const String& direction, const ExternalMessage& message) {
    ESP_LOGD(TAG, "[%s-EXT] %s (%d bytes): %s",
             direction.c_str(),
             MessageProtocol::messageTypeToString(message.messageType),
             message.rawPayload.length(),
             (message.rawPayload.length() > Config::MESSAGE_LOG_TRUNCATE_LENGTH ? message.rawPayload.substring(0, Config::MESSAGE_LOG_TRUNCATE_LENGTH) + "..." : message.rawPayload).c_str());
}

void MessageCore::logInternalMessage(const String& direction, const InternalMessage& message) {
    ESP_LOGD(TAG, "[%s-INT] %s (Core %d, Priority %d, Data %d bytes)",
             direction.c_str(),
             MessageProtocol::messageTypeToString(message.messageType),
             message.shouldRouteToCore1() ? 1 : 0,
             message.priority,
             message.dataSize);
}

// LEGACY: Log old message format
void MessageCore::logMessage(const String& direction, const Message& message) {
    ESP_LOGD(TAG, "[%s-LEGACY] %s: %s", direction.c_str(),
             MessageProtocol::messageTypeToString(message.messageType),
             (message.payload.length() > Config::MESSAGE_LOG_TRUNCATE_LENGTH ? message.payload.substring(0, Config::MESSAGE_LOG_TRUNCATE_LENGTH) + "..." : message.payload).c_str());
}

}  // namespace Messaging
