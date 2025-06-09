#include "AppController.h"
#include "AudioStatusManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/MqttManager.h"
#include "../events/UiEventHandlers.h"
#include <ui/ui.h>
#include <esp_log.h>

// Private variables
static unsigned long nextUpdateMillis = 0;
static const char* TAG = "AppController";

namespace Application {

bool init(void) {
    ESP_LOGI(TAG, "Initializing Application Controller");

    // Initialize hardware/device manager
    if (!Hardware::Device::init()) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
        return false;
    }

    // Initialize network manager
    if (!Hardware::Network::init()) {
        ESP_LOGE(TAG, "Failed to initialize network manager");
        return false;
    }

    // Initialize display manager
    if (!Display::init()) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return false;
    }

    // Initialize audio status manager
    if (!Application::Audio::StatusManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize audio status manager");
        return false;
    }

    // Setup UI components
    setupUiComponents();

    // Initialize timing
    nextUpdateMillis = Hardware::Device::getMillis() + APP_UPDATE_INTERVAL_MS;

    ESP_LOGI(TAG, "Application Controller initialized successfully");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Application Controller");

    Application::Audio::StatusManager::deinit();
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
        updateAudioStatus();
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

    // Update MQTT status with indicator support
    Display::updateMqttStatus(ui_lblMQTTValue, ui_objMQTTIndicator, mqttStatus);
}

void updateAudioStatus(void) {
    // Get audio statistics from the AudioStatusManager
    int activeProcessCount = Application::Audio::StatusManager::getActiveProcessCount();
    int totalVolume = Application::Audio::StatusManager::getTotalVolume();

    // Log audio status information
    if (activeProcessCount > 0) {
        ESP_LOGD(TAG, "Audio Status - Active Processes: %d, Total Volume: %d",
                 activeProcessCount, totalVolume);

        // Get the process with highest volume
        Application::Audio::AudioLevel highest = Application::Audio::StatusManager::getHighestVolumeProcess();
        if (!highest.processName.isEmpty()) {
            ESP_LOGD(TAG, "Highest Volume Process: %s (%d)",
                     highest.processName.c_str(), highest.volume);
        }
    }
}

}  // namespace Application