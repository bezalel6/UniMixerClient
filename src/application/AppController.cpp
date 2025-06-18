#include "AppController.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../messaging/MessageBus.h"
#include "../messaging/MessageHandlerRegistry.h"
#include "AudioController.h"
#include "LVGLMessageHandler.h"
#include "TaskManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <ui/ui.h>

// Private variables
static const char *TAG = "AppController";

namespace Application {

bool init(void) {
    ESP_LOGI(TAG,
             "Initializing Application Controller (Multi-threaded ESP32-S3)");

    // Initialize watchdog timer for startup debugging (15 seconds)
    ESP_LOGI(TAG, "Initializing startup watchdog timer...");
    esp_task_wdt_init(15, true);
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    // Initialize hardware/device manager
    ESP_LOGI(TAG, "WDT Reset: Initializing Device Manager...");
    if (!Hardware::Device::init()) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
        return false;
    }
    esp_task_wdt_reset();

    // Initialize display manager
    ESP_LOGI(TAG, "WDT Reset: Initializing Display Manager...");
    if (!Display::init()) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return false;
    }
    esp_task_wdt_reset();

    // Initialize messaging system
    ESP_LOGI(TAG, "WDT Reset: Initializing Message Bus...");
    if (!Messaging::MessageBus::Init()) {
        ESP_LOGE(TAG, "Failed to initialize messaging system");
        return false;
    }
    esp_task_wdt_reset();

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
        ESP_LOGI(TAG,
                 "Network required for MQTT/OTA - initializing network manager");
        if (!Hardware::Network::init()) {
            ESP_LOGE(TAG, "Failed to initialize network manager");
            return false;
        }

        // Enable auto-reconnect (WiFi connection will be started automatically by
        // NetworkManager)
        Hardware::Network::enableAutoReconnect(true);
    }
    esp_task_wdt_reset();

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
    ESP_LOGE(
        TAG,
        "Dual transport requested but one or both transports disabled in config");
    return false;
#endif
#else
    ESP_LOGE(TAG, "Invalid MESSAGING_DEFAULT_TRANSPORT value: %d",
             MESSAGING_DEFAULT_TRANSPORT);
    return false;
#endif
    esp_task_wdt_reset();

    // Register all message handlers centrally
    ESP_LOGI(TAG, "WDT Reset: Registering message handlers...");
    if (!Messaging::MessageHandlerRegistry::RegisterAllHandlers()) {
        ESP_LOGE(TAG, "Failed to register message handlers");
        return false;
    }
    esp_task_wdt_reset();

    // Initialize audio status manager (requires MessageBus and handlers to be initialized)
    ESP_LOGI(TAG, "WDT Reset: Initializing Audio Status Manager...");
    if (!Application::Audio::AudioController::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioController");
        return false;
    }
    esp_task_wdt_reset();

#if OTA_ENABLE_UPDATES
    // Initialize standard OTA
    ESP_LOGI(TAG, "WDT Reset: Initializing OTA Manager...");
    if (!Hardware::OTA::init()) {
        ESP_LOGE(TAG, "Failed to initialize OTA manager");
        return false;
    }
    ESP_LOGI(TAG, "OTA manager initialized successfully");
    esp_task_wdt_reset();
#endif

    // Setup UI components
    ESP_LOGI(TAG, "WDT Reset: Setting up UI components...");
    setupUiComponents();
    esp_task_wdt_reset();

    // Initialize the multi-threaded task manager (includes LVGL Message Handler)
    ESP_LOGI(TAG, "WDT Reset: Initializing Task Manager...");
    if (!TaskManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize task manager");
        return false;
    }
    esp_task_wdt_reset();

    // Send initial status request to get current audio information
    ESP_LOGI(TAG, "WDT Reset: Sending initial status request...");
    Application::Audio::AudioController::getInstance().publishAudioStatusRequest();
    esp_task_wdt_reset();

    ESP_LOGI(TAG,
             "Application Controller initialized successfully "
             "(Multi-threaded ESP32-S3)");

    // De-initialize watchdog timer after successful startup
    ESP_LOGI(TAG, "De-initializing startup watchdog timer.");
    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();

    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Application Controller");

    // Deinitialize task manager first (this stops all tasks)
    TaskManager::deinit();

    Application::Audio::AudioController::getInstance().deinit();

#if OTA_ENABLE_UPDATES
    Hardware::OTA::deinit();
