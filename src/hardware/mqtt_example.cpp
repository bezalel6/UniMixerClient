#include "mqtt_example.h"
#include "mqtt_manager.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "MQTTExample";

// Example handler callback function
static void example_message_handler(const char* topic, const char* payload) {
    ESP_LOGI(TAG, "Example handler received - Topic: %s, Payload: %s", topic, payload);

    // Example: Echo the message back
    char response_topic[128];
    snprintf(response_topic, sizeof(response_topic), "homeassistant/smartdisplay/response");

    char response_payload[256];
    snprintf(response_payload, sizeof(response_payload), "Received: %s", payload);

    mqtt_publish_delayed(response_topic, response_payload);
}

// Example MQTT handler configuration
static mqtt_handler_t example_handler;

static void initialize_example_handler(void) {
    strcpy(example_handler.identifier, "ExampleHandler");
    strcpy(example_handler.subscribe_topic, "homeassistant/smartdisplay/command");
    strcpy(example_handler.publish_topic, "homeassistant/smartdisplay/response");
    example_handler.callback = example_message_handler;
    example_handler.active = true;
}

void mqtt_example_init(void) {
    ESP_LOGI(TAG, "Initializing MQTT example");

    // Initialize the handler structure
    initialize_example_handler();

    // Register the handler
    if (mqtt_register_handler(&example_handler)) {
        ESP_LOGI(TAG, "Example handler registered successfully");
    } else {
        ESP_LOGE(TAG, "Failed to register example handler");
    }
}

void mqtt_example_publish_test_message(void) {
    if (!mqtt_is_connected()) {
        ESP_LOGW(TAG, "Cannot publish test message: MQTT not connected");
        return;
    }

    ESP_LOGI(TAG, "Publishing test message");
    mqtt_publish("homeassistant/smartdisplay/test", "Hello from ESP32 Smart Display!");
}

void mqtt_example_publish_sensor_data(float temperature, float humidity) {
    if (!mqtt_is_connected()) {
        return;
    }

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"temperature\":%.2f,\"humidity\":%.2f}", temperature, humidity);

    mqtt_publish_delayed("homeassistant/smartdisplay/sensors", payload);
}