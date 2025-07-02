#include "AppController.h"
#include "../../include/ManagerMacros.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/MessageAPI.h"
#include "../messaging/transport/SerialEngine.h"
#include "AudioManager.h"
#include "AudioUI.h"
#include "../logo/LogoManager.h"
#include "MessageBusLogoSupplier.h"
#include "LVGLMessageHandler.h"
#include "TaskManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <ui/ui.h>

// Unified OTA System
#include "../hardware/OTAManager.h"

// Private variables
static const char *TAG = "AppController";

namespace Application {

// Refactored init function using the macros
bool init(void) {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "AppController::init() called!");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Initializing Application Controller (Multi-threaded ESP32-S3)");
    ESP_LOGI(TAG, "Build Info: %s", getBuildInfo());
    ESP_LOGE(TAG, "[DEBUG] AppController init started - using ERROR level for visibility");

    // Initialize watchdog timer for startup debugging (15 seconds)
    ESP_LOGI(TAG, "Initializing startup watchdog timer...");
    esp_task_wdt_init(15, true);
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    // Critical initialization steps
    INIT_STEP_CRITICAL("Initializing Device Manager", Hardware::Device::init());

    // Optional initialization steps
    INIT_STEP_OPTIONAL("Initializing SD Manager",
                       "SD Manager initialized successfully",
                       "SD Manager initialization failed - SD card functionality will be unavailable",
                       Hardware::SD::init());

    INIT_STEP_OPTIONAL("Initializing Logo Manager",
                       "Logo Manager initialized successfully",
                       "Logo Manager initialization failed - logo functionality will be limited",
                       Logo::LogoManager::getInstance().init());

    INIT_STEP_CRITICAL("Initializing Display Manager", Display::init());

    // Conditional SD filesystem initialization
    INIT_STEP("Checking SD filesystem", {
        if (Hardware::SD::isMounted()) {
            ESP_LOGI(TAG, "Initializing LVGL SD filesystem...");
            if (!Hardware::SD::initLVGLFilesystem()) {
                ESP_LOGW(TAG, "Failed to initialize LVGL SD filesystem - SD file access from UI will be unavailable");
            } else {
                ESP_LOGI(TAG, "LVGL SD filesystem initialized successfully");
            }
        }
    });

    INIT_STEP_CRITICAL("Initializing Message System", Messaging::MessageAPI::init());

    // Network-Free Architecture - OTAManager handles network on-demand
    INIT_STEP("Configuring Network-Free Architecture", {
        ESP_LOGI(TAG, "[NETWORK-FREE] Network-free architecture enabled");
        ESP_LOGI(TAG, "[NETWORK-FREE] OTAManager will handle network communications on-demand");

#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2
        // MQTT completely removed - pure network-free architecture
        ESP_LOGW(TAG, "[NETWORK-FREE] Using Serial-only transport via InterruptMessagingEngine");
#endif

        ESP_LOGI(TAG, "[NETWORK-FREE] Network will be activated only during OTA operations");
    });

    // Transport configuration
    INIT_STEP("Configuring Message Transport", {
#if MESSAGING_DEFAULT_TRANSPORT == 1
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
        ESP_LOGI(TAG, "Initializing Core 1 Interrupt Messaging Engine");

        // Initialize MessageCore first
        if (!Messaging::MessageAPI::init()) {
            ESP_LOGE(TAG, "Failed to initialize MessageCore");
            return false;
        }

        // Initialize Core 1 Interrupt Messaging Engine
        if (!Messaging::Core1::InterruptMessagingEngine::init()) {
            ESP_LOGE(TAG, "Failed to initialize Core 1 Messaging Engine");
            return false;
        }

        ESP_LOGI(TAG, "Core 1 Messaging Engine initialized successfully");
#else
        ESP_LOGE(TAG, "Serial transport requested but disabled in config");
        return false;
#endif
#else
        ESP_LOGE(TAG, "Only Serial transport supported in network-free mode");
        return false;
#endif
    });

    ESP_LOGI(TAG, "WDT Reset: Message handlers will be registered by components...");
    esp_task_wdt_reset();

    // MessageBusLogoSupplier initialization
    INIT_STEP("Initializing MessageBusLogoSupplier", {
        Application::LogoAssets::MessageBusLogoSupplier &messageBusSupplier =
            Application::LogoAssets::MessageBusLogoSupplier::getInstance();
        messageBusSupplier.setRequestTimeout(30000);
        messageBusSupplier.setMaxConcurrentRequests(1);

        if (messageBusSupplier.init()) {
            ESP_LOGI(TAG, "MessageBusLogoSupplier initialized successfully");
        } else {
            ESP_LOGW(TAG, "MessageBusLogoSupplier initialization failed - automatic logo requests will be disabled");
        }
    });

