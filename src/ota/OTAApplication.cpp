#include "OTAApplication.h"
#include "../hardware/OTAManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
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

    // Initialize OTA manager
    if (!Hardware::OTA::OTAManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize OTA manager");
        return false;
    }

    applicationRunning = true;
    initialized = true;
    ESP_LOGI(TAG, "OTA Application initialized successfully");
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

    // Feed watchdog
    esp_task_wdt_reset();

    // Check if we should exit
    if (!Hardware::OTA::OTAManager::isActive() &&
        Hardware::OTA::OTAManager::getCurrentState() == Hardware::OTA::OTA_IDLE) {
        // OTA completed or failed, exit application
        applicationRunning = false;
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
    Display::deinit();
    Hardware::Device::deinit();

    ESP_LOGI(TAG, "OTA Application cleaned up");
}

bool startOTA() {
    ESP_LOGI(TAG, "Starting OTA process");

    if (!applicationRunning) {
        ESP_LOGE(TAG, "Cannot start OTA - application not initialized");
        return false;
    }

    // OTAManager handles network initialization internally
    return Hardware::OTA::OTAManager::startOTA();
}

}  // namespace OTA
