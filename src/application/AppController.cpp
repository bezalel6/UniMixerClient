#include "AppController.h"
#include "AudioStatusManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../messaging/MessageBus.h"
#include "../events/UiEventHandlers.h"
#include "../../include/MessagingConfig.h"
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

    // Initialize messaging system
    if (!Messaging::MessageBus::Init()) {
        ESP_LOGE(TAG, "Failed to initialize messaging system");
        return false;
    }

    // Configure transport based on MessagingConfig.h settings
#if MESSAGING_DEFAULT_TRANSPORT == 0
// MQTT only
#if MESSAGING_ENABLE_MQTT_TRANSPORT
    ESP_LOGI(TAG, "Configuring MQTT transport (config: MQTT only)");
    Messaging::MessageBus::EnableMqttTransport();
#else
    ESP_LOGE(TAG, "MQTT transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 1
// Serial only
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring Serial transport (config: Serial only)");
    Messaging::MessageBus::EnableSerialTransport();
#else
    ESP_LOGE(TAG, "Serial transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 2
// Both transports
#if MESSAGING_ENABLE_MQTT_TRANSPORT && MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring dual transport (config: MQTT + Serial)");
    Messaging::MessageBus::EnableBothTransports();
#else
    ESP_LOGE(TAG, "Dual transport requested but one or both transports disabled in config");
    return false;
#endif
#else
    ESP_LOGE(TAG, "Invalid MESSAGING_DEFAULT_TRANSPORT value: %d", MESSAGING_DEFAULT_TRANSPORT);
    return false;
#endif

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
    Messaging::MessageBus::Deinit();
    Hardware::Network::deinit();
    Display::deinit();
    Hardware::Device::deinit();
}

void run(void) {
    unsigned long now = Hardware::Device::getMillis();

    // Update network manager
    Hardware::Network::update();

    // Update messaging system
    Messaging::MessageBus::Update();

    // Update periodic data
    if (now >= nextUpdateMillis) {
        updatePeriodicData();
        updateNetworkStatus();
        updateAudioStatus();
        updateFpsDisplay();
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

    // Register audio device dropdown event handlers
    lv_obj_add_event_cb(ui_selectAudioDevice, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice1, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice2, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);

    // Register volume arc event handler
    lv_obj_add_event_cb(ui_volumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
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

    // Get messaging status
    const char* messagingStatus = Messaging::MessageBus::GetStatusString();

    // Update messaging status with indicator support
    Display::updateMqttStatus(ui_lblMQTTValue, ui_objMQTTIndicator, messagingStatus);
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

void updateFpsDisplay(void) {
    // Update FPS display
    Display::updateFpsDisplay(ui_lblFPS);
}

}  // namespace Application