#include "OTAApplication.h"
#include "../ota/OTAManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../core/TaskManager.h"
#include "BootManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "OTAApplication";

namespace OTA {

// Application state
static bool applicationRunning = false;
static unsigned long lastProgressUpdate = 0;
bool OTAApplication::initialized = false;

bool OTAApplication::init() {
    ESP_LOGI(TAG, "Initializing OTA Application");

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

    // Initialize TaskManager for OTA progress queue
    if (!Application::TaskManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize TaskManager (OTA progress queue)");
        return false;
    }

    // Show enhanced OTA screen immediately
    ESP_LOGI(TAG, "Showing OTA interface...");
    Application::LVGLMessageHandler::showOtaScreen();

    // Give UI time to render
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initialize OTA manager
    if (!Hardware::OTA::OTAManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize OTA manager");
        return false;
    }

    applicationRunning = true;
    initialized = true;

    ESP_LOGI(TAG, "OTA Application initialized successfully");

    // Start OTA process immediately
    ESP_LOGI(TAG, "Starting OTA process...");
    if (!Hardware::OTA::OTAManager::startOTA()) {
        ESP_LOGE(TAG, "Failed to start OTA process");
        return false;
    }

    return true;
}

void OTAApplication::run() {
    if (!applicationRunning) {
        return;
    }

    // Update OTA manager - handles all network communications internally
    Hardware::OTA::OTAManager::update();

    // Update display for user feedback
    Display::update();
    Display::tickUpdate();

    // Process LVGL message queue for UI updates
    Application::LVGLMessageHandler::processMessageQueue(nullptr);

    // Feed watchdog
    esp_task_wdt_reset();

    // Check if OTA process is complete
    if (!Hardware::OTA::OTAManager::isActive()) {
        Hardware::OTA::OTAState state = Hardware::OTA::OTAManager::getCurrentState();

        if (state == Hardware::OTA::OTA_SUCCESS) {
            ESP_LOGI(TAG, "OTA completed successfully - preparing to restart");
            // Clear the OTA request flag
            Boot::BootManager::clearBootRequest();
            // Small delay for logging
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Restart (will boot into normal mode)
            esp_restart();
        } else if (state == Hardware::OTA::OTA_FAILED || state == Hardware::OTA::OTA_CANCELLED) {
            ESP_LOGW(TAG, "OTA failed or cancelled - returning to normal mode");
            // Clear the OTA request flag
            Boot::BootManager::clearBootRequest();
            // Request normal mode and restart
            Boot::BootManager::requestNormalMode();
            // Small delay for logging
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        // If still idle, exit application
        if (state == Hardware::OTA::OTA_IDLE) {
            applicationRunning = false;
        }
    }

    // Simple delay
    vTaskDelay(pdMS_TO_TICKS(100));
}

void OTAApplication::cleanup() {
    ESP_LOGI(TAG, "Cleaning up OTA Application");

    applicationRunning = false;
    initialized = false;

    // Deinitialize in reverse order
    Hardware::OTA::OTAManager::deinit();
    Application::LVGLMessageHandler::deinit();
    Display::deinit();
    Hardware::Device::deinit();

    ESP_LOGI(TAG, "OTA Application cleaned up");
}

}  // namespace OTA
