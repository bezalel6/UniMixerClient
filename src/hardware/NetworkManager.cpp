#include "NetworkManager.h"
#include "DeviceManager.h"
#include "OTAManager.h"
#include "../../include/OTAConfig.h"
#include <esp_log.h>

static const char* TAG = "NetworkManager";

namespace Hardware {
namespace Network {

// Private variables
static WifiConnectionStatus currentWifiStatus = WIFI_STATUS_DISCONNECTED;
static unsigned long lastConnectionAttempt = 0;
static unsigned long connectionStartTime = 0;
static String currentIpAddress = "";
static String currentSsid = "";
static bool initializationComplete = false;
static bool autoReconnectEnabled = true;
static bool otaInitialized = false;

// Private function declarations
static void wifiEventHandler(WiFiEvent_t event);
static void updateConnectionStatus(void);
static void initializeNetworkComponents(void);
static void deinitializeNetworkComponents(void);

bool init(void) {
    ESP_LOGI(TAG, "Initializing network manager");

    // Set WiFi mode to station
    WiFi.mode(WIFI_STA);

    // Register event handler
    WiFi.onEvent(wifiEventHandler);

    // Initialize status
    currentWifiStatus = WIFI_STATUS_DISCONNECTED;
    currentIpAddress = "";
    currentSsid = "";
    autoReconnectEnabled = true;

    // Automatically start WiFi connection
    connectWifi();

    initializationComplete = true;
    ESP_LOGI(TAG, "Network manager initialized and WiFi connection initiated");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing network manager");

    // Deinitialize network components first
    deinitializeNetworkComponents();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    initializationComplete = false;
    autoReconnectEnabled = false;
}

void update(void) {
    if (!initializationComplete) {
        return;
    }

    updateConnectionStatus();

#if OTA_ENABLE_UPDATES
    // Update OTA manager if it's initialized
    if (otaInitialized) {
        Hardware::OTA::update();
    }
#endif

    // Handle reconnection logic only if auto-reconnect is enabled
    if (!autoReconnectEnabled) {
        return;
    }

    unsigned long now = Hardware::Device::getMillis();

    switch (currentWifiStatus) {
        case WIFI_STATUS_CONNECTING:
            // Check for connection timeout
            if (connectionStartTime > 0 &&
                (now - connectionStartTime) > WIFI_CONNECT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "WiFi connection timeout");
                currentWifiStatus = WIFI_STATUS_FAILED;
                WiFi.disconnect();
            }
            break;

        case WIFI_STATUS_FAILED:
        case WIFI_STATUS_DISCONNECTED:
            // Attempt reconnection if enough time has passed
            if ((now - lastConnectionAttempt) >= WIFI_RECONNECT_INTERVAL_MS) {
                ESP_LOGI(TAG, "Attempting WiFi reconnection");
                reconnectWifi();
            }
            break;

        case WIFI_STATUS_CONNECTED:
            // Periodically check if we're still connected
            if (WiFi.status() != WL_CONNECTED) {
                ESP_LOGW(TAG, "WiFi connection lost");
                currentWifiStatus = WIFI_STATUS_DISCONNECTED;
                currentIpAddress = "";
            }
            break;
    }
}

WifiConnectionStatus getWifiStatus(void) {
    return currentWifiStatus;
}

const char* getWifiStatusString(void) {
    switch (currentWifiStatus) {
        case WIFI_STATUS_DISCONNECTED:
            return "Disconnected";
        case WIFI_STATUS_CONNECTING:
            return "Connecting...";
        case WIFI_STATUS_CONNECTED:
            return "Connected";
        case WIFI_STATUS_FAILED:
            return "Failed";
        default:
            return "Unknown";
    }
}

const char* getSsid(void) {
    return currentSsid.c_str();
}

const char* getIpAddress(void) {
    return currentIpAddress.c_str();
}

int getSignalStrength(void) {
    if (currentWifiStatus == WIFI_STATUS_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

bool isConnected(void) {
    return currentWifiStatus == WIFI_STATUS_CONNECTED;
}

void connectWifi(void) {
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);

    currentWifiStatus = WIFI_STATUS_CONNECTING;
    connectionStartTime = Hardware::Device::getMillis();
    lastConnectionAttempt = connectionStartTime;
    currentSsid = String(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void disconnectWifi(void) {
    ESP_LOGI(TAG, "Disconnecting WiFi");
    WiFi.disconnect();
    currentWifiStatus = WIFI_STATUS_DISCONNECTED;
    currentIpAddress = "";
    autoReconnectEnabled = false;
}

void reconnectWifi(void) {
    WiFi.disconnect();
    delay(100);
    connectWifi();
}

void enableAutoReconnect(bool enable) {
    autoReconnectEnabled = enable;
    ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");
}

bool isAutoReconnectEnabled(void) {
    return autoReconnectEnabled;
}

bool isOtaReady(void) {
    return otaInitialized;
}

// Private function implementations
static void wifiEventHandler(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGI(TAG, "WiFi station started");
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to: %s", WiFi.SSID().c_str());
            currentSsid = WiFi.SSID();
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "WiFi got IP: %s", WiFi.localIP().toString().c_str());
            currentWifiStatus = WIFI_STATUS_CONNECTED;
            currentIpAddress = WiFi.localIP().toString();
            connectionStartTime = 0;  // Reset connection timer

            // Initialize network components
            initializeNetworkComponents();
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            currentWifiStatus = WIFI_STATUS_DISCONNECTED;
            currentIpAddress = "";

            // Deinitialize network components
            deinitializeNetworkComponents();
            break;

        case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
            ESP_LOGW(TAG, "WiFi auth mode changed");
            break;

        default:
            break;
    }
}

static void updateConnectionStatus(void) {
    wl_status_t wifi_status = WiFi.status();

    switch (wifi_status) {
        case WL_CONNECTED:
            if (currentWifiStatus != WIFI_STATUS_CONNECTED) {
                currentWifiStatus = WIFI_STATUS_CONNECTED;
                currentIpAddress = WiFi.localIP().toString();
                currentSsid = WiFi.SSID();
            }
            break;

        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
            if (currentWifiStatus == WIFI_STATUS_CONNECTING) {
                currentWifiStatus = WIFI_STATUS_FAILED;
            } else if (currentWifiStatus == WIFI_STATUS_CONNECTED) {
                currentWifiStatus = WIFI_STATUS_DISCONNECTED;
            }
            currentIpAddress = "";
            break;

        case WL_DISCONNECTED:
            if (currentWifiStatus != WIFI_STATUS_CONNECTING) {
                currentWifiStatus = WIFI_STATUS_DISCONNECTED;
                currentIpAddress = "";
            }
            break;

        default:
            // WL_IDLE_STATUS and others - keep current status
            break;
    }
}

static void initializeNetworkComponents(void) {
    ESP_LOGI(TAG, "Initializing network-dependent components");

#if OTA_ENABLE_UPDATES
    if (!otaInitialized) {
        ESP_LOGI(TAG, "Initializing OTA manager");
        if (Hardware::OTA::init()) {
            ESP_LOGI(TAG, "OTA manager initialized successfully");
            otaInitialized = true;
        } else {
            ESP_LOGW(TAG, "Failed to initialize OTA manager");
        }
    }
#endif
}

static void deinitializeNetworkComponents(void) {
    ESP_LOGI(TAG, "Deinitializing network-dependent components");

#if OTA_ENABLE_UPDATES
    if (otaInitialized) {
        ESP_LOGI(TAG, "Deinitializing OTA manager");
        Hardware::OTA::deinit();
        otaInitialized = false;
    }
#endif
}

}  // namespace Network
}  // namespace Hardware