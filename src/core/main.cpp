#include "BootManager.h"
#include "CoreLoggingFilter.h"
#include "core/AppController.h"
#include "hardware/DeviceManager.h"
#include "SimpleOTA.h"
#include <Arduino.h>

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
    CoreLoggingFilter::init();
    // Comment out to actually filter out core0
    CoreLoggingFilter::disableFilter();
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
    log_i("=== SIMPLE OTA BOOT MODE ===");
    log_i("Starting SimpleOTA with dual-core architecture");

    // Initialize basic hardware for OTA
    if (!Hardware::Device::init()) {
      log_e("Failed to initialize device manager for OTA");
      Boot::BootManager::requestNormalMode();
      ESP.restart();
    }

    // Initialize SimpleOTA with default configuration
    if (!SimpleOTA::initWithDefaults()) {
      log_e("Failed to initialize SimpleOTA");
      Boot::BootManager::requestNormalMode();
      ESP.restart();
    }

    // Start OTA update process
    if (!SimpleOTA::startUpdate()) {
      log_e("Failed to start OTA update");
      Boot::BootManager::requestNormalMode();
      ESP.restart();
    }

    log_i("SimpleOTA initialized successfully");
    log_i("Architecture: Dual-core with smooth LVGL UI");
    log_i("Core 0: 60 FPS UI updates | Core 1: Network operations");
    break;

  case Boot::BootMode::FACTORY:
  case Boot::BootMode::RECOVERY:
  default:
    log_w("Unsupported boot mode: %s",
          Boot::BootManager::getBootReasonString());
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
    delay(1); // Minimal delay for task switching
    break;

  case Boot::BootMode::OTA_UPDATE:
    // SimpleOTA handles everything in background tasks
    // Just need to check if we should exit
    if (!SimpleOTA::isRunning()) {
      log_i("OTA completed - returning to normal mode");
      Boot::BootManager::requestNormalMode();
      ESP.restart();
    }
    delay(100);  // Light delay since everything runs in background
    break;

  default:
    // Should not reach here, but handle gracefully
    delay(100);
    break;
  }
}
