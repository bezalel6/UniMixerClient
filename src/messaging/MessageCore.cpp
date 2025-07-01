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

    ESP_LOGI(TAG, "Initializing MessageCore with dual architecture...");

    // Clear any existing state
    externalSubscriptions.clear();
    internalSubscriptions.clear();
    internalWildcardSubscribers.clear();
    transports.clear();

    // Initialize MessageType registry for string<->enum conversion
    MessageProtocol::ExternalMessageTypeRegistry::getInstance().init();
    MessageProtocol::InternalMessageTypeRegistry::getInstance().init();

    // Reset statistics
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

    // Clear all state
    externalSubscriptions.clear();
    internalSubscriptions.clear();
    internalWildcardSubscribers.clear();
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
                 MessageProtocol::externalMessageTypeToString(external.messageType));
        return;
    }

    // Process external message (validation + conversion + routing)
    convertAndRouteExternal(external);
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
    doc["messageType"] = MessageProtocol::externalMessageTypeToString(message.messageType);
    doc["requestId"] = message.requestId;
    doc["deviceId"] = message.deviceId;
    doc["timestamp"] = message.timestamp;

    if (!message.originatingDeviceId.isEmpty()) {
        doc["originatingDeviceId"] = message.originatingDeviceId;
    }

    // Copy any additional parsed data (ArduinoJson V7 compatible iteration)
    if (message.parsedData.is<JsonObject>()) {
        for (JsonPair kv : message.parsedData.as<JsonObjectConst>()) {
            if (strcmp(kv.key().c_str(), "messageType") != 0 &&
                strcmp(kv.key().c_str(), "requestId") != 0 &&
                strcmp(kv.key().c_str(), "deviceId") != 0 &&
                strcmp(kv.key().c_str(), "timestamp") != 0 &&
                strcmp(kv.key().c_str(), "originatingDeviceId") != 0) {
                doc[kv.key()] = kv.value();
            }
        }
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Send to all transports
    for (auto& [name, transport] : transports) {
        if (transport.sendRaw) {
            if (!transport.sendRaw(jsonPayload)) {
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
                         MessageProtocol::externalMessageTypeToString(message.messageType));
            }
        }
    }

    return success;
}

void MessageCore::subscribeToExternal(MessageProtocol::ExternalMessageType messageType, ExternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to external - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to external messageType: %s", MessageProtocol::externalMessageTypeToString(messageType));
    externalSubscriptions[messageType].push_back(callback);
}

void MessageCore::unsubscribeFromExternal(MessageProtocol::ExternalMessageType messageType) {
    auto it = externalSubscriptions.find(messageType);
    if (it != externalSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from external messageType: %s",
                 MessageProtocol::externalMessageTypeToString(messageType));
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

void MessageCore::subscribeToInternal(MessageProtocol::InternalMessageType messageType, InternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to internal - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to internal messageType: %s", MessageProtocol::internalMessageTypeToString(messageType));
    internalSubscriptions[messageType].push_back(callback);
}

void MessageCore::unsubscribeFromInternal(MessageProtocol::InternalMessageType messageType) {
    auto it = internalSubscriptions.find(messageType);
    if (it != internalSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from internal messageType: %s",
                 MessageProtocol::internalMessageTypeToString(messageType));
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
    String info = "MessageCore Status (Dual Architecture):\n";
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

void MessageCore::convertAndRouteExternal(const ExternalMessage& external) {
    // Convert external message to internal message(s) for routing
    std::vector<InternalMessage> internalMessages = MessageConverter::externalToInternal(external);

    for (const auto& internal : internalMessages) {
        routeInternalMessage(internal);
    }

    ESP_LOGD(TAG, "Processed external message %s -> %d internal messages",
             MessageProtocol::externalMessageTypeToString(external.messageType), internalMessages.size());
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
                         MessageProtocol::internalMessageTypeToString(internal.messageType));
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
             MessageProtocol::internalMessageTypeToString(internal.messageType),
             internal.shouldRouteToCore1() ? 1 : 0);
}

void MessageCore::logExternalMessage(const char* direction, const ExternalMessage& message) {
    ESP_LOGD(TAG, "[%s-EXT] %s (device: %s)",
             direction,
             MessageProtocol::externalMessageTypeToString(message.messageType),
             message.deviceId.c_str());
}

void MessageCore::logInternalMessage(const char* direction, const InternalMessage& message) {
    ESP_LOGD(TAG, "[%s-INT] %s (Core %d, Priority %d, Data %d bytes)",
             direction,
             MessageProtocol::internalMessageTypeToString(message.messageType),
             message.shouldRouteToCore1() ? 1 : 0,
             message.priority,
             message.dataSize);
}

}  // namespace Messaging
