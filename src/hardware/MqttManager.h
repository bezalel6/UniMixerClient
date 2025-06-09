#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Hardware {
namespace Mqtt {

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
} ConnectionStatus;

// MQTT message structure for delayed publishing
typedef struct {
    char topic[MQTT_MAX_TOPIC_LENGTH];
    char payload[MQTT_MAX_PAYLOAD_LENGTH];
    unsigned long timestamp;
    bool valid;
} Message;

// MQTT message callback function type
typedef void (*MessageCallback)(const char* topic, const char* payload);

// MQTT handler structure
typedef struct {
    char identifier[64];
    char subscribeTopic[MQTT_MAX_TOPIC_LENGTH];
    char publishTopic[MQTT_MAX_TOPIC_LENGTH];
    MessageCallback callback;
    bool active;
} Handler;

// MQTT manager initialization and control
bool init(void);
void deinit(void);
void update(void);

// Connection management
bool connect(void);
void disconnect(void);
void reconnect(void);

// Status query functions
ConnectionStatus getStatus(void);
const char* getStatusString(void);
bool isConnected(void);
unsigned long getLastActivity(void);

// Publishing functions
bool publish(const char* topic, const char* payload);
bool publishDelayed(const char* topic, const char* payload);
void publishSystemStatus(void);

// Subscription and handler management
bool subscribe(const char* topic);
bool unsubscribe(const char* topic);
bool registerHandler(const Handler* handler);
bool unregisterHandler(const char* identifier);

// Utility functions
void clearPublishQueue(void);
int getSignalQuality(void);

}  // namespace Mqtt
}  // namespace Hardware

#endif  // MQTT_MANAGER_H