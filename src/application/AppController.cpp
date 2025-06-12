#include "AppController.h"
#include "AudioStatusManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../messaging/MessageBus.h"
#include "../events/UiEventHandlers.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include <ui/ui.h>
#include <esp_log.h>

// Private variables
static const char* TAG = "AppController";
static unsigned long lastNetworkUpdate = 0;
static unsigned long lastMessageBusUpdate = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastOTAUpdate = 0;

// Update intervals (in milliseconds)
#define NETWORK_UPDATE_INTERVAL 1000
#define MESSAGE_BUS_UPDATE_INTERVAL 10
#define DISPLAY_UPDATE_INTERVAL 200
#define OTA_UPDATE_INTERVAL 5000

namespace Application {

// Private helper function declarations
static void updateNetworkStatus(void);
static void updateDisplayElements(void);
static void updateOTA(void);

bool init(void) {
    ESP_LOGI(TAG, "Initializing Application Controller (single-threaded)");

    // Initialize hardware/device manager
    if (!Hardware::Device::init()) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
        return false;
    }

    // Initialize display manager
    if (!Display::init()) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return false;
    }

    // Initialize messaging system
    if (!Messaging::MessageBus::Init()) {
        ESP_LOGE(TAG, "Failed to initialize messaging system");
        return false;
    }

    // Determine if network manager is needed (for MQTT or OTA)
    bool networkNeeded = false;

#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2
    networkNeeded = true;  // MQTT transport modes need network
#endif

#if OTA_ENABLE_UPDATES
    networkNeeded = true;  // OTA always needs network
#endif

    // Initialize network manager if needed
    if (networkNeeded) {
        ESP_LOGI(TAG, "Network required for MQTT/OTA - initializing network manager");
        if (!Hardware::Network::init()) {
            ESP_LOGE(TAG, "Failed to initialize network manager");
            return false;
        }

        // Enable auto-reconnect (WiFi connection will be started automatically by NetworkManager)
        Hardware::Network::enableAutoReconnect(true);
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
    // Serial only - no additional network configuration needed
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

    // Initialize audio status manager (requires MessageBus to be initialized)
    if (!Application::Audio::StatusManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize audio status manager");
        return false;
    }

    // Send initial status request to get current audio information
    ESP_LOGI(TAG, "Sending initial status request");
    Application::Audio::StatusManager::publishAudioStatusRequest();

    // Setup UI components
    setupUiComponents();

    ESP_LOGI(TAG, "Application Controller initialized successfully (single-threaded)");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Application Controller");

    Application::Audio::StatusManager::deinit();

    // Deinitialize messaging system
    Messaging::MessageBus::Deinit();

    // Deinitialize network manager if it was initialized
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || OTA_ENABLE_UPDATES
    Hardware::Network::deinit();
#endif

    Display::deinit();
    Hardware::Device::deinit();
}

void run(void) {
    unsigned long currentTime = millis();

    // Update LVGL tick system
    Display::tickUpdate();

    // Handle LVGL tasks and rendering
    lv_timer_handler();

    // Update message bus (high frequency for responsiveness)
    if (currentTime - lastMessageBusUpdate >= MESSAGE_BUS_UPDATE_INTERVAL) {
        Messaging::MessageBus::Update();
        lastMessageBusUpdate = currentTime;
    }

    // Update network status (lower frequency)
    if (currentTime - lastNetworkUpdate >= NETWORK_UPDATE_INTERVAL) {
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || OTA_ENABLE_UPDATES
        Hardware::Network::update();
        updateNetworkStatus();
#endif
        lastNetworkUpdate = currentTime;
    }

    // Update display elements (medium frequency)
    if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        updateDisplayElements();
        lastDisplayUpdate = currentTime;
    }

    // Update OTA (very low frequency)
    if (currentTime - lastOTAUpdate >= OTA_UPDATE_INTERVAL) {
#if OTA_ENABLE_UPDATES
        updateOTA();
#endif
        lastOTAUpdate = currentTime;
    }

