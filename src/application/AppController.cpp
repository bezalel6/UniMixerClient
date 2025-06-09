#include "AppController.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/MqttManager.h"
#include "../events/UiEventHandlers.h"
#include <ui/ui.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include <vector>

// Private variables
static unsigned long nextUpdateMillis = 0;
static const char* TAG = "AppController";

// Audio status handler
static Hardware::Mqtt::Handler audioStatusHandler;

// Structure to hold key-value pairs
struct KeyValuePair {
    String key;
    int value;
};

namespace Application {

// JSON parsing function that returns a list of string:number key pairs
std::vector<KeyValuePair> parseJsonStringNumberPairs(const char* jsonPayload) {
    std::vector<KeyValuePair> result;

    if (!jsonPayload) {
        ESP_LOGE(TAG, "Invalid JSON payload");
        return result;
    }

    // Create a JSON document
    JsonDocument doc;

    // Parse the JSON
    DeserializationError error = deserializeJson(doc, jsonPayload);

    if (error) {
        ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
        return result;
    }

    // Check if the root is an object
    if (!doc.is<JsonObject>()) {
        ESP_LOGE(TAG, "JSON root is not an object");
        return result;
    }

    JsonObject root = doc.as<JsonObject>();

    // Iterate through all key-value pairs
    for (JsonPair pair : root) {
        const char* key = pair.key().c_str();

        // Check if value is a number
        if (pair.value().is<int>()) {
            int value = pair.value().as<int>();
            // Add to result vector
            KeyValuePair kvp;
            kvp.key = String(key);
            kvp.value = value;
            result.push_back(kvp);
        } else {
            ESP_LOGW(TAG, "Skipping non-numeric value for key: %s", key);
        }
    }

    return result;
}

// Audio status handler callback function
static void audioStatusMessageHandler(const char* topic, const char* payload) {
    std::vector<KeyValuePair> audioLevels = parseJsonStringNumberPairs(payload);

    if (audioLevels.empty()) {
        ESP_LOGE(TAG, "Failed to parse audio status JSON or no valid data found");
        return;
    }

    // Process each audio level from the returned list
    for (const auto& level : audioLevels) {
        ESP_LOGI(TAG, "🔊 Audio Level - Process: %s, Volume: %d", level.key.c_str(), level.value);
    }
}

// Initialize audio status handler
static void initializeAudioStatusHandler(void) {
    strcpy(audioStatusHandler.identifier, "AudioStatusHandler");
    strcpy(audioStatusHandler.subscribeTopic, "homeassistant/unimix/audio_status");
    strcpy(audioStatusHandler.publishTopic, "homeassistant/unimix/audio/requests");
    audioStatusHandler.callback = audioStatusMessageHandler;
    audioStatusHandler.active = true;
}

bool init(void) {
    // Initialize hardware/device manager
    if (!Hardware::Device::init()) {
        return false;
    }

    // Initialize network manager
    if (!Hardware::Network::init()) {
        return false;
    }

    // Initialize display manager
    if (!Display::init()) {
        return false;
    }

    // Initialize and register audio status handler
    initializeAudioStatusHandler();
    if (!Hardware::Mqtt::registerHandler(&audioStatusHandler)) {
        ESP_LOGE(TAG, "Failed to register audio status handler");
    } else {
        ESP_LOGI(TAG, "Audio status handler registered successfully");
    }

    // Setup UI components
    setupUiComponents();

    // Initialize timing
    nextUpdateMillis = Hardware::Device::getMillis() + APP_UPDATE_INTERVAL_MS;

    return true;
}

void deinit(void) {
    Hardware::Network::deinit();
    Display::deinit();
    Hardware::Device::deinit();
}

void run(void) {
    unsigned long now = Hardware::Device::getMillis();

    // Update network manager
    Hardware::Network::update();

    // Update periodic data
    if (now >= nextUpdateMillis) {
        updatePeriodicData();
        updateNetworkStatus();
        nextUpdateMillis = now + APP_UPDATE_INTERVAL_MS;
    }

#ifdef BOARD_HAS_RGB_LED
    // Update LED colors
    Hardware::Device::ledCycleColors();
#endif

    // Update display
    Display::update();
}

void setupUiComponents(void) {
    // Set display to 180 degrees rotation
    Display::setRotation(Display::ROTATION_180);

    // Register button click event handler
    lv_obj_add_event_cb(ui_btnRequestData, Events::UI::btnRequestDataClickedHandler, LV_EVENT_CLICKED, NULL);
}

void updatePeriodicData(void) {
    // This function can be used for any Screen1-specific periodic data updates
    // Currently focused on network status updates only
}

void updateNetworkStatus(void) {
    // Get network status
    const char* wifiStatus = Hardware::Network::getWifiStatusString();
    bool isConnected = Hardware::Network::isConnected();
    const char* ssid = Hardware::Network::getSsid();
    const char* ipAddress = Hardware::Network::getIpAddress();

    // Update WiFi status and indicator
    Display::updateWifiStatus(ui_lblWifiStatus, ui_objWifiIndicator, wifiStatus, isConnected);

    // Update network information
    Display::updateNetworkInfo(ui_lblSSIDValue, ui_lblIPValue, ssid, ipAddress);

    // Get MQTT status
    const char* mqttStatus = Hardware::Mqtt::getStatusString();

    // Update MQTT status
    Display::updateMqttStatus(ui_lblMQTTValue, mqttStatus);
}

}  // namespace Application