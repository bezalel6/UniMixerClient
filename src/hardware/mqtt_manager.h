#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef __cplusplus
extern "C" {
#endif

// MQTT configuration constants
#define MQTT_SERVER "rndev.local"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "ESP32SmartDisplay"
#define MQTT_KEEPALIVE 60
#define MQTT_RECONNECT_INTERVAL_MS 5000  // 5 seconds between reconnection attempts
#define MQTT_CONNECT_TIMEOUT_MS 10000    // 10 seconds timeout for connection
#define MQTT_PUBLISH_DELAY_MS 200        // Delay between publish operations
#define MQTT_MAX_TOPIC_LENGTH 128
#define MQTT_MAX_PAYLOAD_LENGTH 512
#define MQTT_MAX_HANDLERS 10

// MQTT connection status enum
typedef enum {
    MQTT_STATUS_DISCONNECTED = 0,
    MQTT_STATUS_CONNECTING,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_FAILED,
    MQTT_STATUS_ERROR
} mqtt_connection_status_t;

// MQTT message structure for delayed publishing
typedef struct {
    char topic[MQTT_MAX_TOPIC_LENGTH];
    char payload[MQTT_MAX_PAYLOAD_LENGTH];
    unsigned long timestamp;
    bool valid;
} mqtt_message_t;

// MQTT message callback function type
typedef void (*mqtt_message_callback_t)(const char* topic, const char* payload);

// MQTT handler structure
typedef struct {
    char identifier[64];
    char subscribe_topic[MQTT_MAX_TOPIC_LENGTH];
    char publish_topic[MQTT_MAX_TOPIC_LENGTH];
    mqtt_message_callback_t callback;
    bool active;
} mqtt_handler_t;

// MQTT manager initialization and control
bool mqtt_manager_init(void);
void mqtt_manager_deinit(void);
void mqtt_manager_update(void);

// Connection management
bool mqtt_connect(void);
void mqtt_disconnect(void);
void mqtt_reconnect(void);

// Status query functions
mqtt_connection_status_t mqtt_get_status(void);
const char* mqtt_get_status_string(void);
bool mqtt_is_connected(void);
unsigned long mqtt_get_last_activity(void);

// Publishing functions
bool mqtt_publish(const char* topic, const char* payload);
bool mqtt_publish_delayed(const char* topic, const char* payload);
void mqtt_publish_system_status(void);

// Subscription and handler management
bool mqtt_subscribe(const char* topic);
bool mqtt_unsubscribe(const char* topic);
bool mqtt_register_handler(const mqtt_handler_t* handler);
bool mqtt_unregister_handler(const char* identifier);

// Utility functions
void mqtt_clear_publish_queue(void);
int mqtt_get_signal_quality(void);

#ifdef __cplusplus
}
#endif

#endif  // MQTT_MANAGER_H