#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include "protocol/MessageConfig.h"
#include <ArduinoJson.h>
#include <MessagingConfig.h>

static const char *TAG = "Message";

namespace Messaging {

// String constants for message types
const char *Message::TYPE_INVALID = "INVALID";
const char *Message::TYPE_AUDIO_STATUS = "AUDIO_STATUS";
const char *Message::TYPE_VOLUME_CHANGE = "VOLUME_CHANGE";
const char *Message::TYPE_MUTE_TOGGLE = "MUTE_TOGGLE";
const char *Message::TYPE_ASSET_REQUEST = "ASSET_REQUEST";
const char *Message::TYPE_ASSET_RESPONSE = "ASSET_RESPONSE";
const char *Message::TYPE_GET_STATUS = "GET_STATUS";
const char *Message::TYPE_SET_VOLUME = "SET_VOLUME";
const char *Message::TYPE_SET_DEFAULT_DEVICE = "SET_DEFAULT_DEVICE";

MessageRouter *MessageRouter::instance = nullptr;

// =============================================================================
// MESSAGE FACTORY METHODS
// =============================================================================

Message Message::createStatusRequest(const String &deviceId) {
  Message msg;
  msg.type = TYPE_GET_STATUS;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();
  return msg;
}

Message Message::createAssetRequest(const String &processName,
                                    const String &deviceId) {
  Message msg;
  msg.type = TYPE_ASSET_REQUEST;
  msg.deviceId = deviceId.isEmpty() ? Config::getDeviceId() : deviceId;
  msg.requestId = Config::generateRequestId();
  msg.timestamp = millis();

  SAFE_STRING_CLONE(processName, msg.data.asset.processName,
                    sizeof(msg.data.asset.processName));

  return msg;
}

Message Message::createVolumeChange(const String &processName, int volume,
                                    const String &deviceId) {
  Message msg;
  msg.type = TYPE_SET_VOLUME;
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
  msg.type = TYPE_AUDIO_STATUS;
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
  msg.type = TYPE_ASSET_RESPONSE;
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
  if (type == TYPE_AUDIO_STATUS) {
    doc["activeSessionCount"] = data.audio.activeSessionCount;
    doc["reason"] = data.audio.reason;

    if (strlen(data.audio.originatingRequestId) > 0) {
      doc["originatingRequestId"] = data.audio.originatingRequestId;
    }
    if (strlen(data.audio.originatingDeviceId) > 0) {
      doc["originatingDeviceId"] = data.audio.originatingDeviceId;
    }

    // For now, skip complex nested objects to avoid API issues
    // Sessions and default device can be added later if needed
  } else if (type == TYPE_ASSET_REQUEST) {
    doc["processName"] = data.asset.processName;
  } else if (type == TYPE_ASSET_RESPONSE) {
    doc["processName"] = data.asset.processName;
    doc["success"] = data.asset.success;
    doc["errorMessage"] = data.asset.errorMessage;
    doc["assetData"] = data.asset.assetDataBase64;
    doc["width"] = data.asset.width;
    doc["height"] = data.asset.height;
    doc["format"] = data.asset.format;
  } else if (type == TYPE_SET_VOLUME || type == TYPE_VOLUME_CHANGE) {
    doc["processName"] = data.volume.processName;
    doc["volume"] = data.volume.volume;
    doc["target"] = data.volume.target;
  }
  // GET_STATUS, MUTE_TOGGLE, SET_DEFAULT_DEVICE have no additional data

  String result;
  serializeJson(doc, result);
  return result;
}

Message Message::fromJson(const String &json) {
  Message msg;
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    LOG_JSON_PARSE_ERROR(TAG, error);
    return msg; // Returns invalid message
  }

  // Parse core fields using safe extraction macros
  String messageTypeStr;
  SAFE_JSON_EXTRACT_STRING(doc, "messageType", messageTypeStr, "");
  msg.type = stringToType(messageTypeStr);

  SAFE_JSON_EXTRACT_STRING(doc, "deviceId", msg.deviceId, "");
  SAFE_JSON_EXTRACT_STRING(doc, "requestId", msg.requestId, "");

  SAFE_JSON_EXTRACT_INT(doc, "timestamp", msg.timestamp, millis());

