#include "MessageConfig.h"
#include "hardware/DeviceManager.h"
#include <MessageProtocol.h>

namespace Messaging {
namespace Config {

// =============================================================================
// DEVICE CONFIGURATION
// =============================================================================

const char* DEVICE_ID = "ESP32S3-CONTROL-CENTER";
const char* DEVICE_TYPE = "ESP32SmartDisplay";

// =============================================================================
// NEW ENUM-BASED MESSAGE TYPE CONFIGURATION
// =============================================================================

// External message types (cross-transport boundaries)
const MessageProtocol::ExternalMessageType EXT_MSG_GET_STATUS = MessageProtocol::ExternalMessageType::GET_STATUS;
const MessageProtocol::ExternalMessageType EXT_MSG_STATUS_UPDATE = MessageProtocol::ExternalMessageType::STATUS_UPDATE;
const MessageProtocol::ExternalMessageType EXT_MSG_STATUS_MESSAGE = MessageProtocol::ExternalMessageType::STATUS_MESSAGE;
const MessageProtocol::ExternalMessageType EXT_MSG_GET_ASSETS = MessageProtocol::ExternalMessageType::GET_ASSETS;
const MessageProtocol::ExternalMessageType EXT_MSG_ASSET_RESPONSE = MessageProtocol::ExternalMessageType::ASSET_RESPONSE;
const MessageProtocol::ExternalMessageType EXT_MSG_SESSION_UPDATE = MessageProtocol::ExternalMessageType::SESSION_UPDATE;

// Internal message types (ESP32 internal communication)
const MessageProtocol::InternalMessageType INT_MSG_WIFI_STATUS = MessageProtocol::InternalMessageType::WIFI_STATUS;
const MessageProtocol::InternalMessageType INT_MSG_NETWORK_INFO = MessageProtocol::InternalMessageType::NETWORK_INFO;
const MessageProtocol::InternalMessageType INT_MSG_UI_UPDATE = MessageProtocol::InternalMessageType::UI_UPDATE;
const MessageProtocol::InternalMessageType INT_MSG_AUDIO_STATE_UPDATE = MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE;
const MessageProtocol::InternalMessageType INT_MSG_AUDIO_UI_REFRESH = MessageProtocol::InternalMessageType::AUDIO_UI_REFRESH;
const MessageProtocol::InternalMessageType INT_MSG_SD_STATUS = MessageProtocol::InternalMessageType::SD_STATUS;
const MessageProtocol::InternalMessageType INT_MSG_ASSET_RESPONSE = MessageProtocol::InternalMessageType::ASSET_RESPONSE;

// Legacy string-based message types have been removed.
// Use the new enum-based constants (EXT_MSG_* and INT_MSG_*) instead.

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
// TRANSPORT NAMES
// =============================================================================

// Network transports not currently available
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
