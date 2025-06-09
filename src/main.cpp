#include <Arduino.h>
#include "application/AppController.h"

/*
 * ESP32-S3 Dual USB Serial Configuration:
 * - CDC0 (Serial): Debug logs, ESP_LOG output, system diagnostics
 * - CDC1 (DataSerial): Clean messaging protocol (MQTT-compatible format over serial)
 *
 * Host side will see two serial ports:
 * - /dev/ttyACM0 or COM3: Debug interface (Serial - human readable)
 * - /dev/ttyACM1 or COM4: Data interface (DataSerial - protocol messages only)
 */

void setup() {
    // Initialize the application controller (includes dual serial setup)
    if (!Application::init()) {
        log_e("Failed to initialize application controller");
        while (1) {
            delay(1000);
        }
    }

    log_i("Application initialized successfully");
    log_i("Debug interface: CDC0 (Serial) - Logs and diagnostics");
    log_i("Data interface: CDC1 (DataSerial) - Clean messaging protocol");
}

void loop() {
    // Run the main application loop
    Application::run();

    delay(10);
}