#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

namespace Messaging::Protocol {

// Topics - same for MQTT and Serial
inline constexpr const char* TOPIC_AUDIO_REQUESTS = "homeassistant/unimix/audio/requests";
inline constexpr const char* TOPIC_AUDIO_STATUS = "homeassistant/unimix/audio_status";
inline constexpr const char* TOPIC_SYSTEM_STATUS = "homeassistant/smartdisplay/status";

// Message Types
inline constexpr const char* MSG_TYPE_AUDIO_STATUS_REQUEST = "audio.status.request";
inline constexpr const char* MSG_TYPE_AUDIO_MIX_UPDATE = "audio.mix.update";
inline constexpr const char* MSG_TYPE_SYSTEM_STATUS = "system.status";

// Audio Actions
inline constexpr const char* AUDIO_ACTION_SET_VOLUME = "SetVolume";
inline constexpr const char* AUDIO_ACTION_MUTE = "Mute";
inline constexpr const char* AUDIO_ACTION_UNMUTE = "Unmute";

// Configuration Constants
inline constexpr int MAX_TOPIC_LENGTH = 128;
inline constexpr int MAX_PAYLOAD_LENGTH = 512;
inline constexpr int MAX_HANDLERS = 10;
inline constexpr int MAX_IDENTIFIER_LENGTH = 64;

// Serial Protocol Constants
inline constexpr char SERIAL_DELIMITER = ':';
inline constexpr char SERIAL_TERMINATOR = '\n';
inline constexpr int SERIAL_TIMEOUT_MS = 1000;

// Serial Interface Configuration
inline constexpr int SERIAL_BAUD_RATE = 115200;  // Standard Serial - Debug/logs and messaging

}  // namespace Messaging::Protocol

#endif  // MESSAGE_PROTOCOL_H