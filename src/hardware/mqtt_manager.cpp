#include "mqtt_manager.h"
#include "network_manager.h"
#include "device_manager.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <secret.h>

static const char* TAG = "MQTTManager";

// Private variables
static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);
static mqtt_connection_status_t current_mqtt_status = MQTT_STATUS_DISCONNECTED;
static unsigned long last_connection_attempt = 0;
static unsigned long connection_start_time = 0;
static unsigned long last_activity_time = 0;
static bool initialization_complete = false;

// Message queue for delayed publishing
static mqtt_message_t publish_queue[1];  // Single message queue as per original design
static bool queue_has_message = false;

// Handler management
static mqtt_handler_t* registered_handlers[MQTT_MAX_HANDLERS];
static int handler_count = 0;

// Private function declarations
static void mqtt_callback(char* topic, byte* payload, unsigned int length);
static void mqtt_event_handler(void);
static void update_connection_status(void);
static void process_publish_queue(void);
static void subscribe_to_registered_handlers(void);
static mqtt_handler_t* find_handler_by_topic(const char* topic);

bool mqtt_manager_init(void) {
    ESP_LOGI(TAG, "Initializing MQTT manager");

    // Initialize variables
    current_mqtt_status = MQTT_STATUS_DISCONNECTED;
    last_connection_attempt = 0;
    connection_start_time = 0;
    last_activity_time = device_get_millis();
    queue_has_message = false;
    handler_count = 0;

    // Clear handlers array
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        registered_handlers[i] = nullptr;
    }

    // Configure MQTT client
    mqtt_client.setServer(mqtt_server, MQTT_PORT);
    mqtt_client.setCallback(mqtt_callback);
    mqtt_client.setKeepAlive(MQTT_KEEPALIVE);

    // Start connection if WiFi is available
    if (network_is_connected()) {
        mqtt_connect();
    }

    initialization_complete = true;
    ESP_LOGI(TAG, "MQTT manager initialized successfully");
    return true;
}

void mqtt_manager_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing MQTT manager");

    mqtt_disconnect();

    // Clear all handlers
    handler_count = 0;
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        registered_handlers[i] = nullptr;
    }

    // Clear publish queue
    mqtt_clear_publish_queue();

    initialization_complete = false;
}

void mqtt_manager_update(void) {
    if (!initialization_complete) {
        return;
    }

    // Update connection status
    update_connection_status();

    // Handle MQTT client loop
    if (mqtt_client.connected()) {
        mqtt_client.loop();
        last_activity_time = device_get_millis();
    }

    // Process delayed publish queue
    process_publish_queue();

    // Handle reconnection logic
    unsigned long now = device_get_millis();

    switch (current_mqtt_status) {
        case MQTT_STATUS_CONNECTING:
            // Check for connection timeout
            if (connection_start_time > 0 &&
                (now - connection_start_time) > MQTT_CONNECT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "MQTT connection timeout");
                current_mqtt_status = MQTT_STATUS_FAILED;
                mqtt_client.disconnect();
            }
            break;

        case MQTT_STATUS_FAILED:
        case MQTT_STATUS_DISCONNECTED:
            // Attempt reconnection if WiFi is connected and enough time has passed
            if (network_is_connected() &&
                (now - last_connection_attempt) >= MQTT_RECONNECT_INTERVAL_MS) {
                ESP_LOGI(TAG, "Attempting MQTT reconnection");
                mqtt_reconnect();
            }
            break;

        case MQTT_STATUS_CONNECTED:
            // Check if we're still connected
            if (!mqtt_client.connected()) {
                ESP_LOGW(TAG, "MQTT connection lost");
                current_mqtt_status = MQTT_STATUS_DISCONNECTED;
            }
            break;

        case MQTT_STATUS_ERROR:
        default:
            break;
    }
}

bool mqtt_connect(void) {
    if (!network_is_connected()) {
        ESP_LOGW(TAG, "Cannot connect to MQTT: WiFi not connected");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to MQTT server: %s:%d", mqtt_server, MQTT_PORT);

    current_mqtt_status = MQTT_STATUS_CONNECTING;
    connection_start_time = device_get_millis();
    last_connection_attempt = connection_start_time;

    // Attempt connection with credentials
    bool connected = mqtt_client.connect(MQTT_CLIENT_ID, mqtt_user, mqtt_password);

    if (connected) {
        ESP_LOGI(TAG, "MQTT connected successfully");
        current_mqtt_status = MQTT_STATUS_CONNECTED;
        connection_start_time = 0;

        // Subscribe to registered handlers
        subscribe_to_registered_handlers();

        // Publish system status
        // mqtt_publish_system_status();

        return true;
    } else {
        ESP_LOGE(TAG, "MQTT connection failed, state: %d", mqtt_client.state());
        current_mqtt_status = MQTT_STATUS_FAILED;
        connection_start_time = 0;
        return false;
    }
}

void mqtt_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from MQTT");
    mqtt_client.disconnect();
    current_mqtt_status = MQTT_STATUS_DISCONNECTED;
}

