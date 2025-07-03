#include "MultithreadedOTAApplication.h"
#include "MultithreadedOTA.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../core/TaskManager.h"
#include "BootManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "MultiOTAApp";

namespace OTA {

// Application state
static bool s_applicationRunning = false;
bool MultithreadedOTAApplication::s_initialized = false;

bool MultithreadedOTAApplication::init() {
    ESP_LOGI(TAG, "Initializing Multithreaded OTA Application");

    // Initialize device manager for basic hardware
    if (!Hardware::Device::init()) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
        return false;
    }

    // Initialize display for progress feedback
    if (!Display::init()) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return false;
    }

    // Initialize LVGL message handler for UI updates
    if (!Application::LVGLMessageHandler::init()) {
        ESP_LOGE(TAG, "Failed to initialize LVGL message handler");
        return false;
    }

    // Initialize TaskManager for LVGL synchronization
    if (!Application::TaskManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize TaskManager");
        return false;
    }

    // Show enhanced OTA screen immediately
    ESP_LOGI(TAG, "Showing OTA interface...");
    Application::LVGLMessageHandler::showOtaScreen();
    
    // Give UI time to render
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize the multithreaded OTA system
    if (!MultiOTA::init()) {
        ESP_LOGE(TAG, "Failed to initialize multithreaded OTA system");
        return false;
    }

    s_applicationRunning = true;
    s_initialized = true;

    ESP_LOGI(TAG, "Multithreaded OTA Application initialized successfully");

    // Start OTA process immediately
    ESP_LOGI(TAG, "Starting multithreaded OTA process...");
    if (!MultiOTA::startOTA()) {
        ESP_LOGE(TAG, "Failed to start OTA process");
        return false;
    }

    return true;
}

void MultithreadedOTAApplication::run() {
    if (!s_applicationRunning) {
        return;
    }

    // The multithreaded OTA system handles everything in background tasks
    // This main loop just needs to handle application-level state management
    
    // Check if OTA process is complete
    MultiOTA::DetailedProgress_t progress = MultiOTA::getProgress();
    
    switch (progress.state) {
        case MultiOTA::OTA_STATE_SUCCESS:
            ESP_LOGI(TAG, "OTA completed successfully - preparing to restart");
            // Clear the OTA request flag
            Boot::BootManager::clearBootRequest();
            // Small delay for logging
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Restart (will boot into normal mode)
            esp_restart();
            break;
            
        case MultiOTA::OTA_STATE_FAILED:
            ESP_LOGW(TAG, "OTA failed - waiting for user action");
            // The multithreaded system handles retry/exit buttons
            // Just keep running until user decides
            break;
            
        case MultiOTA::OTA_STATE_CANCELLED:
            ESP_LOGW(TAG, "OTA cancelled - returning to normal mode");
            // Clear the OTA request flag
            Boot::BootManager::clearBootRequest();
            // Request normal mode and restart
            Boot::BootManager::requestNormalMode();
            // Small delay for logging
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            break;
            
        case MultiOTA::OTA_STATE_CLEANUP:
            s_applicationRunning = false;
            break;
            
        default:
            // Continue running - OTA in progress
            break;
    }

    // Feed main thread watchdog
    esp_task_wdt_reset();

    // Longer delay since all work is done in background tasks
    vTaskDelay(pdMS_TO_TICKS(250));
}

void MultithreadedOTAApplication::cleanup() {
    ESP_LOGI(TAG, "Cleaning up Multithreaded OTA Application");

    s_applicationRunning = false;
    s_initialized = false;

    // Deinitialize in reverse order
    MultiOTA::deinit();
    Application::TaskManager::deinit();
    Application::LVGLMessageHandler::deinit();
    Display::deinit();
    Hardware::Device::deinit();

    ESP_LOGI(TAG, "Multithreaded OTA Application cleaned up");
}

bool MultithreadedOTAApplication::isInitialized() {
    return s_initialized;
}

bool MultithreadedOTAApplication::isRunning() {
    return s_applicationRunning;
}

MultiOTA::DetailedProgress_t MultithreadedOTAApplication::getProgress() {
    return MultiOTA::getProgress();
}

MultiOTA::OTAStats_t MultithreadedOTAApplication::getStats() {
    return MultiOTA::getStats();
}

bool MultithreadedOTAApplication::cancelOTA() {
    return MultiOTA::cancelOTA();
}

bool MultithreadedOTAApplication::retryOTA() {
    return MultiOTA::retryOTA();
}

void MultithreadedOTAApplication::exitOTA() {
    MultiOTA::exitOTA();
}

}  // namespace OTA