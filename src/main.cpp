#include <Arduino.h>
#include "application/AppController.h"
#include "hardware/DeviceManager.h"
#include "../include/BootManager.h"
#include "../include/CoreLoggingFilter.h"
#include "ota/OTAApplication.h"

/*
 * ESP32-S3 UniMixer Client - Boot Mode Architecture
 *
 * BOOT MODE SEPARATION:
 * - NORMAL MODE: Full application with UI, messaging, audio
 * - OTA MODE: Minimal OTA-only application for firmware updates
 *
 * NORMAL MODE Architecture:
 * - Core 0: LVGL/UI, Messaging, Audio processing (high priority)
 * - Core 1: Dedicated messaging engine with interrupt-driven I/O
 * - No network tasks = Maximum UI/audio performance
 *
 * OTA MODE Architecture:
 * - Minimal display for progress
 * - Network + OTA only
 * - Returns to normal mode after completion
 *
 * Core 1 Messaging Engine:
 * - Interrupt-driven Serial I/O
 * - Zero busy-waiting
 * - Dedicated message routing and processing
 */

void setup() {
    // Initialize Core 1-only logging filter FIRST to prevent interference
    // This must be done before any ESP_LOG calls to ensure proper filtering
    CoreLoggingFilter::init();
    CoreLoggingFilter::disableFilter();

    // NOTE: UART/Serial initialization is now handled by InterruptMessagingEngine
    // to avoid driver conflicts

    // Initialize Boot Manager to determine mode
    if (!Boot::BootManager::init()) {
        log_e("Failed to initialize Boot Manager");
        ESP.restart();
    }

    // Branch based on boot mode
    Boot::BootMode bootMode = Boot::BootManager::getCurrentMode();

    switch (bootMode) {
        case Boot::BootMode::NORMAL:
            log_i("=== NORMAL BOOT MODE ===");
            log_i("Starting full UniMixer Client application");

            if (!Application::init()) {
                log_e("Failed to initialize normal application");
                ESP.restart();
            }

            log_i("ESP32-S3 UniMixer Client initialized successfully");
            log_i("Architecture: Network-free with dedicated messaging core");
            log_i("Core 0: UI/LVGL/Audio (high priority)");
            log_i("Core 1: Messaging engine (interrupt-driven)");
            break;

        case Boot::BootMode::OTA_UPDATE:
            log_i("=== OTA BOOT MODE ===");
            log_i("Starting dedicated OTA application");

            if (!OTA::OTAApplication::init()) {
                log_e("Failed to initialize OTA application");
                Boot::BootManager::requestNormalMode();
                ESP.restart();
            }

            log_i("OTA Application initialized successfully");
            log_i("Architecture: Minimal OTA-only mode");
            break;

        case Boot::BootMode::FACTORY:
        case Boot::BootMode::RECOVERY:
        default:
            log_w("Unsupported boot mode: %s", Boot::BootManager::getBootReasonString());
            log_i("Falling back to normal mode");
            Boot::BootManager::requestNormalMode();
            ESP.restart();
            break;
    }

    // Print memory information
    log_i("Free heap: %d bytes", ESP.getFreeHeap());
    log_i("Free PSRAM: %d bytes", ESP.getFreePsram());
    log_i("PSRAM size: %d bytes", ESP.getPsramSize());
}

void loop() {
    // Boot mode-specific main loop
    Boot::BootMode bootMode = Boot::BootManager::getCurrentMode();

    switch (bootMode) {
        case Boot::BootMode::NORMAL:
            // Normal application loop
            Application::run();
            delay(1);  // Minimal delay for task switching
            break;

        case Boot::BootMode::OTA_UPDATE:
            // OTA application loop
            OTA::OTAApplication::run();
            delay(10);  // Slightly longer delay for OTA operations
            break;

        default:
            // Should not reach here, but handle gracefully
            delay(100);
            break;
    }
}