void mqtt_reconnect(void) {
    mqtt_disconnect();
    device_delay(100);
    mqtt_connect();
}

mqtt_connection_status_t mqtt_get_status(void) {
    return current_mqtt_status;
}

const char* mqtt_get_status_string(void) {
    switch (current_mqtt_status) {
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

bool mqtt_is_connected(void) {
    return current_mqtt_status == MQTT_STATUS_CONNECTED && mqtt_client.connected();
}

unsigned long mqtt_get_last_activity(void) {
    return last_activity_time;
}

bool mqtt_publish(const char* topic, const char* payload) {
    if (!mqtt_is_connected()) {
        ESP_LOGW(TAG, "Cannot publish: MQTT not connected");
        return false;
    }

    if (topic == nullptr || payload == nullptr) {
        ESP_LOGE(TAG, "Cannot publish: topic or payload is null");
        return false;
    }

    ESP_LOGI(TAG, "Publishing to topic '%s': %s", topic, payload);
    bool result = mqtt_client.publish(topic, payload);

    if (result) {
        last_activity_time = device_get_millis();
    } else {
        ESP_LOGE(TAG, "Failed to publish message");
    }

    return result;
}

bool mqtt_publish_delayed(const char* topic, const char* payload) {
    if (topic == nullptr || payload == nullptr) {
        ESP_LOGE(TAG, "Cannot publish delayed: topic or payload is null");
        return false;
    }

    // Update existing message or create new one (single message queue like original)
    strncpy(publish_queue[0].topic, topic, MQTT_MAX_TOPIC_LENGTH - 1);
    publish_queue[0].topic[MQTT_MAX_TOPIC_LENGTH - 1] = '\0';

    strncpy(publish_queue[0].payload, payload, MQTT_MAX_PAYLOAD_LENGTH - 1);
    publish_queue[0].payload[MQTT_MAX_PAYLOAD_LENGTH - 1] = '\0';

    publish_queue[0].timestamp = device_get_millis();
    publish_queue[0].valid = true;
    queue_has_message = true;

    ESP_LOGI(TAG, "Queued delayed message for topic '%s'", topic);
    return true;
}

void mqtt_publish_system_status(void) {
    if (!mqtt_is_connected()) {
        return;
    }

    // Create system status JSON
    JsonDocument doc;
    doc["device"] = "ESP32SmartDisplay";
    doc["ip"] = network_get_ip_address();
    doc["rssi"] = network_get_signal_strength();
    doc["free_heap"] = device_get_free_heap();
    doc["uptime"] = device_get_millis();
    doc["wifi_status"] = network_get_wifi_status_string();
    doc["mqtt_status"] = mqtt_get_status_string();

    String status_json;
    serializeJson(doc, status_json);

    mqtt_publish("homeassistant/smartdisplay/status", status_json.c_str());
}

bool mqtt_subscribe(const char* topic) {
    if (!mqtt_is_connected()) {
        ESP_LOGW(TAG, "Cannot subscribe: MQTT not connected");
        return false;
    }

    if (topic == nullptr) {
        ESP_LOGE(TAG, "Cannot subscribe: topic is null");
        return false;
    }

    bool result = mqtt_client.subscribe(topic);
    if (result) {
        ESP_LOGI(TAG, "Subscribed to topic: %s", topic);
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
    }

    return result;
}

bool mqtt_unsubscribe(const char* topic) {
    if (!mqtt_is_connected()) {
        return false;
    }

    if (topic == nullptr) {
        return false;
    }

    bool result = mqtt_client.unsubscribe(topic);
    if (result) {
        ESP_LOGI(TAG, "Unsubscribed from topic: %s", topic);
    }

    return result;
}

bool mqtt_register_handler(const mqtt_handler_t* handler) {
    if (handler == nullptr) {
        ESP_LOGE(TAG, "Cannot register null handler");
        return false;
    }

    if (handler_count >= MQTT_MAX_HANDLERS) {
        ESP_LOGE(TAG, "Cannot register handler: maximum handlers reached");
        return false;
    }

    // Store handler pointer
    registered_handlers[handler_count] = const_cast<mqtt_handler_t*>(handler);
    handler_count++;

    ESP_LOGI(TAG, "Registered MQTT handler: %s", handler->identifier);

    // Subscribe immediately if connected
    if (mqtt_is_connected() && strlen(handler->subscribe_topic) > 0) {
        mqtt_subscribe(handler->subscribe_topic);
    }

    return true;
}

bool mqtt_unregister_handler(const char* identifier) {
    if (identifier == nullptr) {
        return false;
    }

    for (int i = 0; i < handler_count; i++) {
        if (registered_handlers[i] != nullptr &&
            strcmp(registered_handlers[i]->identifier, identifier) == 0) {
            // Unsubscribe if connected
            if (mqtt_is_connected() && strlen(registered_handlers[i]->subscribe_topic) > 0) {
                mqtt_unsubscribe(registered_handlers[i]->subscribe_topic);
            }

            // Remove handler by shifting array
            for (int j = i; j < handler_count - 1; j++) {
                registered_handlers[j] = registered_handlers[j + 1];
            }
            registered_handlers[handler_count - 1] = nullptr;
            handler_count--;

            ESP_LOGI(TAG, "Unregistered MQTT handler: %s", identifier);
            return true;
        }
    }

    ESP_LOGW(TAG, "Handler not found: %s", identifier);
    return false;
}

void mqtt_clear_publish_queue(void) {
    queue_has_message = false;
    publish_queue[0].valid = false;
    ESP_LOGI(TAG, "Publish queue cleared");
}

int mqtt_get_signal_quality(void) {
    return network_get_signal_strength();
}

// Private function implementations
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    char payload_str[MQTT_MAX_PAYLOAD_LENGTH];
    size_t copy_length = (length < MQTT_MAX_PAYLOAD_LENGTH - 1) ? length : MQTT_MAX_PAYLOAD_LENGTH - 1;
    memcpy(payload_str, payload, copy_length);
    payload_str[copy_length] = '\0';

    ESP_LOGI(TAG, "Received message - Topic: %s, Payload: %s", topic, payload_str);

    // Find and call appropriate handler
    mqtt_handler_t* handler = find_handler_by_topic(topic);
    if (handler != nullptr && handler->callback != nullptr) {
        handler->callback(topic, payload_str);
        ESP_LOGI(TAG, "Message handled by: %s", handler->identifier);
    } else {
        ESP_LOGW(TAG, "No handler found for topic: %s", topic);
    }

    last_activity_time = device_get_millis();
}

static void update_connection_status(void) {
    bool is_connected = mqtt_client.connected();

    if (is_connected && current_mqtt_status != MQTT_STATUS_CONNECTED) {
        current_mqtt_status = MQTT_STATUS_CONNECTED;
        ESP_LOGI(TAG, "MQTT connection established");
    } else if (!is_connected && current_mqtt_status == MQTT_STATUS_CONNECTED) {
        current_mqtt_status = MQTT_STATUS_DISCONNECTED;
        ESP_LOGW(TAG, "MQTT connection lost");
    }
}

static void process_publish_queue(void) {
    if (!queue_has_message || !publish_queue[0].valid) {
        return;
    }

    unsigned long current_time = device_get_millis();
    if (current_time - publish_queue[0].timestamp >= MQTT_PUBLISH_DELAY_MS) {
        if (mqtt_is_connected()) {
            ESP_LOGI(TAG, "Publishing delayed message - Topic: %s, Payload: %s",
                     publish_queue[0].topic, publish_queue[0].payload);
            mqtt_publish(publish_queue[0].topic, publish_queue[0].payload);
        } else {
            ESP_LOGW(TAG, "Cannot publish delayed message: MQTT not connected");
        }

        // Clear the queue
        mqtt_clear_publish_queue();
    }
}

static void subscribe_to_registered_handlers(void) {
    ESP_LOGI(TAG, "Subscribing to %d registered handlers", handler_count);

    for (int i = 0; i < handler_count; i++) {
        if (registered_handlers[i] != nullptr &&
            strlen(registered_handlers[i]->subscribe_topic) > 0) {
            mqtt_subscribe(registered_handlers[i]->subscribe_topic);
        }
    }
}

static mqtt_handler_t* find_handler_by_topic(const char* topic) {
    if (topic == nullptr) {
        return nullptr;
    }

    for (int i = 0; i < handler_count; i++) {
        if (registered_handlers[i] != nullptr &&
            strcmp(registered_handlers[i]->subscribe_topic, topic) == 0) {
            return registered_handlers[i];
        }
    }

    return nullptr;
}