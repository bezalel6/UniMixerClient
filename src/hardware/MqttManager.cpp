#include "MqttManager.h"
#include "NetworkManager.h"
#include "DeviceManager.h"
#include "../application/AudioManager.h"
#include "../messaging/MessageAPI.h"
#include "../messaging/MessageConfig.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <secret.h>

static const char* TAG = "MQTTManager";

namespace Hardware {
namespace Mqtt {

// Private variables
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static ConnectionStatus currentMqttStatus = MQTT_STATUS_DISCONNECTED;
static unsigned long lastConnectionAttempt = 0;
static unsigned long connectionStartTime = 0;
static unsigned long lastActivityTime = 0;
static bool initializationComplete = false;
static bool networkRequested = false;

// Message queue for delayed publishing
static Message publishQueue[1];  // Single message queue as per original design
static bool queueHasMessage = false;

// Handler management
static Handler* registeredHandlers[MQTT_MAX_HANDLERS];
static int handlerCount = 0;

// Private function declarations
static void mqttCallback(char* topic, byte* payload, unsigned int length);
static void mqttEventHandler(void);
static void updateConnectionStatus(void);
static void processPublishQueue(void);
static void subscribeToRegisteredHandlers(void);
static Handler* findHandlerByTopic(const char* topic);
static void ensureNetworkAvailable(void);

bool init(void) {
    ESP_LOGI(TAG, "Initializing MQTT manager");

    // Initialize variables
    currentMqttStatus = MQTT_STATUS_DISCONNECTED;
    lastConnectionAttempt = 0;
    connectionStartTime = 0;
    lastActivityTime = Hardware::Device::getMillis();
    queueHasMessage = false;
    handlerCount = 0;
    networkRequested = false;

    // Clear handlers array
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        registeredHandlers[i] = nullptr;
    }

    // Configure MQTT client
    mqttClient.setServer(mqtt_server, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE);

    initializationComplete = true;
    ESP_LOGI(TAG, "MQTT manager initialized successfully (network not connected)");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing MQTT manager");

    disconnect();

    // Clear all handlers
    handlerCount = 0;
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        registeredHandlers[i] = nullptr;
    }

    // Clear publish queue
    clearPublishQueue();

    // Disable auto-reconnect if we requested it
    if (networkRequested) {
        Hardware::Network::enableAutoReconnect(false);
        networkRequested = false;
    }

    initializationComplete = false;
}

void update(void) {
    if (!initializationComplete) {
        return;
    }

    // Update connection status
    updateConnectionStatus();

    // Handle MQTT client loop
    if (mqttClient.connected()) {
        mqttClient.loop();
        lastActivityTime = Hardware::Device::getMillis();
    }

    // Process delayed publish queue
    processPublishQueue();

    // Handle reconnection logic
    unsigned long now = Hardware::Device::getMillis();

    switch (currentMqttStatus) {
        case MQTT_STATUS_CONNECTING:
            // Check for connection timeout
            if (connectionStartTime > 0 &&
                (now - connectionStartTime) > MQTT_CONNECT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "MQTT connection timeout");
                currentMqttStatus = MQTT_STATUS_FAILED;
                mqttClient.disconnect();
            }
            break;

        case MQTT_STATUS_FAILED:
        case MQTT_STATUS_DISCONNECTED:
            // Attempt reconnection if enough time has passed and network is needed
            if (networkRequested) {
                // Check if network just became available
                if (Hardware::Network::isConnected() &&
                    (now - lastConnectionAttempt) >= MQTT_RECONNECT_INTERVAL_MS) {
                    ESP_LOGI(TAG, "Network available, attempting MQTT reconnection");
                    reconnect();
                }
            }
            break;

        case MQTT_STATUS_CONNECTED:
            // Check if we're still connected
            if (!mqttClient.connected()) {
                ESP_LOGW(TAG, "MQTT connection lost");
                currentMqttStatus = MQTT_STATUS_DISCONNECTED;
            }
            break;

        case MQTT_STATUS_ERROR:
        default:
            break;
    }
}

