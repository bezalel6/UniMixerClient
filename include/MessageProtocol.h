#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

namespace Messaging::Protocol {

// Serial Protocol - Simplified (no prefixes needed)

// Message Types (simplified protocol)
inline constexpr const char* MESSAGE_STATUS_UPDATE = "StatusUpdate";
inline constexpr const char* MESSAGE_GET_STATUS = "GetStatus";

// Configuration Constants
inline constexpr int MAX_TOPIC_LENGTH = 128;      // Maximum topic name length
inline constexpr int MAX_PAYLOAD_LENGTH = 2048;   // Maximum payload size to match server
inline constexpr int MAX_HANDLERS = 10;           // Maximum number of message handlers
inline constexpr int MAX_IDENTIFIER_LENGTH = 64;  // Maximum handler identifier length

// Serial Protocol Constants
inline constexpr char SERIAL_TERMINATOR = '\n';  // Message terminator character
inline constexpr int SERIAL_TIMEOUT_MS = 1000;   // Serial communication timeout

// Serial Interface Configuration
inline constexpr int SERIAL_BAUD_RATE = 115200;  // Serial baud rate to match server

// Request ID generation helper
inline String generateRequestId() {
    return String("esp32_") + String(millis());
}

}  // namespace Messaging::Protocol

#endif  // MESSAGE_PROTOCOL_H