    // Audio system initialization
    INIT_STEP_CRITICAL("Initializing Audio System",
                       Application::Audio::AudioManager::getInstance().init() &&
                           Application::Audio::AudioUI::getInstance().init());

#if OTA_ENABLE_UPDATES
    INIT_STEP_CRITICAL("Initializing Unified OTA Manager (Network-Free)",
                       Hardware::OTA::OTAManager::init());
    ESP_LOGI(TAG, "Unified OTA Manager initialized successfully - network-free mode active");
#endif

    INIT_STEP("Setting up UI components", { setupUiComponents(); });

    // Task Manager initialization - Network-free mode for maximum performance
    INIT_STEP_CRITICAL("Starting Task Manager", Application::TaskManager::init());

    // Start Core 1 Messaging Engine after TaskManager
    INIT_STEP_CRITICAL("Starting Core 1 Messaging Engine",
                       Messaging::Core1::InterruptMessagingEngine::start());

    // Post-initialization debug test
    ESP_LOGI(TAG, "AppController initialization complete - testing debug UI log");

    // Test the new DEBUG_UI_LOG functionality
    if (Messaging::MessageAPI::publishDebugUILog("AppController initialization complete")) {
        ESP_LOGI(TAG, "DEBUG_UI_LOG test message sent successfully");
    } else {
        ESP_LOGW(TAG, "Failed to send DEBUG_UI_LOG test message");
    }

    // Send initial status request
    INIT_STEP("Sending initial status request", {
        Application::Audio::AudioManager &audioManager = Application::Audio::AudioManager::getInstance();
        audioManager.publishStatusRequest();
    });

    ESP_LOGI(TAG, "Application Controller initialized successfully (Multi-threaded ESP32-S3)");

    // Update build time display
    INIT_STEP("Updating build time display", {
        Application::LVGLMessageHandler::updateBuildTimeDisplay();
    });

    // Cleanup watchdog timer
    ESP_LOGI(TAG, "De-initializing startup watchdog timer.");
    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();

    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Application Controller");

    // Stop Core 1 Messaging Engine first
    Messaging::Core1::InterruptMessagingEngine::stop();

    // Deinitialize task manager first (this stops all tasks)
    TaskManager::deinit();

    Application::Audio::AudioUI::getInstance().deinit();
    Application::Audio::AudioManager::getInstance().deinit();

    // Deinitialize MessageBusLogoSupplier
    Application::LogoAssets::MessageBusLogoSupplier::getInstance().deinit();

    // Deinitialize Logo Manager
    Logo::LogoManager::getInstance().deinit();

#if OTA_ENABLE_UPDATES
    Hardware::OTA::OTAManager::deinit();
#endif

    // Shutdown messaging system (handlers will clean up automatically)
    Messaging::MessageAPI::shutdown();

    // Network communications handled by OTAManager - no separate deinit needed

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
        ESP_LOGI(TAG, "Audio Task Stack High Water Mark: %d bytes",
                 TaskManager::getAudioTaskHighWaterMark() * sizeof(StackType_t));

        lastStatsTime = currentTime;
    }
}

void setupUiComponents(void) {
    Display::setRotation(Display::ROTATION_0);

    // =========================================================================
    // CORE UI EVENT REGISTRATION - USING COMPREHENSIVE MACROS
    // =========================================================================

    // Settings button - ensures clickable state
    SETUP_CLICK_EVENT(ui_btnGOTOSettings, Events::UI::openSettings, "Settings button");

    SETUP_CLICK_EVENT(ui_btnRequestStatus, Events::UI::btnRequestDataClickedHandler, "Send Status Request");

    // All audio dropdowns at once
    SETUP_ALL_AUDIO_DROPDOWNS(Events::UI::audioDeviceDropdownChangedHandler);

    // All volume sliders with both visual and change handlers
    SETUP_ALL_VOLUME_SLIDERS(Events::UI::volumeArcVisualHandler, Events::UI::volumeArcChangedHandler);

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
#if OTA_ENABLE_UPDATES
    Application::LVGLMessageHandler::updateOTAProgress(0, false, false, "OTA Ready (Network-Free Mode)");
    ESP_LOGI(TAG, "[NETWORK-FREE] OTA UI configured for on-demand operation");
#endif

    // =========================================================================
    // FILE EXPLORER NAVIGATION SETUP
    // =========================================================================
    SETUP_FILE_EXPLORER_NAVIGATION();
}

}  // namespace Application