  // Parse type-specific data using safe macros
  if (msg.type == TYPE_AUDIO_STATUS) {
    SAFE_JSON_EXTRACT_INT(doc, "activeSessionCount",
                          msg.data.audio.activeSessionCount, 0);
    SAFE_JSON_EXTRACT_CSTRING(doc, "reason", msg.data.audio.reason,
                              sizeof(msg.data.audio.reason), "");
    SAFE_JSON_EXTRACT_CSTRING(doc, "originatingRequestId",
                              msg.data.audio.originatingRequestId,
                              sizeof(msg.data.audio.originatingRequestId), "");
    SAFE_JSON_EXTRACT_CSTRING(doc, "originatingDeviceId",
                              msg.data.audio.originatingDeviceId,
                              sizeof(msg.data.audio.originatingDeviceId), "");

    // Parse sessions array
    if (doc.containsKey("sessions") && doc["sessions"].is<JsonArray>()) {
      JsonArray sessions = doc["sessions"].as<JsonArray>();
      msg.data.audio.sessionCount = 0;

      for (JsonVariant sessionVariant : sessions) {
        if (msg.data.audio.sessionCount >= 16)
          break; // Max 16 sessions

        JsonObject session = sessionVariant.as<JsonObject>();
        auto &sessionData =
            msg.data.audio.sessions[msg.data.audio.sessionCount];

        sessionData.processId = session["processId"] | 0;
        SAFE_JSON_EXTRACT_CSTRING(session, "processName",
                                  sessionData.processName,
                                  sizeof(sessionData.processName), "");
        SAFE_JSON_EXTRACT_CSTRING(session, "displayName",
                                  sessionData.displayName,
                                  sizeof(sessionData.displayName), "");
        sessionData.volume = session["volume"] | 0.0f;
        sessionData.isMuted = session["isMuted"] | false;
        SAFE_JSON_EXTRACT_CSTRING(session, "state", sessionData.state,
                                  sizeof(sessionData.state), "");

        msg.data.audio.sessionCount++;
      }
    } else {
      msg.data.audio.sessionCount = 0;
    }

    // Parse defaultDevice object
    if (doc.containsKey("defaultDevice") &&
        doc["defaultDevice"].is<JsonObject>()) {
      JsonObject defaultDevice = doc["defaultDevice"].as<JsonObject>();
      msg.data.audio.hasDefaultDevice = true;

      SAFE_JSON_EXTRACT_CSTRING(
          defaultDevice, "friendlyName",
          msg.data.audio.defaultDevice.friendlyName,
          sizeof(msg.data.audio.defaultDevice.friendlyName), "");
      msg.data.audio.defaultDevice.volume = defaultDevice["volume"] | 0.0f;
      msg.data.audio.defaultDevice.isMuted = defaultDevice["isMuted"] | false;
      SAFE_JSON_EXTRACT_CSTRING(
          defaultDevice, "dataFlow", msg.data.audio.defaultDevice.dataFlow,
          sizeof(msg.data.audio.defaultDevice.dataFlow), "");
      SAFE_JSON_EXTRACT_CSTRING(
          defaultDevice, "deviceRole", msg.data.audio.defaultDevice.deviceRole,
          sizeof(msg.data.audio.defaultDevice.deviceRole), "");
    } else {
      msg.data.audio.hasDefaultDevice = false;
    }
  } else if (msg.type == TYPE_ASSET_REQUEST) {
    SAFE_JSON_EXTRACT_CSTRING(doc, "processName", msg.data.asset.processName,
                              sizeof(msg.data.asset.processName), "");
  } else if (msg.type == TYPE_ASSET_RESPONSE) {
    SAFE_JSON_EXTRACT_CSTRING(doc, "processName", msg.data.asset.processName,
                              sizeof(msg.data.asset.processName), "");
    SAFE_JSON_EXTRACT_BOOL(doc, "success", msg.data.asset.success, false);
    SAFE_JSON_EXTRACT_CSTRING(doc, "errorMessage", msg.data.asset.errorMessage,
                              sizeof(msg.data.asset.errorMessage), "");
    SAFE_JSON_EXTRACT_CSTRING(doc, "assetData", msg.data.asset.assetDataBase64,
                              sizeof(msg.data.asset.assetDataBase64), "");
    SAFE_JSON_EXTRACT_INT(doc, "width", msg.data.asset.width, 0);
    SAFE_JSON_EXTRACT_INT(doc, "height", msg.data.asset.height, 0);
    SAFE_JSON_EXTRACT_CSTRING(doc, "format", msg.data.asset.format,
                              sizeof(msg.data.asset.format), "");
  } else if (msg.type == TYPE_SET_VOLUME || msg.type == TYPE_VOLUME_CHANGE) {
    SAFE_JSON_EXTRACT_CSTRING(doc, "processName", msg.data.volume.processName,
                              sizeof(msg.data.volume.processName), "");
    SAFE_JSON_EXTRACT_INT(doc, "volume", msg.data.volume.volume, 0);
    SAFE_JSON_EXTRACT_CSTRING(doc, "target", msg.data.volume.target,
                              sizeof(msg.data.volume.target), "default");
  }

