#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>

// Network configuration constants
#define WIFI_SSID "IOT"                   // Replace with your WiFi SSID
#define WIFI_PASSWORD "0527714039a"       // Replace with your WiFi password
#define WIFI_CONNECT_TIMEOUT_MS 10000     // 10 seconds timeout for connection
#define WIFI_RECONNECT_INTERVAL_MS 30000  // 30 seconds between reconnection attempts

// WiFi status enum
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_connection_status_t;

// Network manager functions
bool network_manager_init(void);
void network_manager_deinit(void);
void network_manager_update(void);

// Status query functions
wifi_connection_status_t network_get_wifi_status(void);
const char* network_get_wifi_status_string(void);
const char* network_get_ssid(void);
const char* network_get_ip_address(void);
int network_get_signal_strength(void);
bool network_is_connected(void);

// Connection control functions
void network_connect_wifi(void);
void network_disconnect_wifi(void);
void network_reconnect_wifi(void);

#endif  // NETWORK_MANAGER_H