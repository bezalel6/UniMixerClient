#pragma once

#include <Arduino.h>

namespace Messaging {
namespace Config {

// =============================================================================
// DEVICE CONFIGURATION
// =============================================================================

extern const char* DEVICE_ID;
extern const char* DEVICE_TYPE;

// =============================================================================
// TOPIC CONFIGURATION
// =============================================================================

extern const char* TOPIC_AUDIO_STATUS_REQUEST;
extern const char* TOPIC_AUDIO_STATUS_RESPONSE;
extern const char* TOPIC_AUDIO_CONTROL;
extern const char* TOPIC_WILDCARD;

// =============================================================================
// MESSAGE TYPE CONFIGURATION
// =============================================================================

extern const char* MESSAGE_TYPE_GET_STATUS;
extern const char* MESSAGE_TYPE_STATUS_UPDATE;
extern const char* MESSAGE_TYPE_SET_VOLUME;
extern const char* MESSAGE_TYPE_MUTE_PROCESS;
extern const char* MESSAGE_TYPE_UNMUTE_PROCESS;
extern const char* MESSAGE_TYPE_SET_MASTER_VOLUME;
extern const char* MESSAGE_TYPE_MUTE_MASTER;
extern const char* MESSAGE_TYPE_UNMUTE_MASTER;

// =============================================================================
// REASON CODES
// =============================================================================

extern const char* REASON_UPDATE_RESPONSE;
extern const char* REASON_STATUS_REQUEST;

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================

extern const unsigned long ACTIVITY_TIMEOUT_MS;
extern const unsigned long MESSAGE_LOG_TRUNCATE_LENGTH;

// =============================================================================
// MESSAGE DETECTION KEYWORDS
// =============================================================================

extern const char* AUDIO_MESSAGE_KEYWORD_SESSIONS;
extern const char* AUDIO_MESSAGE_KEYWORD_DEFAULT_DEVICE;
extern const char* STATUS_KEYWORD;

// =============================================================================
// TRANSPORT NAMES
// =============================================================================

extern const char* TRANSPORT_NAME_MQTT;
extern const char* TRANSPORT_NAME_SERIAL;

String generateRequestId();
String getDeviceId();
}  // namespace Config
}  // namespace Messaging