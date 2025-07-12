#ifndef MESSAGING_CONFIG_H
#define MESSAGING_CONFIG_H

// Messaging Transport Configuration
// Enable/disable different transport methods at compile time

// Network Transports (available only during OTA mode for maximum normal mode
// performance)
#define MESSAGING_ENABLE_MQTT_TRANSPORT 0

// Serial Transport (via USB/UART)
#define MESSAGING_ENABLE_SERIAL_TRANSPORT 1

// Network-free Normal Mode (Serial only), Network available during OTA mode
#define MESSAGING_ENABLE_DUAL_TRANSPORT 0

// Transport mode for normal operation
// 0 = Network (OTA mode only), 1 = Serial (normal mode), 2 = Dynamic
// (mode-dependent)
#define MESSAGING_DEFAULT_TRANSPORT 1

// Serial Configuration (ESP32-S3)
// Standard Serial: Debug logs, ESP_LOG output, diagnostics, and messaging
// protocol
#define MESSAGING_SERIAL_BAUD_RATE 115200 // Match server configuration
#define MESSAGING_SERIAL_BUFFER_SIZE                                           \
  4096 * 2 // Increased from 2048 to 4096 for better UART handling
#define MESSAGING_SERIAL_TIMEOUT_MS 1000 // Match server read/write timeout

// Debug Configuration
#define MESSAGING_DEBUG_ENABLED 0
#define MESSAGING_LOG_ALL_MESSAGES 0
#define MESSAGING_DESERIALIZATION_DEBUG_MODE                                   \
  0 // 0 = Normal processing, 1 = Log to UI only

// Binary Protocol Debug Configuration
// Set to 1 to enable, 0 to disable for production builds
#define BINARY_PROTOCOL_DEBUG_FRAMES 0 // Enable detailed binary frame debugging
#define BINARY_PROTOCOL_DEBUG_HEX_DUMP                                         \
  0 // Enable hex dump of transmitted frames
#define BINARY_PROTOCOL_DEBUG_CRC_DETAILS 0 // Enable CRC calculation debugging

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
#define MESSAGING_MAX_HANDLERS 10      // Maximum number of message handlers
#define MESSAGING_MAX_TOPIC_LENGTH 128 // Maximum topic name length
#define MESSAGING_MAX_PAYLOAD_LENGTH                                           \
  4096 * 2 // Increased from 2048 to match buffer size

// =============================================================================
// SAFE MESSAGING MACROS - Centralized Safety Improvements
// =============================================================================

// Safe JSON field extraction with null checks and default values
#define SAFE_JSON_EXTRACT_STRING(doc, field, target, defaultValue)             \
  do {                                                                         \
    const char *_temp = (doc)[field];                                          \
    (target) = _temp ? String(_temp) : String(defaultValue);                   \
  } while (0)

#define SAFE_JSON_EXTRACT_CSTRING(doc, field, target, targetSize,              \
                                  defaultValue)                                \
  do {                                                                         \
    const char *_temp = (doc)[field];                                          \
    if (_temp) {                                                               \
      strncpy((target), _temp, (targetSize) - 1);                              \
      (target)[(targetSize) - 1] = '\0';                                       \
    } else {                                                                   \
      strncpy((target), defaultValue, (targetSize) - 1);                       \
      (target)[(targetSize) - 1] = '\0';                                       \
    }                                                                          \
  } while (0)
#define SAFE_JSON_EXTRACT_INT(doc, field, target, defaultValue)                \
  (target) = (doc)[field].isNull() ? (defaultValue) : (doc)[field].as<int>()

#define SAFE_JSON_EXTRACT_BOOL(doc, field, target, defaultValue)               \
  (target) = (doc)[field].isNull() ? (defaultValue) : (doc)[field].as<bool>()

#define SAFE_JSON_EXTRACT_FLOAT(doc, field, target, defaultValue)              \
  (target) = (doc)[field].isNull() ? (defaultValue) : (doc)[field].as<float>()

// Safe string cloning with bounds checking
#define SAFE_STRING_CLONE(source, target, targetSize)                          \
  do {                                                                         \
    strncpy((target), (source).c_str(), (targetSize) - 1);                     \
    (target)[(targetSize) - 1] = '\0';                                         \
  } while (0)

#define SAFE_CSTRING_CLONE(source, target, targetSize)                         \
  do {                                                                         \
    if (source) {                                                              \
      strncpy((target), (source), (targetSize) - 1);                           \
      (target)[(targetSize) - 1] = '\0';                                       \
    } else {                                                                   \
      (target)[0] = '\0';                                                      \
    }                                                                          \
  } while (0)

// Safe field validation and assignment
#define VALIDATE_AND_ASSIGN_STRING(condition, target, value, defaultValue)     \
  (target) = (condition) ? (value) : (defaultValue)

#define VALIDATE_STRING_LENGTH(str, maxLen) ((str).length() <= (maxLen))

// JSON field existence checks
#define JSON_FIELD_EXISTS(doc, field) (!(doc)[field].isNull())

#define JSON_FIELD_IS_STRING(doc, field) ((doc)[field].is<const char *>())

#define JSON_FIELD_IS_NUMBER(doc, field)                                       \
  ((doc)[field].is<int>() || (doc)[field].is<float>())

// Safe array bounds checking
#define SAFE_ARRAY_ACCESS(array, index, maxSize)                               \
  ((index) >= 0 && (index) < (maxSize))

#define SAFE_ARRAY_COPY(source, target, count, maxSize)                        \
  do {                                                                         \
    size_t _copyCount = ((count) < (maxSize)) ? (count) : (maxSize);           \
    for (size_t _i = 0; _i < _copyCount; _i++) {                               \
      (target)[_i] = (source)[_i];                                             \
    }                                                                          \
  } while (0)

// Memory safety macros
#define SAFE_STRLEN(str) ((str) ? strlen(str) : 0)

#define SAFE_MEMSET(ptr, value, size)                                          \
  do {                                                                         \
    if (ptr) {                                                                 \
      memset((ptr), (value), (size));                                          \
    }                                                                          \
  } while (0)

// Debug logging macros for consistent formatting
#define LOG_JSON_PARSE_ERROR(tag, error)                                       \
  ESP_LOGW(tag, "JSON parse error: %s", error.c_str())

#define LOG_FIELD_EXTRACTION(tag, field, value)                                \
  ESP_LOGD(tag, "Extracted field '%s': '%s'", field, value)

#define LOG_SAFE_OPERATION(tag, operation)                                     \
  ESP_LOGD(tag, "Safe operation: %s", operation)

#endif // MESSAGING_CONFIG_H
