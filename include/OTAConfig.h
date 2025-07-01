#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

// =============================================================================
// UNIFIED OTA SYSTEM CONFIGURATION
// =============================================================================

// Core OTA Settings
#define OTA_ENABLE_UPDATES 1  // Master OTA enable
#define OTA_HTTP_BASED 1      // HTTPUpdate implementation

// Network Configuration (On-Demand Only)
#define OTA_WIFI_SSID "IOT"
#define OTA_WIFI_PASSWORD "0527714039a"

// Server Configuration
#define OTA_SERVER_URL "http://rndev.local:3000/api/firmware/latest.bin"
#define OTA_SERVER_PORT 80
#define OTA_FIRMWARE_VERSION "1.0.0"

// Timeout Configuration
#define OTA_NETWORK_CONNECT_TIMEOUT_MS 30000  // 30 seconds to connect
#define OTA_DOWNLOAD_TIMEOUT_MS 300000        // 5 minutes total download time
#define OTA_PROGRESS_TIMEOUT_MS 60000         // 1 minute without progress

// Security Configuration
#define OTA_REQUIRE_AUTHENTICATION 0
#define OTA_USERNAME "admin"
#define OTA_PASSWORD "password"

// UI Integration
#define OTA_SHOW_PROGRESS_SCREEN 1    // Show OTA screen during update
#define OTA_ENABLE_USER_CANCEL 1      // Allow user cancellation
#define OTA_AUTO_REBOOT_ON_SUCCESS 1  // Auto-reboot after successful update

// Performance Tuning
#define OTA_PROGRESS_UPDATE_INTERVAL_MS 500  // Progress update frequency
#define OTA_MIN_PROGRESS_STEP 1              // Report every 1% progress
#define OTA_WATCHDOG_FEED_INTERVAL_MS 1000   // Feed watchdog every 1 second
#define OTA_TASK_YIELD_INTERVAL_MS 50        // Yield to other tasks every 50ms
#define OTA_UI_UPDATE_THROTTLE_MS 250        // Throttle UI updates to 4/second

#endif  // OTA_CONFIG_H
