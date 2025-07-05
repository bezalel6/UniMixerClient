#include "SimpleOTA.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "BootManager.h"
#include <esp_log.h>

static const char* TAG = "SimpleOTA";

namespace SimpleOTA {

// =============================================================================
// SIMPLE STATIC VARIABLES
// =============================================================================

static Config g_config;
static bool g_running = false;
static uint8_t g_progress = 0;
static char g_statusMessage[128] = "Ready";
static HTTPUpdate g_httpUpdate;

// =============================================================================
// SIMPLE IMPLEMENTATION
// =============================================================================

bool init(const Config& config) {
    ESP_LOGI(TAG, "Initializing SimpleOTA");

    g_config = config;
    g_running = false;
    g_progress = 0;
    strcpy(g_statusMessage, "SimpleOTA Ready");

    // Setup HTTPUpdate callbacks
    g_httpUpdate.onProgress([](int current, int total) {
        if (total > 0) {
            g_progress = 5 + ((current * 90) / total);  // 5-95% for download

            char msg[128];
            if (current > 1024 && total > 1024) {
                snprintf(msg, sizeof(msg), "Downloading: %.1f/%.1f KB",
                        current / 1024.0f, total / 1024.0f);
            } else {
                snprintf(msg, sizeof(msg), "Downloading: %d/%d bytes", current, total);
            }

            strncpy(g_statusMessage, msg, sizeof(g_statusMessage) - 1);
            g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';

            if (g_config.showProgress) {
                Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
            }

            ESP_LOGI(TAG, "Progress: %d%% - %s", g_progress, g_statusMessage);
        }
    });

    g_httpUpdate.setLedPin(-1);           // Disable LED indication
    g_httpUpdate.rebootOnUpdate(false);   // We handle reboot ourselves

    ESP_LOGI(TAG, "SimpleOTA initialized");
    return true;
}

bool startUpdate() {
    if (g_running) {
        ESP_LOGW(TAG, "OTA already in progress");
        return false;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", g_config.serverURL);
    g_running = true;
    g_progress = 0;

    // Show OTA screen
    if (g_config.showProgress) {
        Application::LVGLMessageHandler::showOtaScreen();
    }

    // Step 1: Connect to WiFi
    strcpy(g_statusMessage, "Connecting to WiFi...");
    g_progress = 5;
    if (g_config.showProgress) {
        Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_config.wifiSSID, g_config.wifiPassword);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);

        // Timeout check
        if (millis() - startTime > 30000) {
            strcpy(g_statusMessage, "WiFi connection timeout");
            g_running = false;
            ESP_LOGE(TAG, "WiFi connection timeout");
            return false;
        }

        // Update status
        wl_status_t status = WiFi.status();
        switch (status) {
            case WL_NO_SSID_AVAIL:
                strcpy(g_statusMessage, "Network not found");
                g_running = false;
                return false;
            case WL_CONNECT_FAILED:
                strcpy(g_statusMessage, "Connection failed - check password");
                g_running = false;
                return false;
            default:
                strcpy(g_statusMessage, "Connecting to WiFi...");
                break;
        }

        if (g_config.showProgress) {
            Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
        }
    }

    // WiFi connected
    char ipMsg[128];
    snprintf(ipMsg, sizeof(ipMsg), "WiFi connected - IP: %s", WiFi.localIP().toString().c_str());
    strcpy(g_statusMessage, ipMsg);
    g_progress = 10;

    if (g_config.showProgress) {
        Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
    }

    ESP_LOGI(TAG, "WiFi connected: %s", WiFi.localIP().toString().c_str());

    // Step 2: Download and install firmware
    strcpy(g_statusMessage, "Starting firmware download...");
    g_progress = 15;

    if (g_config.showProgress) {
        Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
    }

    WiFiClient client;
    HTTPUpdateResult result = g_httpUpdate.update(client, String(g_config.serverURL));

    switch (result) {
        case HTTP_UPDATE_OK:
            strcpy(g_statusMessage, "Update completed successfully");
            g_progress = 100;
            g_running = false;

            if (g_config.showProgress) {
                Application::LVGLMessageHandler::updateOtaScreenProgress(g_progress, g_statusMessage);
            }

            ESP_LOGI(TAG, "OTA update completed successfully");

            // Auto-reboot if configured
            if (g_config.autoReboot) {
                for (int i = 3; i > 0; i--) {
                    snprintf(g_statusMessage, sizeof(g_statusMessage),
                            "Rebooting in %d second%s...", i, (i == 1) ? "" : "s");

                    if (g_config.showProgress) {
                        Application::LVGLMessageHandler::updateOtaScreenProgress(100, g_statusMessage);
                    }

                    delay(1000);
                }

                ESP_LOGI(TAG, "Restarting system after successful OTA");
                esp_restart();
            }

            return true;

        case HTTP_UPDATE_NO_UPDATES:
            strcpy(g_statusMessage, "No updates available");
            g_running = false;
            ESP_LOGW(TAG, "No updates available");
            return false;

        case HTTP_UPDATE_FAILED:
            {
                String error = g_httpUpdate.getLastErrorString();
                strncpy(g_statusMessage, error.c_str(), sizeof(g_statusMessage) - 1);
                g_statusMessage[sizeof(g_statusMessage) - 1] = '\0';
                g_running = false;
                ESP_LOGE(TAG, "HTTP Update failed: %s", error.c_str());
                return false;
            }

        default:
            strcpy(g_statusMessage, "Unknown update error");
            g_running = false;
            ESP_LOGE(TAG, "Unknown update error");
            return false;
    }
}

bool isRunning() {
    return g_running;
}

uint8_t getProgress() {
    return g_progress;
}

const char* getStatusMessage() {
    return g_statusMessage;
}

void deinit() {
    ESP_LOGI(TAG, "Deinitializing SimpleOTA");
    g_running = false;
    WiFi.disconnect();
}

bool initWithDefaults() {
    Config defaultConfig;  // Uses default values from struct
    return init(defaultConfig);
}

} // namespace SimpleOTA
