#include <Arduino.h>
#include "application/app_controller.h"

void setup() {
    // Initialize the application controller
    if (!app_controller_init()) {
        log_e("Failed to initialize application controller");
        while (1) {
            delay(1000);
        }
    }

    log_i("Application initialized successfully");
}

void loop() {
    // Run the main application loop
    app_controller_run();

    delay(10);
}