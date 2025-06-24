#include "MessageCore.h"
#include "MessageConfig.h"
#include "../application/LogoManager.h"
#include <esp_log.h>

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

    ESP_LOGI(TAG, "Initializing...");

    // Clear any existing state
    topicSubscriptions.clear();
    audioStatusSubscribers.clear();
    transports.clear();

    // Reset statistics
    messagesPublished = 0;
    messagesReceived = 0;
    lastActivityTime = millis();

    initialized = true;

    ESP_LOGI(TAG, "Initialized successfully");
    return true;
}

void MessageCore::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Shutting down...");

    // Shutdown all transports
    for (auto& [name, transport] : transports) {
        if (transport.deinit) {
            transport.deinit();
        }
    }

    // Clear all state
    topicSubscriptions.clear();
    audioStatusSubscribers.clear();
    transports.clear();
    lastLogoCheckTime.clear();

    initialized = false;

    ESP_LOGI(TAG, "Shutdown complete");
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
// MESSAGE HANDLING
// =============================================================================

void MessageCore::subscribe(const String& topic, MessageCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to topic: %s", topic.c_str());
    topicSubscriptions[topic].push_back(callback);
}

void MessageCore::subscribeToAudioStatus(AudioStatusCallback callback) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot subscribe to audio status - not initialized");
        return;
    }

    ESP_LOGI(TAG, "Subscribing to audio status updates");
    audioStatusSubscribers.push_back(callback);
}

void MessageCore::unsubscribe(const String& topic) {
    auto it = topicSubscriptions.find(topic);
    if (it != topicSubscriptions.end()) {
        ESP_LOGI(TAG, "Unsubscribing from topic: %s", topic.c_str());
        topicSubscriptions.erase(it);
    }
}

bool MessageCore::publish(const String& topic, const String& payload) {
    if (!initialized) {
        ESP_LOGW(TAG, "Cannot publish - not initialized");
        return false;
    }

    updateActivity();
    messagesPublished++;

    logMessage("OUT", topic, payload);

    bool success = true;

    // Send to all transports
    for (auto& [name, transport] : transports) {
        if (transport.send) {
            if (!transport.send(topic, payload)) {
                ESP_LOGW(TAG, "Failed to send via transport: %s", name.c_str());
                success = false;
            }
        }
    }

    // Notify local subscribers
    auto it = topicSubscriptions.find(topic);
    if (it != topicSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(topic, payload);
            } catch (...) {
                ESP_LOGE(TAG, "Callback exception for topic: %s", topic.c_str());
            }
        }
    }

    return success;
}

bool MessageCore::publish(const Message& message) {
    return publish(message.topic, message.payload);
}

bool MessageCore::requestAudioStatus() {
    if (!initialized) {
        return false;
    }

    String request = Json::createStatusRequest();
    return publish(Config::TOPIC_AUDIO_STATUS_REQUEST, request);
}

void MessageCore::handleIncomingMessage(const String& topic, const String& payload) {
    if (!initialized) {
        return;
    }

    updateActivity();
    messagesReceived++;

    logMessage("IN", topic, payload);

    // // Check if we should ignore self-originated messages
    // if (Json::shouldIgnoreMessage(payload)) {
    //     ESP_LOGD(TAG, "Ignoring self-originated message");
    //     return;
    // }

    // Handle audio status messages specially
    if (topic == Config::TOPIC_AUDIO_STATUS_RESPONSE || topic.indexOf(Config::STATUS_KEYWORD) >= 0) {
        processAudioStatusMessage(payload);
    }

    // Notify topic subscribers
    auto it = topicSubscriptions.find(topic);
    if (it != topicSubscriptions.end()) {
        for (auto& callback : it->second) {
            try {
                callback(topic, payload);
            } catch (...) {
                ESP_LOGE(TAG, "Callback exception for topic: %s", topic.c_str());
            }
        }
    }

    // Notify wildcard subscribers
    auto wildcardIt = topicSubscriptions.find(Config::TOPIC_WILDCARD);
    if (wildcardIt != topicSubscriptions.end()) {
        for (auto& callback : wildcardIt->second) {
            try {
                callback(topic, payload);
            } catch (...) {
                ESP_LOGE(TAG, "Wildcard callback exception");
            }
        }
    }
}

