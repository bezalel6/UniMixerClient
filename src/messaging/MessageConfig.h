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

// Asset/Logo message types
extern const char* MESSAGE_TYPE_GET_ASSETS;
extern const char* MESSAGE_TYPE_ASSET_RESPONSE;

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
// TRANSPORT NAMES
// =============================================================================

extern const char* TRANSPORT_NAME_MQTT;
extern const char* TRANSPORT_NAME_SERIAL;

String generateRequestId();
String getDeviceId();
}  // namespace Config
}  // namespace Messaging