bool connect(void) {
    ESP_LOGI(TAG, "MQTT connect requested");

    // Ensure network is available
    ensureNetworkAvailable();

    if (!Hardware::Network::isConnected()) {
        ESP_LOGW(TAG, "Cannot connect to MQTT: WiFi not connected, waiting...");
        currentMqttStatus = MQTT_STATUS_DISCONNECTED;
        return false;
    }

    ESP_LOGI(TAG, "Connecting to MQTT server: %s:%d", mqtt_server, MQTT_PORT);

    currentMqttStatus = MQTT_STATUS_CONNECTING;
    connectionStartTime = Hardware::Device::getMillis();
    lastConnectionAttempt = connectionStartTime;

    // Attempt connection with credentials
    bool connected = mqttClient.connect(MQTT_CLIENT_ID, mqtt_user, mqtt_password);

    if (connected) {
        ESP_LOGI(TAG, "MQTT connected successfully");
        currentMqttStatus = MQTT_STATUS_CONNECTED;
        connectionStartTime = 0;

        // Subscribe to registered handlers
        subscribeToRegisteredHandlers();

        // Register with new messaging system as MQTT transport
        Messaging::MessageAPI::registerMqttTransport(
            // Send function
            [](const String& topic, const String& payload) -> bool {
                return Hardware::Mqtt::publish(topic.c_str(), payload.c_str());
            },
            // IsConnected function
            []() -> bool {
                return Hardware::Mqtt::isConnected();
            },
            // Update function (optional)
            []() -> void {
                Hardware::Mqtt::update();
            },
            // Status function (optional)
            []() -> String {
                return String(Hardware::Mqtt::getStatusString());
            });

        // Set up message handling from MQTT to MessageAPI
        // We'll handle this through the existing callback mechanism

        // Publish system status
        // publishSystemStatus();

        // Request audio status after successful connection
        Application::Audio::AudioManager::getInstance().publishStatusRequest(true);

        return true;
    } else {
        ESP_LOGE(TAG, "MQTT connection failed, state: %d", mqttClient.state());
        currentMqttStatus = MQTT_STATUS_FAILED;
        connectionStartTime = 0;
        return false;
    }
}

void disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from MQTT");

    // Unregister from new messaging system
    Messaging::MessageAPI::unregisterTransport(Messaging::Config::TRANSPORT_NAME_MQTT);

    mqttClient.disconnect();
    currentMqttStatus = MQTT_STATUS_DISCONNECTED;
}

void reconnect(void) {
    disconnect();
    Hardware::Device::delay(100);
    connect();
}

ConnectionStatus getStatus(void) {
    return currentMqttStatus;
}

