#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

// OTA (Over-The-Air) Update Configuration
// Enable/disable OTA updating functionality at compile time

// Main OTA Feature Toggle
#define OTA_ENABLE_UPDATES 1

// OTA Security Configuration
#define OTA_REQUIRE_PASSWORD 0 // Require password for OTA updates
#define OTA_PASSWORD "esp32_smartdisplay_ota" // Default OTA password
#define OTA_HOSTNAME "esp32-smartdisplay"     // Device hostname for OTA

// OTA Port Configuration
#define OTA_PORT 8266 // Default port for Arduino OTA

#endif // OTA_CONFIG_H