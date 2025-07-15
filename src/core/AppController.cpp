#include "AppController.h"
#include "../application/audio/AudioManager.h"
#include "../application/audio/AudioUI.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/SDManager.h"
#include "../logo/SimpleLogoManager.h"
#include "../messaging/Message.h"
#include "../messaging/MessagingInit.h"
#include "BSODHandler.h"        // NEW: BSOD system
#include "BootProgressScreen.h" // NEW: Boot progress
#include "BuildInfo.h"
#include "ManagerMacros.h"
#include "MessagingConfig.h"
#include "TaskManager.h"
#include "UIPerformanceOptimizations.h" // EMERGENCY PERFORMANCE FIX
#include "UiEventHandlers.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <ui/ui.h>


// Private variables
static const char *TAG = "AppController";

namespace Application {

// Refactored init function using the macros
bool init(void) {
  ESP_LOGI(TAG, "==========================================");
  ESP_LOGI(TAG, "AppController::init() called!");
  ESP_LOGI(TAG, "==========================================");
  ESP_LOGI(TAG,
           "Initializing Application Controller (Multi-threaded ESP32-S3)");
  ESP_LOGI(TAG, "Build Info: %s", getBuildInfo());
  ESP_LOGE(
      TAG,
      "[DEBUG] AppController init started - using ERROR level for visibility");

  // Initialize watchdog timer for startup debugging (15 seconds)
  ESP_LOGI(TAG, "Initializing startup watchdog timer...");
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  // Critical initialization steps
  // First, initialize minimal display for BSOD capability
  BOOT_STATUS("Initializing hardware...");
  BootProgress::updateProgress(5);
  INIT_CRITICAL(
      Hardware::Device::init(),
      "Failed to initialize device manager. Hardware initialization failed.");

  // Initialize BSOD handler early
  INIT_CRITICAL(
      BSODHandler::init(),
      "Failed to initialize BSOD handler. Critical error system unavailable.");

  // Initialize display and show boot progress
  BOOT_STATUS("Initializing display...");
  BootProgress::updateProgress(10);
  INIT_CRITICAL(Display::init(),
                "Display initialization failed. Cannot show user interface.");

  // Now show boot progress screen
  INIT_CRITICAL(BootProgress::init(),
                "Failed to initialize boot progress screen.");

  // Optional initialization steps
  BOOT_STATUS("Checking SD card...");
  BootProgress::updateProgress(20);
  INIT_OPTIONAL(Hardware::SD::init(), "SD Manager");

  BOOT_STATUS("Loading logo...");
  BootProgress::updateProgress(30);
  INIT_CRITICAL(SimpleLogoManager::getInstance().init(),
                "Failed to initialize logo manager.");

  // Conditional SD filesystem initialization
  INIT_STEP("Checking SD filesystem", {
    if (Hardware::SD::isMounted()) {
      ESP_LOGI(TAG, "Initializing LVGL SD filesystem...");
      if (!Hardware::SD::initLVGLFilesystem()) {
        ESP_LOGW(TAG, "Failed to initialize LVGL SD filesystem - SD file "
                      "access from UI will be unavailable");
      } else {
        ESP_LOGI(TAG, "LVGL SD filesystem initialized successfully");
      }
    }
  });

  BOOT_STATUS("Initializing messaging system...");
  BootProgress::updateProgress(40);
  INIT_CRITICAL(
      Messaging::initMessaging(),
      "Failed to initialize messaging system. Communication unavailable.");

  // Network-Free Architecture
  BOOT_STATUS("Configuring network-free architecture...");
  BootProgress::updateProgress(50);
  INIT_STEP("Configuring Network-Free Architecture", {
    ESP_LOGI(TAG, "[NETWORK-FREE] Network-free architecture enabled");

#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2
    // MQTT completely removed - pure network-free architecture
    ESP_LOGW(TAG, "[NETWORK-FREE] Using Serial-only transport via "
                  "InterruptMessagingEngine");
#endif

    ESP_LOGI(
        TAG,
        "[NETWORK-FREE] Network will be activated only during OTA operations");
  });

  // Transport configuration
  INIT_STEP("Configuring Message Transport", {
#if MESSAGING_DEFAULT_TRANSPORT == 1
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Initializing Core 1 Interrupt Messaging Engine");

    // Messaging already initialized above
    ESP_LOGI(TAG, "Using BRUTAL messaging system - no abstractions");
#else
        ESP_LOGE(TAG, "Serial transport requested but disabled in config");
        CRITICAL_FAILURE("Serial transport requested but disabled in configuration");
#endif
#else
        ESP_LOGE(TAG, "Only Serial transport supported in network-free mode");
        CRITICAL_FAILURE("Only Serial transport is supported in network-free mode");
#endif
  });

  ESP_LOGI(TAG,
           "WDT Reset: Message handlers will be registered by components...");
  esp_task_wdt_reset();

  // Brutal logo system is already initialized above - no complex setup needed

  // Audio system initialization
  BOOT_STATUS("Initializing audio system...");
  BootProgress::updateProgress(60);
  INIT_CRITICAL(
      Application::Audio::AudioManager::getInstance().init() &&
          Application::Audio::AudioUI::getInstance().init(),
      "Failed to initialize audio system. Audio functionality unavailable.");

  BOOT_STATUS("Setting up UI components...");
  BootProgress::updateProgress(70);
  INIT_STEP("Setting up UI components", { setupUiComponents(); });

  // EMERGENCY PERFORMANCE FIX: Apply critical UI optimizations
  BOOT_STATUS("Optimizing performance...");
  BootProgress::updateProgress(80);
  INIT_STEP("Applying emergency UI performance optimizations", {
    ui_performance_apply_all_optimizations();
    ESP_LOGI(TAG, "Emergency performance optimizations applied - expect 80-90% "
                  "processing time reduction");
  });

  // Task Manager initialization - Network-free mode for maximum performance
  BOOT_STATUS("Starting task manager...");
  BootProgress::updateProgress(90);
  INIT_CRITICAL(Application::TaskManager::init(),
                "Failed to start task manager. System cannot run tasks.");

  // Messaging already running - no need to start anything

  // Post-initialization debug test
  ESP_LOGI(TAG, "AppController initialization complete");

  // Send initial status request
  INIT_STEP("Sending initial status request", {
    Application::Audio::AudioManager &audioManager =
        Application::Audio::AudioManager::getInstance();
    audioManager.publishStatusRequest();
  });

  ESP_LOGI(TAG, "Application Controller initialized successfully "
                "(Multi-threaded ESP32-S3)");

  // Update build time display
  INIT_STEP("Updating build time display",
            { Application::LVGLMessageHandler::updateBuildTimeDisplay(); });

  // Hide boot progress screen
  BOOT_STATUS("System ready!");
  BootProgress::updateProgress(100);
  vTaskDelay(pdMS_TO_TICKS(500)); // Brief moment to show 100%
  BootProgress::hide();

  // Cleanup watchdog timer
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

  // Deinitialize Brutal Logo Manager
  SimpleLogoManager::getInstance().deinit();

  // Shutdown messaging system
  Messaging::shutdownMessaging();

  Display::deinit();
  Hardware::SD::deinit();
  Hardware::Device::deinit();
}

void run(void) {
  // In the new multithreaded architecture, the main loop is much simpler
  // All heavy processing is handled by dedicated tasks

  // Longer delay to reduce main loop overhead - tasks handle everything
  vTaskDelay(pdMS_TO_TICKS(100));

  // // Optional: Print task statistics periodically for debugging
  // static unsigned long lastStatsTime = 0;
  // unsigned long currentTime = millis();
  // if (currentTime - lastStatsTime >=
  //     60000) {  // Every 60 seconds (less frequent)
  //     TaskManager::printTaskStats();

  //     // Print stack usage for monitoring
  //     ESP_LOGI(TAG, "LVGL Task Stack High Water Mark: %d bytes",
  //              TaskManager::getLvglTaskHighWaterMark() *
  //              sizeof(StackType_t));
  //     ESP_LOGI(TAG, "Audio Task Stack High Water Mark: %d bytes",
  //              TaskManager::getAudioTaskHighWaterMark() *
  //              sizeof(StackType_t));

  //     lastStatsTime = currentTime;
  // }
}

void setupUiComponents(void) {
  Display::setRotation(Display::ROTATION_0);

  // =========================================================================
  // CORE UI EVENT REGISTRATION - USING COMPREHENSIVE MACROS
  // =========================================================================

  // Settings button - ensures clickable state
  SETUP_CLICK_EVENT(ui_btnGOTOSettings, Events::UI::openSettings,
                    "Settings button");

  SETUP_CLICK_EVENT(ui_btnRequestStatus,
                    Events::UI::btnRequestDataClickedHandler,
                    "Send Status Request");

  // All audio dropdowns at once
  SETUP_ALL_AUDIO_DROPDOWNS(Events::UI::audioDeviceDropdownChangedHandler);

  // All volume sliders with both visual and change handlers
  SETUP_ALL_VOLUME_SLIDERS(Events::UI::volumeArcVisualHandler,
                           Events::UI::volumeArcChangedHandler);

  // Complete tab system setup (tabview + all individual buttons)
  SETUP_TAB_EVENTS(ui_tabsModeSwitch, Events::UI::tabSwitchHandler);

  // Initialize current tab state
  uint32_t activeTabIndex = lv_tabview_get_tab_active(ui_tabsModeSwitch);
  Events::UI::setCurrentTab(static_cast<Events::UI::TabState>(activeTabIndex));
  ESP_LOGI(TAG, "Initialized tab state to index: %d (%s)", activeTabIndex,
           Events::UI::getTabName(Events::UI::getCurrentTab()));

  // =========================================================================
  // NETWORK-FREE ARCHITECTURE: OTA UI SETUP
  // =========================================================================
  // OTA system will be replaced with SimpleOTA implementation

  // =========================================================================
  // FILE EXPLORER NAVIGATION SETUP
  // =========================================================================
  SETUP_FILE_EXPLORER_NAVIGATION();
}

} // namespace Application
