#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>

namespace Hardware {
namespace Network {

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
} WifiConnectionStatus;

// Network manager functions
bool init(void);
void deinit(void);
void update(void);

// Status query functions
WifiConnectionStatus getWifiStatus(void);
const char* getWifiStatusString(void);
const char* getSsid(void);
const char* getIpAddress(void);
int getSignalStrength(void);
bool isConnected(void);

// Connection control functions
void connectWifi(void);
void disconnectWifi(void);
void reconnectWifi(void);

// Auto-reconnect control functions
void enableAutoReconnect(bool enable);
bool isAutoReconnectEnabled(void);

// Network-dependent component status
bool isOtaReady(void);

}  // namespace Network
}  // namespace Hardware

#endif  // NETWORK_MANAGER_H