#ifdef BOARD_HAS_RGB_LED
    // Update LED colors
    Hardware::Device::ledCycleColors();
#endif

    // Count frames for FPS calculation (only when we actually render)
    static unsigned long lastFrameTime = 0;
    if (currentTime - lastFrameTime >= 16) {  // ~60 FPS max
        Display::update();                    // Count this as a frame
        lastFrameTime = currentTime;
    }

    // No delay needed - LVGL handles its own timing
}

void setupUiComponents(void) {
    // Set display to 180 degrees rotation
    Display::setRotation(Display::ROTATION_0);

    // Register button click event handler
    lv_obj_add_event_cb(ui_btnRequestData, Events::UI::btnRequestDataClickedHandler, LV_EVENT_CLICKED, NULL);

    // Register audio device dropdown event handlers
    lv_obj_add_event_cb(ui_selectAudioDevice, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice1, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice2, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);

    // Register volume arc event handler
    lv_obj_add_event_cb(ui_volumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);

    // Register tab switch event handler
    ESP_LOGI(TAG, "Registering tab switch event handler for ui_tabsModeSwitch: %p", ui_tabsModeSwitch);
    lv_obj_add_event_cb(ui_tabsModeSwitch, Events::UI::tabSwitchHandler, LV_EVENT_VALUE_CHANGED, NULL);

    // Alternative approach: Register on individual tab buttons (for newer LVGL versions)
    lv_obj_t* tab_buttons = lv_tabview_get_tab_bar(ui_tabsModeSwitch);
    if (tab_buttons) {
        ESP_LOGI(TAG, "Registering tab button events on tab bar: %p", tab_buttons);

        // Get the number of tabs and register event on each button
        uint32_t tab_count = lv_obj_get_child_count(tab_buttons);
        ESP_LOGI(TAG, "Found %d tab buttons", tab_count);

        for (uint32_t i = 0; i < tab_count; i++) {
            lv_obj_t* tab_button = lv_obj_get_child(tab_buttons, i);
            if (tab_button) {
                ESP_LOGI(TAG, "Registering event on tab button %d: %p", i, tab_button);
                lv_obj_add_event_cb(tab_button, Events::UI::tabSwitchHandler, LV_EVENT_CLICKED, NULL);
            }
        }
    }

    // Initialize current tab state by reading the actual active tab from the UI
    uint32_t activeTabIndex = lv_tabview_get_tab_active(ui_tabsModeSwitch);
    Events::UI::setCurrentTab(static_cast<Events::UI::TabState>(activeTabIndex));
    ESP_LOGI(TAG, "Initialized tab state to index: %d (%s)", activeTabIndex, Events::UI::getTabName(Events::UI::getCurrentTab()));
}

// Private helper functions for single-threaded operation
static void updateNetworkStatus(void) {
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || OTA_ENABLE_UPDATES
    bool connected = Hardware::Network::isConnected();
    const char* status = Hardware::Network::getWifiStatusString();
    const char* ssid = Hardware::Network::getSsid();
    const char* ip = Hardware::Network::getIpAddress();

    // Update WiFi status display
    Display::updateWifiStatus(ui_lblWifiStatus, ui_objWifiIndicator, status, connected);

    // Update network info display
    Display::updateNetworkInfo(ui_lblSSIDValue, ui_lblIPValue, ssid, ip);
#endif
}

static void updateDisplayElements(void) {
    // Update audio status manager UI elements (more important)
    Application::Audio::StatusManager::onAudioLevelsChangedUI();

    // Update FPS display less frequently to reduce overhead
    static unsigned long lastFpsUpdate = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastFpsUpdate >= 500) {  // Update FPS every 500ms
        Display::updateFpsDisplay(ui_lblFPS);
        lastFpsUpdate = currentTime;
    }
}

static void updateOTA(void) {
#if OTA_ENABLE_UPDATES
    // Only check for OTA updates if WiFi is connected
    if (Hardware::Network::isConnected()) {
        Hardware::OTA::update();
    }
#endif
}

}  // namespace Application