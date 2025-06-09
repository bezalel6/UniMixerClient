#include <Arduino.h>
#include "application/AppController.h"

/*
 * ESP32-S3 Serial Configuration:
 * - Serial: Debug logs, ESP_LOG output, system diagnostics, and messaging protocol
 */

void setup() {
    // Initialize the application controller (includes serial setup)
    if (!Application::init()) {
        log_e("Failed to initialize application controller");
        while (1) {
            delay(1000);
        }
    }

    log_i("Application initialized successfully");
    log_i("Serial interface: Standard Serial - Logs, diagnostics, and messaging protocol");
}

void loop() {
    // Run the main application loop
    Application::run();

    delay(10);
}