// =============================================================================
// STATUS & DIAGNOSTICS
// =============================================================================

size_t MessageCore::getSubscriptionCount() const {
    size_t count = audioStatusSubscribers.size();
    for (const auto& [topic, callbacks] : topicSubscriptions) {
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
    info += "- Subscriptions: " + String(getSubscriptionCount()) + "\n";
    info += "- Audio subscribers: " + String(audioStatusSubscribers.size()) + "\n";
    info += "- Messages published: " + String(messagesPublished) + "\n";
    info += "- Messages received: " + String(messagesReceived) + "\n";
    info += "- Last activity: " + String((millis() - lastActivityTime) / 1000) + "s ago\n";
    info += getTransportStatus();

    return info;
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

void MessageCore::processAudioStatusMessage(const String& payload) {
    if (audioStatusSubscribers.empty()) {
        return;
    }

    AudioStatusData statusData = Json::parseStatusResponse(payload);

    if (!statusData.isEmpty()) {
        // Check for logos for all detected audio processes
        checkAndRequestLogosForAudioProcesses(statusData);

        for (auto& callback : audioStatusSubscribers) {
            try {
                callback(statusData);
            } catch (...) {
                ESP_LOGE(TAG, "Audio status callback exception");
            }
        }
    }
}

void MessageCore::updateActivity() {
    lastActivityTime = millis();
}

void MessageCore::logMessage(const String& direction, const String& topic, const String& payload) {
    ESP_LOGD(TAG, "[%s] %s: %s", direction.c_str(), topic.c_str(),
             (payload.length() > Config::MESSAGE_LOG_TRUNCATE_LENGTH ? payload.substring(0, Config::MESSAGE_LOG_TRUNCATE_LENGTH) + "..." : payload).c_str());
}

void MessageCore::checkAndRequestLogosForAudioProcesses(const AudioStatusData& statusData) {
    // Skip if LogoManager is not initialized
    if (!Application::LogoAssets::LogoManager::getInstance().isInitialized() ||
        !Application::LogoAssets::LogoManager::getInstance().isAutoRequestEnabled()) {
        return;
    }

    // Check logos for all audio level processes
    for (const auto& audioLevel : statusData.audioLevels) {
        if (!audioLevel.processName.isEmpty()) {
            checkSingleProcessLogo(audioLevel.processName.c_str());
        }
    }

    // Check logo for default device if it has a process-like name
    if (statusData.hasDefaultDevice && !statusData.defaultDevice.friendlyName.isEmpty()) {
        // Only check if the friendly name looks like a process name (contains .exe, etc.)
        String friendlyName = statusData.defaultDevice.friendlyName;
        if (friendlyName.indexOf(".exe") >= 0 || friendlyName.indexOf(".app") >= 0 ||
            friendlyName.indexOf("-bin") >= 0) {
            checkSingleProcessLogo(friendlyName.c_str());
        }
    }
}

void MessageCore::checkSingleProcessLogo(const char* processName) {
    if (!processName || strlen(processName) == 0) {
        return;
    }

    // Debounce: Don't check the same process too frequently
    String processKey(processName);
    unsigned long currentTime = millis();

    auto it = lastLogoCheckTime.find(processKey);
    if (it != lastLogoCheckTime.end()) {
        if (currentTime - it->second < LOGO_CHECK_DEBOUNCE_MS) {
            // Recently checked this process, skip
            return;
        }
    }

    // Update last check time
    lastLogoCheckTime[processKey] = currentTime;

    Application::LogoAssets::LogoManager& logoManager =
        Application::LogoAssets::LogoManager::getInstance();

    // Use async loading to avoid blocking the messaging task
    // This will check existence and request if needed without blocking
    logoManager.loadLogoAsync(processName, [processName](const Application::LogoAssets::LogoLoadResult& result) {
        if (result.success) {
            ESP_LOGD("MessageCore", "Logo loaded successfully for: %s", processName);
        } else {
            ESP_LOGD("MessageCore", "Logo request initiated for: %s", processName);
        }
    });
}

}  // namespace Messaging
