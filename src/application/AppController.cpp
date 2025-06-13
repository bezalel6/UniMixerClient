#include "AppController.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../messaging/MessageBus.h"
#include "AudioStatusManager.h"
#include "TaskManager.h"
#include <esp_log.h>
#include <ui/ui.h>

// Private variables
static const char *TAG = "AppController";

namespace Application {

bool init(void) {
  ESP_LOGI(TAG,
           "Initializing Application Controller (Multi-threaded ESP32-S3)");

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
  networkNeeded = true; // MQTT transport modes need network
#endif

#if OTA_ENABLE_UPDATES
  networkNeeded = true; // OTA always needs network
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

  // Initialize audio status manager (requires MessageBus to be initialized)
  if (!Application::Audio::StatusManager::init()) {
    ESP_LOGE(TAG, "Failed to initialize audio status manager");
    return false;
  }

#if OTA_ENABLE_UPDATES
  // Initialize standard OTA
  if (!Hardware::OTA::init()) {
    ESP_LOGE(TAG, "Failed to initialize OTA manager");
    return false;
  }
  ESP_LOGI(TAG, "OTA manager initialized successfully");
#endif

  // Setup UI components
  setupUiComponents();

  // Initialize the multi-threaded task manager (includes LVGL Message Handler)
  if (!TaskManager::init()) {
    ESP_LOGE(TAG, "Failed to initialize task manager");
    return false;
  }

  // Send initial status request to get current audio information
  ESP_LOGI(TAG, "Sending initial status request");
  Application::Audio::StatusManager::publishAudioStatusRequest();

  ESP_LOGI(TAG, "Application Controller initialized successfully "
                "(Multi-threaded ESP32-S3)");
  return true;
}

void deinit(void) {
  ESP_LOGI(TAG, "Deinitializing Application Controller");

  // Deinitialize task manager first (this stops all tasks)
  TaskManager::deinit();

  Application::Audio::StatusManager::deinit();

#if OTA_ENABLE_UPDATES
  Hardware::OTA::deinit();
#endif

  // Deinitialize messaging system
  Messaging::MessageBus::Deinit();

  // Deinitialize network manager if it was initialized
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 ||    \
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
      60000) { // Every 60 seconds (less frequent)
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
    lv_obj_add_event_cb(ui_primaryVolumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_singleVolumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_balanceVolumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);

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
  if (ui_barOTAUpdateProgress) {
    lv_bar_set_value(ui_barOTAUpdateProgress, 0, LV_ANIM_OFF);
  }
  if (ui_lblOTAUpdateProgress) {
    lv_label_set_text(ui_lblOTAUpdateProgress, "OTA Ready");
  }
#endif
}

} // namespace Application