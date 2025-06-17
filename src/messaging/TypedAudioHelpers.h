#ifndef TYPED_AUDIO_HELPERS_H
#define TYPED_AUDIO_HELPERS_H

#include "MessageBus.h"
#include "Messages.h"
#include "../application/AudioTypes.h"

namespace Messaging {
namespace AudioHelpers {

// Helper functions that demonstrate the new typed message system
// These can be used to gradually migrate from the current string-based system

// Publish audio status request using typed system
inline bool PublishStatusRequest() {
    Messages::AudioStatusRequest request;
    // request.requestId is automatically generated
    return MessageBus::PublishAudioStatusRequest(request);
}

// Publish audio status request with delayed delivery
inline bool PublishStatusRequestDelayed() {
    Messages::AudioStatusRequest request;
    return MessageBus::PublishTypedDelayed("STATUS_REQUEST", request);
}
// Convert existing AudioStatus to typed message (for migration)
inline Messages::AudioStatusResponse AudioStatusToMessage(const Application::Audio::AudioStatus& status) {
    Messages::AudioStatusResponse message;
    message.timestamp = status.timestamp;
    message.sessions = status.audioLevels;
    message.defaultDevice = status.defaultDevice;
    message.hasDefaultDevice = status.hasDefaultDevice;
    message.requestId = Messaging::Protocol::generateRequestId();
    return message;
}

// Publish status update using typed system (for device changes)
inline bool PublishStatusUpdate(const Application::Audio::AudioStatus& status) {
    Messages::AudioStatusResponse message = AudioStatusToMessage(status);
    return MessageBus::PublishAudioStatusResponse(message);
}


// Convert typed message to existing AudioStatus (for migration)
inline Application::Audio::AudioStatus MessageToAudioStatus(const Messages::AudioStatusResponse& message) {
    Application::Audio::AudioStatus status;
    status.timestamp = message.timestamp;
    status.audioLevels = message.sessions;
    status.defaultDevice = message.defaultDevice;
    status.hasDefaultDevice = message.hasDefaultDevice;
    return status;
}

} // namespace AudioHelpers
} // namespace Messaging

#endif // TYPED_AUDIO_HELPERS_H 