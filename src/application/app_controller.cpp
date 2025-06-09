#include "app_controller.h"
#include "../display/display_manager.h"
#include "../hardware/device_manager.h"
#include "../hardware/network_manager.h"
#include "../hardware/mqtt_manager.h"
#include "../events/ui_event_handlers.h"
#include <ui/ui.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include <vector>

// Private variables
static unsigned long next_update_millis = 0;
static const char* TAG = "AppController";

// Audio status handler
static mqtt_handler_t audio_status_handler;

// Structure to hold key-value pairs
struct KeyValuePair {
    String key;
    int value;
};

// JSON parsing function that returns a list of string:number key pairs
std::vector<KeyValuePair> parse_json_string_number_pairs(const char* json_payload) {
    std::vector<KeyValuePair> result;

    if (!json_payload) {
        ESP_LOGE(TAG, "Invalid JSON payload");
        return result;
    }

    // Create a JSON document with enough capacity
    DynamicJsonDocument doc(1024);

    // Parse the JSON
    DeserializationError error = deserializeJson(doc, json_payload);

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
            // ESP_LOGI(TAG, "Parsed: %s = %d", key, value);

            // Add to result vector
            KeyValuePair kvp;
            kvp.key = String(key);
            kvp.value = value;
            result.push_back(kvp);
        } else {
            ESP_LOGW(TAG, "Skipping non-numeric value for key: %s", key);
        }
    }

    // ESP_LOGI(TAG, "Successfully parsed %d key-value pairs", result.size());
    return result;
}

// Audio status handler callback function
static void audio_status_message_handler(const char* topic, const char* payload) {
    std::vector<KeyValuePair> audioLevels = parse_json_string_number_pairs(payload);

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
static void initialize_audio_status_handler(void) {
    strcpy(audio_status_handler.identifier, "AudioStatusHandler");
    strcpy(audio_status_handler.subscribe_topic, "homeassistant/unimix/audio_status");
    strcpy(audio_status_handler.publish_topic, "homeassistant/unimix/audio/requests");
    audio_status_handler.callback = audio_status_message_handler;
    audio_status_handler.active = true;
}

bool app_controller_init(void) {
    // Initialize hardware/device manager
    if (!device_manager_init()) {
        return false;
    }

    // Initialize network manager
    if (!network_manager_init()) {
        return false;
    }

    // Initialize display manager
    if (!display_manager_init()) {
        return false;
    }

    // Initialize and register audio status handler
    initialize_audio_status_handler();
    if (!mqtt_register_handler(&audio_status_handler)) {
        ESP_LOGE(TAG, "Failed to register audio status handler");
    } else {
        ESP_LOGI(TAG, "Audio status handler registered successfully");
    }

    // Setup UI components
    app_controller_setup_ui_components();

    // Initialize timing
    next_update_millis = device_get_millis() + APP_UPDATE_INTERVAL_MS;

    return true;
}

void app_controller_deinit(void) {
    network_manager_deinit();
    display_manager_deinit();
    device_manager_deinit();
}

void app_controller_run(void) {
    unsigned long now = device_get_millis();

    // Update network manager
    network_manager_update();

    // Update periodic data
    if (now >= next_update_millis) {
        app_controller_update_periodic_data();
        app_controller_update_network_status();
        next_update_millis = now + APP_UPDATE_INTERVAL_MS;
    }

#ifdef BOARD_HAS_RGB_LED
    // Update LED colors
    device_led_cycle_colors();
#endif

    // Update display
    display_manager_update();
}

void app_controller_setup_ui_components(void) {
    // Set display to 180 degrees rotation
    display_set_rotation(DISPLAY_ROTATION_180);

    // Register button click event handler
    lv_obj_add_event_cb(ui_btnRequestData, ui_btnRequestData_clicked_handler, LV_EVENT_CLICKED, NULL);
}

void app_controller_update_periodic_data(void) {
    // This function can be used for any Screen1-specific periodic data updates
    // Currently focused on network status updates only
}

void app_controller_update_network_status(void) {
    // Get network status
    const char* wifi_status = network_get_wifi_status_string();
    bool is_connected = network_is_connected();
    const char* ssid = network_get_ssid();
    const char* ip_address = network_get_ip_address();

    // Update WiFi status and indicator
    display_update_wifi_status(ui_lblWifiStatus, ui_objWifiIndicator, wifi_status, is_connected);

    // Update network information
    display_update_network_info(ui_lblSSIDValue, ui_lblIPValue, ssid, ip_address);

    // Get MQTT status
    const char* mqtt_status = mqtt_get_status_string();

    // Update MQTT status
    display_update_mqtt_status(ui_lblMQTTValue, mqtt_status);
}