#include "MessageConfig.h"
#include "../hardware/DeviceManager.h"

namespace Messaging {
namespace Config {

// =============================================================================
// DEVICE CONFIGURATION
// =============================================================================

const char* DEVICE_ID = "ESP32S3-CONTROL-CENTER";
const char* DEVICE_TYPE = "ESP32SmartDisplay";

// =============================================================================
// TOPIC CONFIGURATION
// =============================================================================

const char* TOPIC_AUDIO_STATUS_REQUEST = "audio/status/request";
const char* TOPIC_AUDIO_STATUS_RESPONSE = "audio/status/response";
const char* TOPIC_AUDIO_CONTROL = "audio/control";
const char* TOPIC_WILDCARD = "*";

// =============================================================================
// MESSAGE TYPE CONFIGURATION
// =============================================================================

const char* MESSAGE_TYPE_GET_STATUS = "GetStatus";
const char* MESSAGE_TYPE_STATUS_UPDATE = "StatusUpdate";
const char* MESSAGE_TYPE_SET_VOLUME = "SetProcessVolume";
const char* MESSAGE_TYPE_MUTE_PROCESS = "MuteProcess";
const char* MESSAGE_TYPE_UNMUTE_PROCESS = "UnmuteProcess";
const char* MESSAGE_TYPE_SET_MASTER_VOLUME = "SetMasterVolume";
const char* MESSAGE_TYPE_MUTE_MASTER = "MuteMaster";
const char* MESSAGE_TYPE_UNMUTE_MASTER = "UnmuteMaster";

// =============================================================================
// REASON CODES
// =============================================================================

const char* REASON_UPDATE_RESPONSE = "UpdateResponse";
const char* REASON_STATUS_REQUEST = "StatusRequest";

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================

const unsigned long ACTIVITY_TIMEOUT_MS = 30000;         // 30 seconds
const unsigned long MESSAGE_LOG_TRUNCATE_LENGTH = 1000;  // Characters

// =============================================================================
// MESSAGE DETECTION KEYWORDS
// =============================================================================

const char* AUDIO_MESSAGE_KEYWORD_SESSIONS = "sessions";
const char* AUDIO_MESSAGE_KEYWORD_DEFAULT_DEVICE = "defaultDevice";
const char* STATUS_KEYWORD = "status";

// =============================================================================
// TRANSPORT NAMES
// =============================================================================

const char* TRANSPORT_NAME_MQTT = "MQTT";
const char* TRANSPORT_NAME_SERIAL = "Serial";

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

String generateRequestId() {
    return "esp32_" + String(millis());
}

String getDeviceId() {
    return String(DEVICE_ID);
}

}  // namespace Config
}  // namespace Messaging
