#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

namespace Messaging::Protocol {

// Serial Protocol - Simplified (no prefixes needed)

// Command Types (aligned with server)
inline constexpr const char* COMMAND_SET_VOLUME = "SetVolume";
inline constexpr const char* COMMAND_MUTE = "Mute";
inline constexpr const char* COMMAND_UNMUTE = "Unmute";
inline constexpr const char* COMMAND_GET_STATUS = "GetStatus";
inline constexpr const char* COMMAND_GET_ALL_SESSIONS = "GetAllSessions";

// Configuration Constants
inline constexpr int MAX_TOPIC_LENGTH = 128;
inline constexpr int MAX_PAYLOAD_LENGTH = 2048;  // Increased for status messages
inline constexpr int MAX_HANDLERS = 10;
inline constexpr int MAX_IDENTIFIER_LENGTH = 64;

// Serial Protocol Constants
inline constexpr char SERIAL_TERMINATOR = '\n';
inline constexpr int SERIAL_TIMEOUT_MS = 1000;

// Serial Interface Configuration
inline constexpr int SERIAL_BAUD_RATE = 115200;  // Standard Serial - Debug/logs and messaging

// Request ID generation helper
inline String generateRequestId() {
    return String("esp32_") + String(millis());
}

}  // namespace Messaging::Protocol

#endif  // MESSAGE_PROTOCOL_H