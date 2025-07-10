#include "../protocol/MessageData.h"
#include <MessageProtocol.h> // Direct import instead of relative path
#include <esp_log.h>

static const char *TAG = "MessageConverter";

namespace Messaging {

// =============================================================================
// EXTERNAL MESSAGE IMPLEMENTATION
// =============================================================================

bool ExternalMessage::isSelfOriginated() const {
  return originatingDeviceId == Config::getDeviceId() ||
         deviceId == Config::getDeviceId();
}

bool ExternalMessage::requiresResponse() const {
  switch (messageType) {
  case MessageProtocol::ExternalMessageType::GET_STATUS:
  case MessageProtocol::ExternalMessageType::GET_ASSETS:
    return true;
  default:
    return false;
  }
}

// =============================================================================
// MESSAGE CONVERSION IMPLEMENTATION
// =============================================================================

std::vector<InternalMessage>
MessageConverter::externalToInternal(const ExternalMessage &external) {
  std::vector<InternalMessage> results;

  if (!external.validated) {
    ESP_LOGW(TAG, "Attempting to convert unvalidated external message");
    return results;
  }

  // Convert based on message type
  switch (external.messageType) {
    case MessageProtocol::ExternalMessageType::STATUS_MESSAGE:
  case MessageProtocol::ExternalMessageType::STATUS_UPDATE: {
    // Audio status update -> Multiple internal messages
    auto parseResult = MessageParser::parseAudioStatusData(external);
    if (!parseResult.isValid()) {
        ESP_LOGW(TAG, "Failed to parse audio status data: %s", parseResult.getError().c_str());
        break;
    }
    AudioStatusData audioData = parseResult.getValue();

    // Create internal message for audio manager
    InternalMessage audioMsg(
        MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE);
    audioMsg.setTypedData(audioData);
    results.push_back(audioMsg);

    // Create internal UI update message
    InternalMessage uiMsg(MessageProtocol::InternalMessageType::UI_UPDATE);
    results.push_back(uiMsg);
    break;
  }

  case MessageProtocol::ExternalMessageType::ASSET_RESPONSE: {
    // Asset response -> Internal asset response message
    InternalMessage msg(MessageProtocol::InternalMessageType::ASSET_RESPONSE);
    // Extract relevant asset data to avoid static storage issues
    AssetResponseData assetData(external);
    msg.setTypedData(assetData);
    results.push_back(msg);
    break;
  }

  case MessageProtocol::ExternalMessageType::SESSION_UPDATE: {
    // Session update -> Audio UI refresh
    InternalMessage msg(MessageProtocol::InternalMessageType::AUDIO_UI_REFRESH);
    results.push_back(msg);
    break;
  }

  default: {
    // Generic conversion for other message types
    InternalMessage msg(MessageProtocol::InternalMessageType::UI_UPDATE);
    results.push_back(msg);
    break;
  }
  }

  ESP_LOGD(TAG, "Converted external message to %d internal messages",
           results.size());
  return results;
}

ExternalMessage
MessageConverter::internalToExternal(const InternalMessage &internal) {
  ExternalMessage external(MessageProtocol::ExternalMessageType::STATUS_MESSAGE,
                           Config::generateRequestId(), Config::getDeviceId());
  external.timestamp = internal.timestamp;

  // Add type-specific data to parsedData
  switch (internal.messageType) {
  case MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE: {
    external.messageType = MessageProtocol::ExternalMessageType::STATUS_UPDATE;
    // Add audio state data if available
    break;
  }

  case MessageProtocol::InternalMessageType::UI_UPDATE: {
    external.messageType = MessageProtocol::ExternalMessageType::STATUS_MESSAGE;
    // Add UI update data
    break;
  }

  default: {
    // Generic conversion - status message
    external.messageType = MessageProtocol::ExternalMessageType::STATUS_MESSAGE;
    break;
  }
  }

  external.validated = true;

  ESP_LOGD(TAG, "Converted internal message to external: %d",
           LOG_EXTERNAL_MSG_TYPE(external.messageType));

  return external;
}

} // namespace Messaging
