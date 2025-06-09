#include <Arduino.h>
#include "application/AppController.h"

void setup() {
    // Initialize the application controller
    if (!Application::init()) {
        log_e("Failed to initialize application controller");
        while (1) {
            delay(1000);
        }
    }

    log_i("Application initialized successfully");
}

void loop() {
    // Run the main application loop
    Application::run();

    delay(10);
}