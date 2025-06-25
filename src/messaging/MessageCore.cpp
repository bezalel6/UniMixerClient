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

    // Clear any existing state
    typeSubscriptions.clear();
    wildcardSubscribers.clear();
    transports.clear();

    // Reset statistics
    messagesPublished = 0;
    messagesReceived = 0;
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
    typeSubscriptions.clear();
    wildcardSubscribers.clear();
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
// MESSAGE HANDLING (Type-Based)
// =============================================================================

void MessageCore::subscribeToType(const String& messageType, MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to messageType: %s", messageType.c_str());
    typeSubscriptions[messageType].push_back(callback);
}

void MessageCore::subscribeToAll(MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to all - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to all message types (wildcard)");
    wildcardSubscribers.push_back(callback);
}

void MessageCore::unsubscribeFromType(const String& messageType) {
    auto it = typeSubscriptions.find(messageType);
    if (it != typeSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from messageType: %s", messageType.c_str());
        typeSubscriptions.erase(it);
    }
}

bool MessageCore::publish(const Message& message) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish - not initialized");
        return false;
    }

    updateActivity();
    messagesPublished++;

    logMessage("OUT", message);

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

    // Notify type-specific subscribers
    auto it = typeSubscriptions.find(message.messageType);
    if (it != typeSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(message);
            } catch (...) {
                ESP_LOGE(TAG, "Callback exception for messageType: %s", message.messageType.c_str());
            }
        }
    }

    // Notify wildcard subscribers
    for (auto& callback : wildcardSubscribers) {
        try {
            callback(message);
        } catch (...) {
            ESP_LOGE(TAG, "Wildcard callback exception");
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

    if (message.messageType.isEmpty()) {
        ESP_LOGW(TAG, "Cannot publish - no messageType found in payload");
        return false;
    }

    return publish(message);
}

bool MessageCore::publishMessage(const String& messageType, const String& jsonPayload) {
    Message message(messageType, jsonPayload);
    return publish(message);
}

bool MessageCore::requestAudioStatus() {
    if (!initialized) {
        return false;
    }

    String request = Json::createStatusRequest();
    return publish(request);
}

void MessageCore::handleIncomingMessage(const String& jsonPayload) {
    if (!initialized) {
        return;
    }

    updateActivity();
    messagesReceived++;

    // Parse the incoming JSON to create a Message object
    Message message = MessageParser::parseMessage(jsonPayload);

    if (message.messageType.isEmpty()) {
        String prettyJson;
        JsonDocument doc;
        deserializeJson(doc, jsonPayload.c_str());
        serializeJsonPretty(doc, prettyJson);
        ESP_LOGD(TAG, "Raw JSON payload:\n%s", prettyJson.c_str());
        ESP_LOGW(TAG, "Ignoring message without messageType");
        return;
    }

    logMessage("IN", message);

    // Check if we should ignore self-originated messages
    if (MessageParser::shouldIgnoreMessage(message)) {
        ESP_LOGD(TAG, "Ignoring self-originated message with messageType: %s", message.messageType.c_str());
        return;
    }

    // Notify type-specific subscribers
    auto it = typeSubscriptions.find(message.messageType);
    if (it != typeSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(message);
            } catch (...) {
                ESP_LOGE(TAG, "Callback exception for messageType: %s", message.messageType.c_str());
            }
        }
    }

    // Notify wildcard subscribers
    for (auto& callback : wildcardSubscribers) {
        try {
            callback(message);
        } catch (...) {
            ESP_LOGE(TAG, "Wildcard callback exception");
        }
    }
}

// =============================================================================
// STATUS & DIAGNOSTICS
// =============================================================================

size_t MessageCore::getSubscriptionCount() const {
    size_t count = wildcardSubscribers.size();
    for (const auto& [messageType, callbacks] : typeSubscriptions) {
        count += callbacks.size();
    }
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
    String info = "MessageCore Status:\n";
    info += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    info += "- Total subscriptions: " + String(getSubscriptionCount()) + "\n";
    info += "- Type subscriptions: " + String(typeSubscriptions.size()) + "\n";
    info += "- Wildcard subscribers: " + String(wildcardSubscribers.size()) + "\n";
    info += "- Messages published: " + String(messagesPublished) + "\n";
    info += "- Messages received: " + String(messagesReceived) + "\n";
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

void MessageCore::logMessage(const String& direction, const Message& message) {
    ESP_LOGD(TAG, "[%s] %s: %s", direction.c_str(), message.messageType.c_str(),
             (message.payload.length() > Config::MESSAGE_LOG_TRUNCATE_LENGTH ? message.payload.substring(0, Config::MESSAGE_LOG_TRUNCATE_LENGTH) + "..." : message.payload).c_str());
}

}  // namespace Messaging
