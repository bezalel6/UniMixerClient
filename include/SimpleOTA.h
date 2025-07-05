#pragma once

#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

namespace SimpleOTA {

// =============================================================================
// SIMPLE CONFIGURATION
// =============================================================================

struct Config {
    const char* serverURL = "http://rndev.local:3000/api/firmware/latest.bin";
    const char* wifiSSID = "IOT";
    const char* wifiPassword = "0527714039a";
    uint32_t timeoutMS = 300000;  // 5 minutes
    bool showProgress = true;
    bool autoReboot = true;
};

// =============================================================================
// SIMPLE API - Just What's Needed
// =============================================================================

/**
 * Initialize SimpleOTA with configuration
 */
bool init(const Config& config = {});

/**
 * Start OTA update process
 * Blocks until complete or error
 */
bool startUpdate();

/**
 * Check if OTA is currently running
 */
bool isRunning();

/**
 * Get current progress (0-100)
 */
uint8_t getProgress();

/**
 * Get current status message
 */
const char* getStatusMessage();

/**
 * Cleanup and deinitialize
 */
void deinit();

/**
 * Initialize with default configuration
 */
bool initWithDefaults();

} // namespace SimpleOTA
