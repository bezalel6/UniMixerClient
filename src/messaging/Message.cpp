#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include "protocol/MessageConfig.h"
#include <ArduinoJson.h>

static const char *TAG = "Message";

namespace Messaging {

MessageRouter *MessageRouter::instance = nullptr;

// =============================================================================
// MESSAGE FACTORY METHODS
// =============================================================================

Message Message::createStatusRequest(const String &deviceId) {
  Message msg;
  msg.type = GET_STATUS;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();
  return msg;
}

Message Message::createAssetRequest(const String &processName,
                                    const String &deviceId) {
  Message msg;
  msg.type = ASSET_REQUEST;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();

  strncpy(msg.data.asset.processName, processName.c_str(),
          sizeof(msg.data.asset.processName) - 1);

  return msg;
}

Message Message::createVolumeChange(const String &processName, int volume,
                                    const String &deviceId) {
  Message msg;
  msg.type = SET_VOLUME;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();

  strncpy(msg.data.volume.processName, processName.c_str(),
          sizeof(msg.data.volume.processName) - 1);
  msg.data.volume.volume = volume;

  return msg;
}

Message Message::createAudioStatus(const AudioData &audioData,
                                   const String &deviceId) {
  Message msg;
  msg.type = AUDIO_STATUS;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();
  msg.data.audio = audioData;
  return msg;
}

Message Message::createAssetResponse(const AssetData &assetData,
                                     const String &requestId,
                                     const String &deviceId) {
  Message msg;
  msg.type = ASSET_RESPONSE;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = requestId;
  msg.timestamp = millis();
  msg.data.asset = assetData;
  return msg;
}

// =============================================================================
// JSON SERIALIZATION - Direct and simple
// =============================================================================

String Message::toJson() const {
  JsonDocument doc;

  // Core fields
  doc["messageType"] = type;
  doc["deviceId"] = deviceId;
  doc["requestId"] = requestId;
  doc["timestamp"] = timestamp;

  // Type-specific data
  switch (type) {
  case AUDIO_STATUS:
    doc["processName"] = data.audio.processName;
    doc["volume"] = data.audio.volume;
    doc["isMuted"] = data.audio.isMuted;
    doc["hasDefaultDevice"] = data.audio.hasDefaultDevice;
    doc["defaultDeviceName"] = data.audio.defaultDeviceName;
    doc["defaultVolume"] = data.audio.defaultVolume;
    doc["defaultIsMuted"] = data.audio.defaultIsMuted;
    doc["activeSessionCount"] = data.audio.activeSessionCount;
    break;

  case ASSET_REQUEST:
    doc["processName"] = data.asset.processName;
    break;

  case ASSET_RESPONSE:
    doc["processName"] = data.asset.processName;
    doc["success"] = data.asset.success;
    doc["errorMessage"] = data.asset.errorMessage;
    doc["assetData"] = data.asset.assetDataBase64;
    doc["width"] = data.asset.width;
    doc["height"] = data.asset.height;
    doc["format"] = data.asset.format;
    break;

  case SET_VOLUME:
  case VOLUME_CHANGE:
    doc["processName"] = data.volume.processName;
    doc["volume"] = data.volume.volume;
    doc["target"] = data.volume.target;
    break;

  case GET_STATUS:
  case MUTE_TOGGLE:
  case SET_DEFAULT_DEVICE:
    // No additional data needed
    break;

  default:
    ESP_LOGW(TAG, "Unknown message type: %d", type);
  }

  String result;
  serializeJson(doc, result);
  return result;
}

Message Message::fromJson(const String &json) {
  Message msg;
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
    return msg; // Returns invalid message
  }

  // Parse core fields
  msg.type = doc["messageType"];
  msg.deviceId = doc["deviceId"] | "";
  msg.requestId = doc["requestId"] | "";
  msg.timestamp = doc["timestamp"] | millis();

