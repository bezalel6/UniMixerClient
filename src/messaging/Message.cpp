#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include "protocol/MessageConfig.h"
#include <ArduinoJson.h>

static const char *TAG = "Message";

namespace Messaging {
template class ArduinoJson::VariantRef;
template class ArduinoJson::VariantConstRef;
template class ArduinoJson::JsonVariant;
template class ArduinoJson::JsonVariantConst;

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
  doc["messageType"] = static_cast<int>(type);
  doc["deviceId"] = deviceId;
  doc["requestId"] = requestId;
  doc["timestamp"] = timestamp;

  // Type-specific data
  switch (type) {
  case AUDIO_STATUS: {
    doc["activeSessionCount"] = data.audio.activeSessionCount;

    // Sessions array
    JsonArray sessions = doc["sessions"].to<JsonArray>();
    for (int i = 0; i < data.audio.sessionCount && i < 16; i++) {
      JsonObject session = sessions.add<JsonObject>();
      session["processId"] = data.audio.sessions[i].processId;
      session["processName"] = data.audio.sessions[i].processName;
      session["displayName"] = data.audio.sessions[i].displayName;
      session["volume"] = data.audio.sessions[i].volume;
      session["isMuted"] = data.audio.sessions[i].isMuted;
      session["state"] = data.audio.sessions[i].state;
    }

    // Default device
    if (data.audio.hasDefaultDevice) {
      JsonObject defaultDevice = doc["defaultDevice"].to<JsonObject>();
      defaultDevice["friendlyName"] = data.audio.defaultDevice.friendlyName;
      defaultDevice["volume"] = data.audio.defaultDevice.volume;
      defaultDevice["isMuted"] = data.audio.defaultDevice.isMuted;
      defaultDevice["dataFlow"] = data.audio.defaultDevice.dataFlow;
      defaultDevice["deviceRole"] = data.audio.defaultDevice.deviceRole;
    }

    // Additional fields
    doc["reason"] = data.audio.reason;
    if (strlen(data.audio.originatingRequestId) > 0) {
      doc["originatingRequestId"] = data.audio.originatingRequestId;
    } else {
      doc["originatingRequestId"] = nullptr;
    }
    if (strlen(data.audio.originatingDeviceId) > 0) {
      doc["originatingDeviceId"] = data.audio.originatingDeviceId;
    } else {
      doc["originatingDeviceId"] = nullptr;
    }
    break;
  }

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
  msg.type = static_cast<Type>(doc["messageType"] | 0);
  msg.deviceId = doc["deviceId"] | "";
  msg.requestId = doc["requestId"] | "";
  msg.timestamp = doc["timestamp"] | millis();

  // Parse type-specific data
  switch (msg.type) {
  case AUDIO_STATUS: {
    msg.data.audio.activeSessionCount = doc["activeSessionCount"] | 0;

    // Parse sessions array
    JsonArray sessions = doc["sessions"];
    msg.data.audio.sessionCount = 0;
    if (sessions) {
      for (int i = 0; i < sessions.size() && i < 16; i++) {
        JsonObject session = sessions[i];
        msg.data.audio.sessions[i].processId = session["processId"] | 0;
        strncpy(msg.data.audio.sessions[i].processName,
                session["processName"] | "",
                sizeof(msg.data.audio.sessions[i].processName) - 1);
        strncpy(msg.data.audio.sessions[i].displayName,
                session["displayName"] | "",
                sizeof(msg.data.audio.sessions[i].displayName) - 1);
        msg.data.audio.sessions[i].volume = session["volume"] | 0.0f;
        msg.data.audio.sessions[i].isMuted = session["isMuted"] | false;
        strncpy(msg.data.audio.sessions[i].state, session["state"] | "",
                sizeof(msg.data.audio.sessions[i].state) - 1);
        msg.data.audio.sessionCount++;
      }
    }

    // Parse default device
    JsonObject defaultDevice = doc["defaultDevice"];
    if (defaultDevice) {
      msg.data.audio.hasDefaultDevice = true;
      strncpy(msg.data.audio.defaultDevice.friendlyName,
              defaultDevice["friendlyName"] | "",
              sizeof(msg.data.audio.defaultDevice.friendlyName) - 1);
      msg.data.audio.defaultDevice.volume = defaultDevice["volume"] | 0.0f;
      msg.data.audio.defaultDevice.isMuted = defaultDevice["isMuted"] | false;
      strncpy(msg.data.audio.defaultDevice.dataFlow,
              defaultDevice["dataFlow"] | "",
              sizeof(msg.data.audio.defaultDevice.dataFlow) - 1);
      strncpy(msg.data.audio.defaultDevice.deviceRole,
              defaultDevice["deviceRole"] | "",
              sizeof(msg.data.audio.defaultDevice.deviceRole) - 1);
    } else {
      msg.data.audio.hasDefaultDevice = false;
    }

    // Parse additional fields
    strncpy(msg.data.audio.reason, doc["reason"] | "",
            sizeof(msg.data.audio.reason) - 1);
    strncpy(msg.data.audio.originatingRequestId,
            doc["originatingRequestId"] | "",
            sizeof(msg.data.audio.originatingRequestId) - 1);
    strncpy(msg.data.audio.originatingDeviceId, doc["originatingDeviceId"] | "",
            sizeof(msg.data.audio.originatingDeviceId) - 1);
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
