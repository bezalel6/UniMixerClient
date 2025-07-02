#ifndef OTA_APPLICATION_H
#define OTA_APPLICATION_H

#include <Arduino.h>
#include "BootManager.h"

namespace OTA {

/**
 * Dedicated OTA Application
 * Runs in its own boot mode, completely separate from normal operation
 */
class OTAApplication {
   public:
    /**
     * Initialize OTA-only application
     */
    static bool init();

    /**
     * Run the OTA application main loop
     */
    static void run();

    /**
     * Cleanup and return to normal mode
     */
    static void cleanup();

   private:
    static bool initialized;

    /**
     * Initialize minimal display for OTA progress
     */
    static bool initMinimalDisplay();

    /**
     * Initialize network for OTA download
     */
    static bool initNetworkForOTA();

    /**
     * Run OTA process
     */
    static bool performOTA();

    /**
     * Display OTA progress
     */
    static void displayProgress(uint8_t progress, const char* message);

    /**
     * Handle OTA completion
     */
    static void handleOTAComplete(bool success, const char* message);
};

}  // namespace OTA

#endif  // OTA_APPLICATION_H
