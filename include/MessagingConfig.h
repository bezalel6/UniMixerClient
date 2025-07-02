#ifndef MESSAGING_CONFIG_H
#define MESSAGING_CONFIG_H

// Messaging Transport Configuration
// Enable/disable different transport methods at compile time

// MQTT Transport (via WiFi)
#define MESSAGING_ENABLE_MQTT_TRANSPORT 0

// Serial Transport (via USB/UART)
#define MESSAGING_ENABLE_SERIAL_TRANSPORT 1

// Dual Transport Mode (both MQTT and Serial simultaneously)
#define MESSAGING_ENABLE_DUAL_TRANSPORT 0

// Default transport selection
// 0 = MQTT only, 1 = Serial only, 2 = Both transports
#define MESSAGING_DEFAULT_TRANSPORT 1

// Serial Configuration (ESP32-S3)
// Standard Serial: Debug logs, ESP_LOG output, diagnostics, and messaging protocol
#define MESSAGING_SERIAL_BAUD_RATE 115200      // Match server configuration
#define MESSAGING_SERIAL_BUFFER_SIZE 4096 * 2  // Increased from 2048 to 4096 for better UART handling
#define MESSAGING_SERIAL_TIMEOUT_MS 1000       // Match server read/write timeout

// Debug Configuration
#define MESSAGING_DEBUG_ENABLED 0
#define MESSAGING_LOG_ALL_MESSAGES 0
#define MESSAGING_DESERIALIZATION_DEBUG_MODE 0  // 0 = Normal processing, 1 = Log to UI only

// Binary Protocol Debug Configuration
// Set to 1 to enable, 0 to disable for production builds
#define BINARY_PROTOCOL_DEBUG_FRAMES 0       // Enable detailed binary frame debugging
#define BINARY_PROTOCOL_DEBUG_HEX_DUMP 0     // Enable hex dump of transmitted frames
#define BINARY_PROTOCOL_DEBUG_CRC_DETAILS 0  // Enable CRC calculation debugging

/*
 * Binary Protocol Debug Usage:
 *
 * BINARY_PROTOCOL_DEBUG_FRAMES:
 *   - Shows frame structure analysis (start/end markers, length, type)
 *   - State machine transitions in the framer
 *   - Message completion notifications
 *   - Uses ESP_LOGW level for visibility
 *
 * BINARY_PROTOCOL_DEBUG_HEX_DUMP:
 *   - Hex dumps of transmitted and received data
 *   - ASCII representation where printable
 *   - Limited to first 64 bytes to avoid log spam
 *
 * BINARY_PROTOCOL_DEBUG_CRC_DETAILS:
 *   - CRC calculation process
 *   - CRC verification during reception
 *   - Hex dump of data used for CRC calculation
 *
 * For production: Set all to 0 to reduce log noise and improve performance
 */

// Performance Configuration
#define MESSAGING_MAX_HANDLERS 10              // Maximum number of message handlers
#define MESSAGING_MAX_TOPIC_LENGTH 128         // Maximum topic name length
#define MESSAGING_MAX_PAYLOAD_LENGTH 4096 * 2  // Increased from 2048 to match buffer size

#endif  // MESSAGING_CONFIG_H
