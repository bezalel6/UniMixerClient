#pragma once

#include "MultithreadedOTA.h"

namespace OTA {

/**
 * @brief Multithreaded OTA Application
 * 
 * This application uses the new multithreaded OTA system to provide
 * a responsive, efficient firmware update experience with:
 * 
 * - 60 FPS UI updates on Core 0
 * - Network operations on Core 1  
 * - Streaming downloads with real-time progress
 * - Always-responsive user controls
 * - Smart error handling and recovery
 */
class MultithreadedOTAApplication {
public:
    /**
     * @brief Initialize the multithreaded OTA application
     * 
     * Sets up all required components:
     * - Hardware managers
     * - Display and UI systems
     * - Multithreaded OTA tasks
     * - User interface
     * 
     * @return true if initialization successful, false otherwise
     */
    static bool init();

    /**
     * @brief Run the OTA application main loop
     * 
     * This is a lightweight loop that monitors OTA state and handles
     * application-level completion logic. All heavy work is done in
     * background tasks.
     */
    static void run();

    /**
     * @brief Clean up the OTA application
     * 
     * Properly shuts down all tasks, frees resources, and prepares
     * for system restart or mode change.
     */
    static void cleanup();

    /**
     * @brief Check if the application is initialized
     * @return true if initialized, false otherwise
     */
    static bool isInitialized();

    /**
     * @brief Check if the application is currently running
     * @return true if running, false otherwise
     */
    static bool isRunning();

    /**
     * @brief Get current OTA progress information
     * @return Detailed progress structure with all status information
     */
    static MultiOTA::DetailedProgress_t getProgress();

    /**
     * @brief Get OTA performance statistics
     * @return Statistics structure with performance metrics
     */
    static MultiOTA::OTAStats_t getStats();

    /**
     * @brief Cancel the current OTA operation
     * @return true if cancellation request sent successfully
     */
    static bool cancelOTA();

    /**
     * @brief Retry a failed OTA operation
     * @return true if retry request sent successfully
     */
    static bool retryOTA();

    /**
     * @brief Exit OTA mode and return to normal operation
     * 
     * Clears OTA boot flags and restarts the system in normal mode.
     */
    static void exitOTA();

private:
    static bool s_initialized;
};

} // namespace OTA