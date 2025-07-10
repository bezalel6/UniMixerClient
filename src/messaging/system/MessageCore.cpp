#include "MessageCore.h"
#include "../protocol/MessageConfig.h"
#include "../MessageAPI.h"
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

    ESP_LOGW(TAG, "Initializing MessageCore with dual architecture...");

    // Clear any existing state
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

    ESP_LOGW(TAG, "MessageCore initialized successfully");
    return true;
}

void MessageCore::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGW(TAG, "Shutting down MessageCore...");

    // Shutdown all transports
    for (auto& [name, transport] : transports) {
        if (transport.deinit) {
            transport.deinit();
        }
    }

    // Clear all state
    internalSubscriptions.clear();
    internalWildcardSubscribers.clear();
    transports.clear();

    initialized = false;

    ESP_LOGW(TAG, "MessageCore shutdown complete");
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

    ESP_LOGW(TAG, "Registering transport: %s", name.c_str());

    // Initialize transport if needed
    if (transport.init && !transport.init()) {
        ESP_LOGE(TAG, "Failed to initialize transport: %s", name.c_str());
        return;
    }

    transports[name] = transport;

    ESP_LOGW(TAG, "Transport registered: %s", name.c_str());
}

void MessageCore::unregisterTransport(const String& name) {
    auto it = transports.find(name);
    if (it != transports.end()) {
        ESP_LOGW(TAG, "Unregistering transport: %s", name.c_str());

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

    logExternalMessage("IN", external);

    // Convert external message to internal message(s) for routing
    std::vector<InternalMessage> internalMessages = MessageConverter::externalToInternal(external);

    for (const auto& internal : internalMessages) {
        routeInternalMessage(internal);
    }

    ESP_LOGW(TAG, "Processed external message %d -> %d internal messages",
             LOG_EXTERNAL_MSG_TYPE(external.messageType), internalMessages.size());
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

    // Prepare JSON payload
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();

    // Core fields
    obj["messageType"] = SERIALIZE_EXTERNAL_MSG_TYPE(message.messageType);
    obj["requestId"] = message.requestId;
    obj["deviceId"] = message.deviceId;
    obj["timestamp"] = message.timestamp;

    if (!message.originatingDeviceId.isEmpty()) {
        obj["originatingDeviceId"] = message.originatingDeviceId;
    }

    // Additional fields (excluding core ones)
    static const char* excluded[] = {
        "messageType", "requestId", "deviceId", "timestamp", "originatingDeviceId"};

    if (message.parsedData.is<JsonObjectConst>()) {
        JsonObjectConst parsed = message.parsedData.as<JsonObjectConst>();
        for (JsonPairConst kv : parsed) {
            bool skip = false;
            for (const char* ex : excluded) {
                if (strcmp(kv.key().c_str(), ex) == 0) {
                    skip = true;
                    break;
                }
            }
            if (!skip) {
                obj[kv.key()] = kv.value();
            }
        }
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);
    Messaging::MessageAPI::publishDebugUILog(jsonPayload);
    // Send to all transports
    for (auto& [name, transport] : transports) {
        if (transport.sendRaw) {
            if (!transport.sendRaw(jsonPayload)) {
                ESP_LOGW(TAG, "Failed to send via transport: %s", name.c_str());
                success = false;
            }
        }
    }



    return success;
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

    ESP_LOGW(TAG, "Subscribing to internal messageType: %d", LOG_INTERNAL_MSG_TYPE(messageType));
    internalSubscriptions[messageType].push_back(callback);
}

void MessageCore::unsubscribeFromInternal(MessageProtocol::InternalMessageType messageType) {
    auto it = internalSubscriptions.find(messageType);
    if (it != internalSubscriptions.end()) {
        ESP_LOGW(TAG, "Unsubscribing from internal messageType: %d",
                 LOG_INTERNAL_MSG_TYPE(messageType));
        internalSubscriptions.erase(it);
    }
}

void MessageCore::subscribeToAllInternal(InternalMessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to all internal - not initialized");
        return;
    }

    ESP_LOGW(TAG, "Subscribing to all internal message types (wildcard)");
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
    InternalMessage msg = MessageFactory::createUIUpdateMessage(component, data);
    return publishInternal(msg);
}

bool MessageCore::publishAudioVolumeUpdate(const String& processName, int volume) {
    InternalMessage msg = MessageFactory::createAudioVolumeMessage(processName, volume);
    return publishInternal(msg);
}

// =============================================================================
// STATUS & DIAGNOSTICS
// =============================================================================

size_t MessageCore::getSubscriptionCount() const {
    size_t count = 0;

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

    // EXTERNAL MESSAGE STATS (Transport layer only - converted to internal)
    info += "- External received: " + String(externalMessagesReceived) + "\n";
    info += "- External published: " + String(externalMessagesPublished) + "\n";
    info += "- Invalid messages: " + String(invalidMessagesReceived) + "\n";

    // INTERNAL MESSAGE STATS (All subscriptions are internal)
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

void MessageCore::routeInternalMessage(const InternalMessage& internal) {
    // Route to appropriate subscribers
    auto it = internalSubscriptions.find(internal.messageType);
    if (it != internalSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(internal);
            } catch (...) {
                ESP_LOGE(TAG, "Internal callback exception for messageType: %d",
                         LOG_INTERNAL_MSG_TYPE(internal.messageType));
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


        ESP_LOGV(TAG, "Routed internal message: %d (Core %d)",
             LOG_INTERNAL_MSG_TYPE(internal.messageType),
             internal.shouldRouteToCore1() ? 1 : 0);
}

void MessageCore::logExternalMessage(const char* direction, const ExternalMessage& message) {
    ESP_LOGW(TAG, "[%s-EXT] %d (device: %s)",
             direction,
             LOG_EXTERNAL_MSG_TYPE(message.messageType),
             message.deviceId.c_str());
}

void MessageCore::logInternalMessage(const char* direction, const InternalMessage& message) {
    ESP_LOGW(TAG, "[%s-INT] %d (Core %d, Priority %d, Data %d bytes)",
             direction,
             LOG_INTERNAL_MSG_TYPE(message.messageType),
             message.shouldRouteToCore1() ? 1 : 0,
             message.priority,
             message.dataSize);
}

}  // namespace Messaging