#endif

    // Unregister all message handlers
    Messaging::MessageHandlerRegistry::UnregisterAllHandlers();

    // Deinitialize messaging system
    Messaging::MessageBus::Deinit();

    // Deinitialize network manager if it was initialized
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || \
    OTA_ENABLE_UPDATES
    Hardware::Network::deinit();
#endif

    Display::deinit();
    Hardware::Device::deinit();
}

void run(void) {
    // In the new multithreaded architecture, the main loop is much simpler
    // All heavy processing is handled by dedicated tasks

    // Longer delay to reduce main loop overhead - tasks handle everything
    vTaskDelay(pdMS_TO_TICKS(100));

    // Optional: Print task statistics periodically for debugging
    static unsigned long lastStatsTime = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastStatsTime >=
        60000) {  // Every 60 seconds (less frequent)
        TaskManager::printTaskStats();

        // Print stack usage for monitoring
        ESP_LOGI(TAG, "LVGL Task Stack High Water Mark: %d bytes",
                 TaskManager::getLvglTaskHighWaterMark() * sizeof(StackType_t));
        ESP_LOGI(TAG, "Network Task Stack High Water Mark: %d bytes",
                 TaskManager::getNetworkTaskHighWaterMark() * sizeof(StackType_t));

        lastStatsTime = currentTime;
    }
}

void setupUiComponents(void) {
    // Set display to 180 degrees rotation
    Display::setRotation(Display::ROTATION_0);

    // Register button click event handler
    lv_obj_add_event_cb(ui_btnRequestData,
                        Events::UI::btnRequestDataClickedHandler,
                        LV_EVENT_CLICKED, NULL);

    // Register audio device dropdown event handlers
    lv_obj_add_event_cb(ui_selectAudioDevice,
                        Events::UI::audioDeviceDropdownChangedHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice1,
                        Events::UI::audioDeviceDropdownChangedHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice2,
                        Events::UI::audioDeviceDropdownChangedHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Register volume arc event handlers for each tab
    // Visual feedback during dragging (VALUE_CHANGED)
    lv_obj_add_event_cb(ui_primaryVolumeSlider,
                        Events::UI::volumeArcVisualHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_singleVolumeSlider,
                        Events::UI::volumeArcVisualHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_balanceVolumeSlider,
                        Events::UI::volumeArcVisualHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Actual volume changes on release (RELEASED)
    lv_obj_add_event_cb(ui_primaryVolumeSlider,
                        Events::UI::volumeArcChangedHandler,
                        LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ui_singleVolumeSlider,
                        Events::UI::volumeArcChangedHandler,
                        LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ui_balanceVolumeSlider,
                        Events::UI::volumeArcChangedHandler,
                        LV_EVENT_RELEASED, NULL);

    // Register tab switch event handler
    ESP_LOGI(TAG,
             "Registering tab switch event handler for ui_tabsModeSwitch: %p",
             ui_tabsModeSwitch);
    lv_obj_add_event_cb(ui_tabsModeSwitch, Events::UI::tabSwitchHandler,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // Alternative approach: Register on individual tab buttons (for newer LVGL
    // versions)
    lv_obj_t *tab_buttons = lv_tabview_get_tab_bar(ui_tabsModeSwitch);
    if (tab_buttons) {
        ESP_LOGI(TAG, "Registering tab button events on tab bar: %p", tab_buttons);

        // Get the number of tabs and register event on each button
        uint32_t tab_count = lv_obj_get_child_count(tab_buttons);
        ESP_LOGI(TAG, "Found %d tab buttons", tab_count);

        for (uint32_t i = 0; i < tab_count; i++) {
            lv_obj_t *tab_button = lv_obj_get_child(tab_buttons, i);
            if (tab_button) {
                ESP_LOGI(TAG, "Registering event on tab button %d: %p", i, tab_button);
                lv_obj_add_event_cb(tab_button, Events::UI::tabSwitchHandler,
                                    LV_EVENT_CLICKED, NULL);
            }
        }
    }

    // Initialize current tab state by reading the actual active tab from the UI
    uint32_t activeTabIndex = lv_tabview_get_tab_active(ui_tabsModeSwitch);
    Events::UI::setCurrentTab(static_cast<Events::UI::TabState>(activeTabIndex));
    ESP_LOGI(TAG, "Initialized tab state to index: %d (%s)", activeTabIndex,
             Events::UI::getTabName(Events::UI::getCurrentTab()));

    // Setup OTA UI elements if available
#if OTA_ENABLE_UPDATES
    // Use message handler for thread-safe UI initialization
    Application::LVGLMessageHandler::updateOTAProgress(0, false, false,
                                                       "OTA Ready");
#endif
}

}  // namespace Application