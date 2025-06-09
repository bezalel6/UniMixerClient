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
#define MESSAGING_SERIAL_BAUD_RATE 115200
#define MESSAGING_SERIAL_BUFFER_SIZE 1024
#define MESSAGING_SERIAL_TIMEOUT_MS 1000

// Debug Configuration
#define MESSAGING_DEBUG_ENABLED 1
#define MESSAGING_LOG_ALL_MESSAGES 0

// Performance Configuration
#define MESSAGING_MAX_HANDLERS 10
#define MESSAGING_MAX_TOPIC_LENGTH 128
#define MESSAGING_MAX_PAYLOAD_LENGTH 512

#endif  // MESSAGING_CONFIG_H