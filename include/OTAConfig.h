#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

// NETWORK-FREE ARCHITECTURE: OTA On-Demand Mode Configuration
// OTA functionality is now ONLY available when explicitly requested by user

// OTA Feature Control
#define OTA_ENABLE_UPDATES 1             // Keep OTA capability but only on-demand
#define OTA_ON_DEMAND_ONLY 1             // NEW: Only activate OTA when user requests it
#define OTA_DISABLE_AUTOMATIC_NETWORK 1  // NEW: Don't auto-connect to network for OTA

// OTA Network Configuration (used only when OTA mode is activated)
#define OTA_WIFI_SSID "YourSSID"
#define OTA_WIFI_PASSWORD "YourPassword"

// OTA Server Configuration
#define OTA_SERVER_URL "http://your-server.com/firmware.bin"
#define OTA_SERVER_PORT 80
#define OTA_FIRMWARE_VERSION "1.0.0"

// OTA Timeouts (for on-demand mode)
#define OTA_NETWORK_CONNECT_TIMEOUT_MS 30000   // 30 seconds to connect
#define OTA_DOWNLOAD_TIMEOUT_MS 300000         // 5 minutes total download time
#define OTA_USER_CANCEL_CHECK_INTERVAL_MS 500  // Check for user cancel every 500ms

// OTA Security
#define OTA_REQUIRE_AUTHENTICATION 0
#define OTA_USERNAME "admin"
#define OTA_PASSWORD "password"

// OTA Progress Reporting
#define OTA_PROGRESS_UPDATE_INTERVAL_MS 1000
#define OTA_MIN_PROGRESS_STEP 1  // Report every 1% progress

// NETWORK-FREE ARCHITECTURE: Resource Optimization
#define OTA_DISABLE_ARDUINO_OTA 1    // Disable Arduino OTA service
#define OTA_MINIMAL_NETWORK_STACK 1  // Use minimal network resources
#define OTA_CLEANUP_AFTER_USE 1      // Fully cleanup network after OTA

// OTA UI Integration
#define OTA_SHOW_PROGRESS_SCREEN 1
#define OTA_ENABLE_USER_CANCEL 1
#define OTA_AUTO_REBOOT_ON_SUCCESS 1
#define OTA_RETURN_TO_MAIN_ON_FAIL 1

// Performance Settings for Network-Free Architecture
#define OTA_REALLOCATE_NETWORK_RESOURCES 1  // Redirect network task resources to UI/audio
#define OTA_BOOST_UI_PRIORITY_DURING_OTA 1  // Ensure UI stays responsive during OTA

#endif  // OTA_CONFIG_H
