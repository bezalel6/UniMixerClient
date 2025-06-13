#include <Arduino.h>
#include "application/AppController.h"

/*
 * ESP32-S3 Optimized UniMixer Client
 * 
 * Architecture:
 * - Dual-core multitasking with FreeRTOS
 * - Core 0: LVGL/UI, Messaging, Audio processing (high priority)
 * - Core 1: Network operations, OTA updates (lower priority)
 * - PSRAM optimization for large display buffers
 * - Real-time OTA progress display
 * - Thread-safe LVGL operations
 * 
 * Performance Optimizations:
 * - Parallel LVGL rendering with 2 draw units
 * - Optimized memory allocation using PSRAM
 * - Reduced display refresh period (8ms vs 16ms)
 * - Caching enabled for shadows, circles, images
 * - WiFi operations isolated to Core 1 to prevent display glitches
 */

void setup() {
    // Initialize the optimized application controller
    if (!Application::init()) {
        log_e("Failed to initialize application controller");
        
        // Try to provide some diagnostic information
        ESP.restart();
    }

    log_i("ESP32-S3 UniMixer Client initialized successfully");
    log_i("Architecture: Multi-threaded with dual-core optimization");
    log_i("LVGL: Optimized with FreeRTOS integration and PSRAM");
    log_i("Core 0: UI/LVGL tasks (high priority)");
    log_i("Core 1: Network/OTA tasks (background)");
    
    // Print memory information
    log_i("Free heap: %d bytes", ESP.getFreeHeap());
    log_i("Free PSRAM: %d bytes", ESP.getFreePsram());
    log_i("PSRAM size: %d bytes", ESP.getPsramSize());
}

void loop() {
    // In the new architecture, this loop is much simpler
    // All heavy processing is handled by dedicated FreeRTOS tasks
    Application::run();
    
    // Small delay to prevent watchdog triggers and allow task switching
    // The actual UI responsiveness is handled by the LVGL task
    delay(1);
}