const char* getStatusString(void) {
    switch (currentMqttStatus) {
        case MQTT_STATUS_DISCONNECTED:
            return "Disconnected";
        case MQTT_STATUS_CONNECTING:
            return "Connecting...";
        case MQTT_STATUS_CONNECTED:
            return "Connected";
        case MQTT_STATUS_FAILED:
            return "Failed";
        case MQTT_STATUS_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

bool isConnected(void) {
    return currentMqttStatus == MQTT_STATUS_CONNECTED && mqttClient.connected();
}

unsigned long getLastActivity(void) {
    return lastActivityTime;
}

bool publish(const char* topic, const char* payload) {
    if (!isConnected()) {
        ESP_LOGW(TAG, "Cannot publish: MQTT not connected");
        return false;
    }

    if (topic == nullptr || payload == nullptr) {
        ESP_LOGE(TAG, "Cannot publish: topic or payload is null");
        return false;
    }

    ESP_LOGI(TAG, "Publishing to topic '%s': %s", topic, payload);
    bool result = mqttClient.publish(topic, payload);

    if (result) {
        lastActivityTime = Hardware::Device::getMillis();
    } else {
        ESP_LOGE(TAG, "Failed to publish message");
    }

    return result;
}

bool publishDelayed(const char* topic, const char* payload) {
    if (topic == nullptr || payload == nullptr) {
        ESP_LOGE(TAG, "Cannot publish delayed: topic or payload is null");
        return false;
    }

    // Update existing message or create new one (single message queue like original)
    strncpy(publishQueue[0].topic, topic, MQTT_MAX_TOPIC_LENGTH - 1);
    publishQueue[0].topic[MQTT_MAX_TOPIC_LENGTH - 1] = '\0';

    strncpy(publishQueue[0].payload, payload, MQTT_MAX_PAYLOAD_LENGTH - 1);
    publishQueue[0].payload[MQTT_MAX_PAYLOAD_LENGTH - 1] = '\0';

    publishQueue[0].timestamp = Hardware::Device::getMillis();
    publishQueue[0].valid = true;
    queueHasMessage = true;

    ESP_LOGI(TAG, "Queued delayed message for topic '%s'", topic);
    return true;
}

void publishSystemStatus(void) {
    if (!isConnected()) {
        return;
    }

    // Create system status JSON
    JsonDocument doc;
    doc["device"] = "ESP32SmartDisplay";
    doc["ip"] = Hardware::Network::getIpAddress();
    doc["rssi"] = Hardware::Network::getSignalStrength();
    doc["free_heap"] = Hardware::Device::getFreeHeap();
    doc["uptime"] = Hardware::Device::getMillis();
    doc["wifi_status"] = Hardware::Network::getWifiStatusString();
    doc["mqtt_status"] = getStatusString();

    String statusJson;
    serializeJson(doc, statusJson);

    publish("homeassistant/smartdisplay/status", statusJson.c_str());
}

bool subscribe(const char* topic) {
    if (!isConnected()) {
        ESP_LOGW(TAG, "Cannot subscribe: MQTT not connected");
        return false;
    }

    if (topic == nullptr) {
        ESP_LOGE(TAG, "Cannot subscribe: topic is null");
        return false;
    }

    bool result = mqttClient.subscribe(topic);
    if (result) {
        ESP_LOGI(TAG, "Subscribed to topic: %s", topic);
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
    }

    return result;
}

bool unsubscribe(const char* topic) {
    if (!isConnected()) {
        return false;
    }

    if (topic == nullptr) {
        return false;
    }

    bool result = mqttClient.unsubscribe(topic);
    if (result) {
        ESP_LOGI(TAG, "Unsubscribed from topic: %s", topic);
    }

    return result;
}

bool registerHandler(const Handler* handler) {
    if (handler == nullptr) {
        ESP_LOGE(TAG, "Cannot register null handler");
        return false;
    }

    if (handlerCount >= MQTT_MAX_HANDLERS) {
        ESP_LOGE(TAG, "Cannot register handler: maximum handlers reached");
        return false;
    }

    // Store handler pointer
    registeredHandlers[handlerCount] = const_cast<Handler*>(handler);
    handlerCount++;

    ESP_LOGI(TAG, "Registered MQTT handler: %s", handler->identifier);

    // Subscribe immediately if connected
    if (isConnected() && strlen(handler->subscribeTopic) > 0) {
        subscribe(handler->subscribeTopic);
    }

    return true;
}

bool unregisterHandler(const char* identifier) {
    if (identifier == nullptr) {
        return false;
    }

    for (int i = 0; i < handlerCount; i++) {
        if (registeredHandlers[i] != nullptr &&
            strcmp(registeredHandlers[i]->identifier, identifier) == 0) {
            // Unsubscribe if connected
            if (isConnected() && strlen(registeredHandlers[i]->subscribeTopic) > 0) {
                unsubscribe(registeredHandlers[i]->subscribeTopic);
            }

            // Remove handler by shifting array
            for (int j = i; j < handlerCount - 1; j++) {
                registeredHandlers[j] = registeredHandlers[j + 1];
            }
            registeredHandlers[handlerCount - 1] = nullptr;
            handlerCount--;

            ESP_LOGI(TAG, "Unregistered MQTT handler: %s", identifier);
            return true;
        }
    }

    ESP_LOGW(TAG, "Handler not found: %s", identifier);
    return false;
}

void clearPublishQueue(void) {
    queueHasMessage = false;
    publishQueue[0].valid = false;
    ESP_LOGI(TAG, "Publish queue cleared");
}

int getSignalQuality(void) {
    return Hardware::Network::getSignalStrength();
}

// Private function implementations
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    char payloadStr[MQTT_MAX_PAYLOAD_LENGTH];
    size_t copyLength = (length < MQTT_MAX_PAYLOAD_LENGTH - 1) ? length : MQTT_MAX_PAYLOAD_LENGTH - 1;
    memcpy(payloadStr, payload, copyLength);
    payloadStr[copyLength] = '\0';

    ESP_LOGI(TAG, "Received message - Topic: %s, Payload: %s", topic, payloadStr);

    // Forward to new messaging system
    Messaging::MessageAPI::handleIncomingMessage(String(topic), String(payloadStr));

    // Also handle through legacy system for backwards compatibility
    Handler* handler = findHandlerByTopic(topic);
    if (handler != nullptr && handler->callback != nullptr) {
        handler->callback(topic, payloadStr);
        ESP_LOGI(TAG, "Message handled by legacy handler: %s", handler->identifier);
    } else {
        ESP_LOGD(TAG, "No legacy handler for topic: %s (handled by new system)", topic);
    }

    lastActivityTime = Hardware::Device::getMillis();
}

static void updateConnectionStatus(void) {
    bool isConnectedNow = mqttClient.connected();

    if (isConnectedNow && currentMqttStatus != MQTT_STATUS_CONNECTED) {
        currentMqttStatus = MQTT_STATUS_CONNECTED;
        ESP_LOGI(TAG, "MQTT connection established");
    } else if (!isConnectedNow && currentMqttStatus == MQTT_STATUS_CONNECTED) {
        currentMqttStatus = MQTT_STATUS_DISCONNECTED;
        ESP_LOGW(TAG, "MQTT connection lost");
    }
}

static void processPublishQueue(void) {
    if (!queueHasMessage || !publishQueue[0].valid) {
        return;
    }

    unsigned long currentTime = Hardware::Device::getMillis();
    if (currentTime - publishQueue[0].timestamp >= MQTT_PUBLISH_DELAY_MS) {
        if (isConnected()) {
            ESP_LOGI(TAG, "Publishing delayed message - Topic: %s, Payload: %s",
                     publishQueue[0].topic, publishQueue[0].payload);
            publish(publishQueue[0].topic, publishQueue[0].payload);
        } else {
            ESP_LOGW(TAG, "Cannot publish delayed message: MQTT not connected");
        }

        // Clear the queue
        clearPublishQueue();
    }
}

static void subscribeToRegisteredHandlers(void) {
    ESP_LOGI(TAG, "Subscribing to %d registered handlers", handlerCount);

    for (int i = 0; i < handlerCount; i++) {
        if (registeredHandlers[i] != nullptr &&
            strlen(registeredHandlers[i]->subscribeTopic) > 0) {
            ESP_LOGI(TAG, "Handler %s subscribing to: %s",
                     registeredHandlers[i]->identifier,
                     registeredHandlers[i]->subscribeTopic);
            subscribe(registeredHandlers[i]->subscribeTopic);
        }
    }
}

static Handler* findHandlerByTopic(const char* topic) {
    if (topic == nullptr) {
        return nullptr;
    }

    for (int i = 0; i < handlerCount; i++) {
        if (registeredHandlers[i] != nullptr &&
            strcmp(registeredHandlers[i]->subscribeTopic, topic) == 0) {
            return registeredHandlers[i];
        }
    }

    return nullptr;
}

static void ensureNetworkAvailable(void) {
    if (!networkRequested) {
        ESP_LOGI(TAG, "MQTT requires network connectivity, requesting WiFi connection");
        Hardware::Network::connectWifi();
        Hardware::Network::enableAutoReconnect(true);
        networkRequested = true;
    }
}

}  // namespace Mqtt
}  // namespace Hardware