  return msg;
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

const char *Message::typeToString() const { return type.c_str(); }

String Message::stringToType(const String &str) {
  if (str == "STATUS_MESSAGE" || str == TYPE_AUDIO_STATUS)
    return TYPE_AUDIO_STATUS;
  if (str == "VOLUME_CHANGE" || str == TYPE_VOLUME_CHANGE)
    return TYPE_VOLUME_CHANGE;
  if (str == "MUTE_TOGGLE" || str == TYPE_MUTE_TOGGLE)
    return TYPE_MUTE_TOGGLE;
  if (str == "GET_ASSETS" || str == TYPE_ASSET_REQUEST)
    return TYPE_ASSET_REQUEST;
  if (str == "ASSET_RESPONSE" || str == TYPE_ASSET_RESPONSE)
    return TYPE_ASSET_RESPONSE;
  if (str == "GET_STATUS" || str == TYPE_GET_STATUS)
    return TYPE_GET_STATUS;
  if (str == "SET_VOLUME" || str == TYPE_SET_VOLUME)
    return TYPE_SET_VOLUME;
  if (str == "SET_DEFAULT_DEVICE" || str == TYPE_SET_DEFAULT_DEVICE)
    return TYPE_SET_DEFAULT_DEVICE;
  return TYPE_INVALID;
}

String Message::toString() const {
  String result = "Message[" + type + "]\n";
  result += "  DeviceId: " + deviceId + "\n";
  result += "  RequestId: " + requestId + "\n";
  result += "  Timestamp: " + String(timestamp) + "\n";

  if (type == TYPE_AUDIO_STATUS) {
    result += "  AudioStatus:\n";
    result += "    Sessions: " + String(data.audio.sessionCount) + "\n";
    result +=
        "    ActiveSessions: " + String(data.audio.activeSessionCount) + "\n";

    for (int i = 0; i < data.audio.sessionCount && i < 16; i++) {
      result += "    Session[" + String(i) + "]:\n";
      result +=
          "      ProcessId: " + String(data.audio.sessions[i].processId) + "\n";
      result += "      ProcessName: '" +
                String(data.audio.sessions[i].processName) + "'\n";
      result += "      DisplayName: '" +
                String(data.audio.sessions[i].displayName) + "'\n";
      result += "      Volume: " + String(data.audio.sessions[i].volume) + "\n";
      result += "      Muted: " +
                String(data.audio.sessions[i].isMuted ? "true" : "false") +
                "\n";
      result += "      State: '" + String(data.audio.sessions[i].state) + "'\n";
    }

    if (data.audio.hasDefaultDevice) {
      result += "    DefaultDevice:\n";
      result += "      Name: '" +
                String(data.audio.defaultDevice.friendlyName) + "'\n";
      result +=
          "      Volume: " + String(data.audio.defaultDevice.volume) + "\n";
      result += "      Muted: " +
                String(data.audio.defaultDevice.isMuted ? "true" : "false") +
                "\n";
      result += "      DataFlow: '" +
                String(data.audio.defaultDevice.dataFlow) + "'\n";
      result += "      DeviceRole: '" +
                String(data.audio.defaultDevice.deviceRole) + "'\n";
    }

    if (strlen(data.audio.reason) > 0) {
      result += "    Reason: '" + String(data.audio.reason) + "'\n";
    }
    if (strlen(data.audio.originatingRequestId) > 0) {
      result += "    OriginatingRequestId: '" +
                String(data.audio.originatingRequestId) + "'\n";
    }
    if (strlen(data.audio.originatingDeviceId) > 0) {
      result += "    OriginatingDeviceId: '" +
                String(data.audio.originatingDeviceId) + "'\n";
    }
  } else if (type == TYPE_ASSET_REQUEST) {
    result += "  AssetRequest:\n";
    result += "    ProcessName: '" + String(data.asset.processName) + "'\n";
  } else if (type == TYPE_ASSET_RESPONSE) {
    result += "  AssetResponse:\n";
    result += "    ProcessName: '" + String(data.asset.processName) + "'\n";
    result +=
        "    Success: " + String(data.asset.success ? "true" : "false") + "\n";
    if (!data.asset.success && strlen(data.asset.errorMessage) > 0) {
      result += "    Error: '" + String(data.asset.errorMessage) + "'\n";
    }
    if (data.asset.success) {
      result += "    Dimensions: " + String(data.asset.width) + "x" +
                String(data.asset.height) + "\n";
      result += "    Format: '" + String(data.asset.format) + "'\n";
      result += "    DataSize: " + String(strlen(data.asset.assetDataBase64)) +
                " bytes\n";
    }
  } else if (type == TYPE_SET_VOLUME || type == TYPE_VOLUME_CHANGE) {
    result += "  VolumeChange:\n";
    result += "    ProcessName: '" + String(data.volume.processName) + "'\n";
    result += "    Volume: " + String(data.volume.volume) + "\n";
    result += "    Target: '" + String(data.volume.target) + "'\n";
  } else if (type == TYPE_GET_STATUS) {
    result += "  StatusRequest\n";
  } else if (type == TYPE_MUTE_TOGGLE) {
    result += "  MuteToggle\n";
  } else if (type == TYPE_SET_DEFAULT_DEVICE) {
    result += "  SetDefaultDevice\n";
  } else {
    result += "  Invalid/Unknown message type\n";
  }

  return result;
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
