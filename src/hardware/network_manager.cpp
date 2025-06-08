#include "network_manager.h"
#include "device_manager.h"
#include "mqtt_manager.h"
#include <esp_log.h>

static const char* TAG = "NetworkManager";

// Private variables
static wifi_connection_status_t current_wifi_status = WIFI_STATUS_DISCONNECTED;
static unsigned long last_connection_attempt = 0;
static unsigned long connection_start_time = 0;
static String current_ip_address = "";
static String current_ssid = "";
static bool initialization_complete = false;

// Private function declarations
static void wifi_event_handler(WiFiEvent_t event);
static void update_connection_status(void);

bool network_manager_init(void) {
    ESP_LOGI(TAG, "Initializing network manager");

    // Set WiFi mode to station
    WiFi.mode(WIFI_STA);

    // Register event handler
    WiFi.onEvent(wifi_event_handler);

    // Initialize status
    current_wifi_status = WIFI_STATUS_DISCONNECTED;
    current_ip_address = "";
    current_ssid = "";

    // Start WiFi connection
    network_connect_wifi();

    // Initialize MQTT manager
    ESP_LOGI(TAG, "Initializing MQTT manager");
    mqtt_manager_init();

    initialization_complete = true;
    return true;
}

void network_manager_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing network manager");

    // Deinitialize MQTT manager first
    mqtt_manager_deinit();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    initialization_complete = false;
}

void network_manager_update(void) {
    if (!initialization_complete) {
        return;
    }

    update_connection_status();

    // Update MQTT manager
    mqtt_manager_update();

    // Handle reconnection logic
    unsigned long now = device_get_millis();

    switch (current_wifi_status) {
        case WIFI_STATUS_CONNECTING:
            // Check for connection timeout
            if (connection_start_time > 0 &&
                (now - connection_start_time) > WIFI_CONNECT_TIMEOUT_MS) {
                ESP_LOGW(TAG, "WiFi connection timeout");
                current_wifi_status = WIFI_STATUS_FAILED;
                WiFi.disconnect();
            }
            break;

        case WIFI_STATUS_FAILED:
        case WIFI_STATUS_DISCONNECTED:
            // Attempt reconnection if enough time has passed
            if ((now - last_connection_attempt) >= WIFI_RECONNECT_INTERVAL_MS) {
                ESP_LOGI(TAG, "Attempting WiFi reconnection");
                network_reconnect_wifi();
            }
            break;

        case WIFI_STATUS_CONNECTED:
            // Periodically check if we're still connected
            if (WiFi.status() != WL_CONNECTED) {
                ESP_LOGW(TAG, "WiFi connection lost");
                current_wifi_status = WIFI_STATUS_DISCONNECTED;
                current_ip_address = "";
            }
            break;
    }
}

wifi_connection_status_t network_get_wifi_status(void) {
    return current_wifi_status;
}

const char* network_get_wifi_status_string(void) {
    switch (current_wifi_status) {
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

const char* network_get_ssid(void) {
    return current_ssid.c_str();
}

const char* network_get_ip_address(void) {
    return current_ip_address.c_str();
}

int network_get_signal_strength(void) {
    if (current_wifi_status == WIFI_STATUS_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

bool network_is_connected(void) {
    return current_wifi_status == WIFI_STATUS_CONNECTED;
}

void network_connect_wifi(void) {
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);

    current_wifi_status = WIFI_STATUS_CONNECTING;
    connection_start_time = device_get_millis();
    last_connection_attempt = connection_start_time;
    current_ssid = String(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void network_disconnect_wifi(void) {
    ESP_LOGI(TAG, "Disconnecting WiFi");
    WiFi.disconnect();
    current_wifi_status = WIFI_STATUS_DISCONNECTED;
    current_ip_address = "";
}

void network_reconnect_wifi(void) {
    WiFi.disconnect();
    delay(100);
    network_connect_wifi();
}

// Private function implementations
static void wifi_event_handler(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGI(TAG, "WiFi station started");
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to: %s", WiFi.SSID().c_str());
            current_ssid = WiFi.SSID();
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "WiFi got IP: %s", WiFi.localIP().toString().c_str());
            current_wifi_status = WIFI_STATUS_CONNECTED;
            current_ip_address = WiFi.localIP().toString();
            connection_start_time = 0;  // Reset connection timer

            // Attempt MQTT connection when WiFi is ready
            if (!mqtt_is_connected()) {
                ESP_LOGI(TAG, "WiFi connected, attempting MQTT connection");
                mqtt_connect();
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            current_wifi_status = WIFI_STATUS_DISCONNECTED;
            current_ip_address = "";

            // Disconnect MQTT when WiFi is lost
            if (mqtt_is_connected()) {
                ESP_LOGI(TAG, "WiFi disconnected, disconnecting MQTT");
                mqtt_disconnect();
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
            ESP_LOGW(TAG, "WiFi auth mode changed");
            break;

        default:
            break;
    }
}

static void update_connection_status(void) {
    wl_status_t wifi_status = WiFi.status();

    switch (wifi_status) {
        case WL_CONNECTED:
            if (current_wifi_status != WIFI_STATUS_CONNECTED) {
                current_wifi_status = WIFI_STATUS_CONNECTED;
                current_ip_address = WiFi.localIP().toString();
                current_ssid = WiFi.SSID();
            }
            break;

        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
            if (current_wifi_status == WIFI_STATUS_CONNECTING) {
                current_wifi_status = WIFI_STATUS_FAILED;
            } else if (current_wifi_status == WIFI_STATUS_CONNECTED) {
                current_wifi_status = WIFI_STATUS_DISCONNECTED;
            }
            current_ip_address = "";
            break;

        case WL_DISCONNECTED:
            if (current_wifi_status != WIFI_STATUS_CONNECTING) {
                current_wifi_status = WIFI_STATUS_DISCONNECTED;
                current_ip_address = "";
            }
            break;

        default:
            // WL_IDLE_STATUS and others - keep current status
            break;
    }
}