  // Parse type-specific data
  switch (msg.type) {
  case AUDIO_STATUS: {
    strncpy(msg.data.audio.processName, doc["processName"] | "",
            sizeof(msg.data.audio.processName) - 1);
    msg.data.audio.volume = doc["volume"] | 0;
    msg.data.audio.isMuted = doc["isMuted"] | false;
    msg.data.audio.hasDefaultDevice = doc["hasDefaultDevice"] | false;
    strncpy(msg.data.audio.defaultDeviceName, doc["defaultDeviceName"] | "",
            sizeof(msg.data.audio.defaultDeviceName) - 1);
    msg.data.audio.defaultVolume = doc["defaultVolume"] | 0;
    msg.data.audio.defaultIsMuted = doc["defaultIsMuted"] | false;
    msg.data.audio.activeSessionCount = doc["activeSessionCount"] | 0;
    break;
  }

  case ASSET_REQUEST: {
    strncpy(msg.data.asset.processName, doc["processName"] | "",
            sizeof(msg.data.asset.processName) - 1);
    break;
  }

  case ASSET_RESPONSE: {
    strncpy(msg.data.asset.processName, doc["processName"] | "",
            sizeof(msg.data.asset.processName) - 1);
    msg.data.asset.success = doc["success"] | false;
    strncpy(msg.data.asset.errorMessage, doc["errorMessage"] | "",
            sizeof(msg.data.asset.errorMessage) - 1);
    strncpy(msg.data.asset.assetDataBase64, doc["assetData"] | "",
            sizeof(msg.data.asset.assetDataBase64) - 1);
    msg.data.asset.width = doc["width"] | 0;
    msg.data.asset.height = doc["height"] | 0;
    strncpy(msg.data.asset.format, doc["format"] | "",
            sizeof(msg.data.asset.format) - 1);
    break;
  }

  case SET_VOLUME:
  case VOLUME_CHANGE: {
    strncpy(msg.data.volume.processName, doc["processName"] | "",
            sizeof(msg.data.volume.processName) - 1);
    msg.data.volume.volume = doc["volume"] | 0;
    strncpy(msg.data.volume.target, doc["target"] | "default",
            sizeof(msg.data.volume.target) - 1);
    break;
  }

  default:
    // No additional parsing needed
    break;
  }

  return msg;
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

const char *Message::typeToString() const {
  switch (type) {
  case INVALID:
    return "INVALID";
  case AUDIO_STATUS:
    return "STATUS_MESSAGE"; // Match existing protocol
  case VOLUME_CHANGE:
    return "VOLUME_CHANGE";
  case MUTE_TOGGLE:
    return "MUTE_TOGGLE";
  case ASSET_REQUEST:
    return "GET_ASSETS"; // Match existing protocol
  case ASSET_RESPONSE:
    return "ASSET_RESPONSE"; // Match existing protocol
  case GET_STATUS:
    return "GET_STATUS"; // Match existing protocol
  case SET_VOLUME:
    return "SET_VOLUME";
  case SET_DEFAULT_DEVICE:
    return "SET_DEFAULT_DEVICE";
  default:
    return "UNKNOWN";
  }
}

Message::Type Message::stringToType(const String &str) {
  if (str == "STATUS_MESSAGE")
    return AUDIO_STATUS;
  if (str == "VOLUME_CHANGE")
    return VOLUME_CHANGE;
  if (str == "MUTE_TOGGLE")
    return MUTE_TOGGLE;
  if (str == "GET_ASSETS")
    return ASSET_REQUEST;
  if (str == "ASSET_RESPONSE")
    return ASSET_RESPONSE;
  if (str == "GET_STATUS")
    return GET_STATUS;
  if (str == "SET_VOLUME")
    return SET_VOLUME;
  if (str == "SET_DEFAULT_DEVICE")
    return SET_DEFAULT_DEVICE;
  return INVALID;
}

// =============================================================================
// MESSAGE ROUTER IMPLEMENTATION
// =============================================================================

void MessageRouter::send(const Message &msg) {
  if (!msg.isValid()) {
    ESP_LOGW(TAG, "Attempted to send invalid message");
    return;
  }

  SerialEngine::getInstance().send(msg);
}

} // namespace Messaging
