#include "BootManager.h"
#include "CoreLoggingFilter.h"
#include "core/AppController.h"
#include "hardware/DeviceManager.h"
#include "BSODHandler.h"
#include <Arduino.h>

/*
 * ESP32-S3 UniMixer Client - Application Architecture
 *
 * NORMAL MODE Architecture:
 * - Core 0: LVGL/UI, Messaging, Audio processing (high priority)
 * - Core 1: Dedicated messaging engine with interrupt-driven I/O
 * - No network tasks = Maximum UI/audio performance
 *
 * Core 1 Messaging Engine:
 * - Interrupt-driven Serial I/O
 * - Zero busy-waiting
 * - Dedicated message routing and processing
 */

void setup() {
  // Initialize Core 1-only logging filter FIRST to prevent interference
  // This must be done before any ESP_LOG calls to ensure proper filtering

  // Initialize application
  CoreLoggingFilter::init();

  // Comment out to actually filter out core0
  // CoreLoggingFilter::disableFilter();

  log_i("=== STARTING UNIMIXER CLIENT ===");
  log_i("Starting full UniMixer Client application");

  if (!Application::init()) {
    log_e("Failed to initialize application");
    CRITICAL_FAILURE("Application initialization failed. Please check system configuration.");
  }

  log_i("ESP32-S3 UniMixer Client initialized successfully");
  log_i("Architecture: Network-free with dedicated messaging core");
  log_i("Core 0: UI/LVGL/Audio (high priority)");
  log_i("Core 1: Messaging engine (interrupt-driven)");

  // Print memory information
  log_i("Free heap: %d bytes", ESP.getFreeHeap());
  log_i("Free PSRAM: %d bytes", ESP.getFreePsram());
  log_i("PSRAM size: %d bytes", ESP.getPsramSize());
}

void loop() {
  // Application main loop
  Application::run();
  delay(1); // Minimal delay for task switching
}
