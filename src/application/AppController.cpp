#include "AppController.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/MessageAPI.h"
#include "../messaging/SerialBridge.h"
#include "AudioManager.h"
#include "AudioUI.h"
#include "LogoManager.h"
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

    // Initialize SD card manager
    ESP_LOGI(TAG, "WDT Reset: Initializing SD Manager...");
    if (!Hardware::SD::init()) {
        ESP_LOGW(TAG, "SD Manager initialization failed - SD card functionality will be unavailable");
        // Note: SD card failure is not fatal for the application
    } else {
        ESP_LOGI(TAG, "SD Manager initialized successfully");
    }
    esp_task_wdt_reset();

    // Initialize Logo Manager (depends on SD Manager)
    ESP_LOGI(TAG, "WDT Reset: Initializing Logo Manager...");
    if (!Application::LogoAssets::LogoManager::getInstance().init()) {
        ESP_LOGW(TAG, "Logo Manager initialization failed - logo functionality will be limited");
        // Note: Logo Manager failure is not fatal for the application
    } else {
        ESP_LOGI(TAG, "Logo Manager initialized successfully with fuzzy matching support");
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
    ESP_LOGI(TAG, "WDT Reset: Initializing Message System...");
    if (!Messaging::MessageAPI::init()) {
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
    // MQTT transport registration will be handled by MqttManager
    // when it connects - just note that we expect it
#else
    ESP_LOGE(TAG, "MQTT transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 1
    // Serial only - no additional network configuration needed
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring Serial transport (config: Serial only)");
    if (!Messaging::Serial::init()) {
        ESP_LOGE(TAG, "Failed to initialize Serial bridge");
        return false;
    }
#else
    ESP_LOGE(TAG, "Serial transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 2
    // Both transports
#if MESSAGING_ENABLE_MQTT_TRANSPORT && MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring dual transport (config: MQTT + Serial)");
    if (!Messaging::Serial::init()) {
        ESP_LOGE(TAG, "Failed to initialize Serial bridge");
        return false;
    }
    // MQTT transport will self-register when it connects
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

    // Note: Message handlers will be registered by individual components
    // during their initialization (AudioManager, etc.)
    ESP_LOGI(TAG, "WDT Reset: Message handlers will be registered by components...");
    esp_task_wdt_reset();

    // Initialize audio system (requires MessageBus and handlers to be initialized)
    ESP_LOGI(TAG, "WDT Reset: Initializing Audio System...");
    if (!Application::Audio::AudioManager::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioManager");
        return false;
    }
    if (!Application::Audio::AudioUI::getInstance().init()) {
        ESP_LOGE(TAG, "Failed to initialize AudioUI");
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
    Application::Audio::AudioManager &audioManager = Application::Audio::AudioManager::getInstance();
    audioManager.publishStatusRequest();
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

    Application::Audio::AudioUI::getInstance().deinit();
    Application::Audio::AudioManager::getInstance().deinit();

    // Deinitialize Logo Manager
    Application::LogoAssets::LogoManager::getInstance().deinit();

#if OTA_ENABLE_UPDATES
    Hardware::OTA::deinit();
#endif

    // Shutdown transports
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
    Messaging::Serial::deinit();
#endif

    // Shutdown messaging system (handlers will clean up automatically)
    Messaging::MessageAPI::shutdown();

    // Deinitialize network manager if it was initialized
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || \
    OTA_ENABLE_UPDATES
    Hardware::Network::deinit();
#endif

    Display::deinit();
    Hardware::SD::deinit();
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
                        Events::UI::stateOverviewLongPressHandler,
                        LV_EVENT_CLICKED, NULL);
    // lv_obj_add_event_cb(ui_btnRequestData,
    //                         Events::UI::btnRequestDataClickedHandler,
    //                         LV_EVENT_CLICKED, NULL);

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

    // Add long-press handler to accessible UI elements for state overview
    ESP_LOGI(TAG, "Registering long-press handler for state overview on tabview: %p", ui_tabsModeSwitch);
    lv_obj_add_event_cb(ui_tabsModeSwitch, Events::UI::stateOverviewLongPressHandler,
                        LV_EVENT_LONG_PRESSED, NULL);

    // Also add to network panel for easy access
    if (ui_pnlNetwork) {
        ESP_LOGI(TAG, "Registering long-press handler for state overview on network panel: %p", ui_pnlNetwork);
        lv_obj_add_event_cb(ui_pnlNetwork, Events::UI::stateOverviewLongPressHandler,
                            LV_EVENT_LONG_PRESSED, NULL);
    }

    // Add to individual tab content areas for maximum accessibility
    if (ui_Master) {
        lv_obj_add_event_cb(ui_Master, Events::UI::stateOverviewLongPressHandler,
                            LV_EVENT_LONG_PRESSED, NULL);
    }
    if (ui_Single) {
        lv_obj_add_event_cb(ui_Single, Events::UI::stateOverviewLongPressHandler,
                            LV_EVENT_LONG_PRESSED, NULL);
    }
    if (ui_Balance) {
        lv_obj_add_event_cb(ui_Balance, Events::UI::stateOverviewLongPressHandler,
                            LV_EVENT_LONG_PRESSED, NULL);
    }

    // Setup OTA UI elements if available
#if OTA_ENABLE_UPDATES
    // Use message handler for thread-safe UI initialization
    Application::LVGLMessageHandler::updateOTAProgress(0, false, false,
                                                       "OTA Ready");
#endif

    // Setup file explorer navigation
    ESP_LOGI(TAG, "Setting up file explorer event handlers");

    // SD container click handler for file explorer navigation
    if (ui_sdContainer) {
        lv_obj_add_event_cb(ui_sdContainer, Events::UI::fileExplorerNavigationHandler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_flag(ui_sdContainer, LV_OBJ_FLAG_CLICKABLE);
        ESP_LOGI(TAG, "SD container click handler registered");
    }

    // File explorer back button handler (if screen exists)
    if (ui_btnFileExplorerBack) {
        lv_obj_add_event_cb(ui_btnFileExplorerBack, Events::UI::fileExplorerBackButtonHandler, LV_EVENT_CLICKED, NULL);
        ESP_LOGI(TAG, "File explorer back button handler registered");
    }
}

}  // namespace Application
