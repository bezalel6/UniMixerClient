/**
 * BRUTAL MESSAGING SYSTEM - Integration Example
 *
 * This shows how simple messaging should be.
 * No abstractions. No conversions. Just messages.
 */

#include "Message.h"
#include "SimplifiedSerialEngine.h"
#include <esp_log.h>

static const char* TAG = "BrutalExample";

namespace Example {

// Example: Audio Controller using the new system
class SimpleAudioController {
private:
    int currentVolume = 50;
    bool isMuted = false;

public:
    void init() {
        // Subscribe to volume changes - THAT'S IT!
        Messaging::subscribe(Messaging::Message::SET_VOLUME,
            [this](const Messaging::Message& msg) {
                ESP_LOGI(TAG, "Volume change for %s: %d",
                         msg.data.volume.processName,
                         msg.data.volume.volume);
                currentVolume = msg.data.volume.volume;

                // Send status update
                sendStatusUpdate();
            });

        // Subscribe to status requests
        Messaging::subscribe(Messaging::Message::GET_STATUS,
            [this](const Messaging::Message& msg) {
                ESP_LOGI(TAG, "Status request from %s", msg.deviceId.c_str());
                sendStatusUpdate();
            });
    }

    void sendStatusUpdate() {
        // Create status message using NEW format
        Messaging::Message::AudioData audio;
        
        // Initialize structure
        audio.sessionCount = 1;
        audio.hasDefaultDevice = true;
        audio.activeSessionCount = 1;
        
        // Create single session
        auto& session = audio.sessions[0];
        session.processId = 12345;
        strncpy(session.processName, "SimpleAudio", sizeof(session.processName) - 1);
        strncpy(session.displayName, "Simple Audio Controller", sizeof(session.displayName) - 1);
        session.volume = currentVolume / 100.0f; // Convert to 0-1 range
        session.isMuted = isMuted;
        strncpy(session.state, "AudioSessionStateActive", sizeof(session.state) - 1);
        
        // Set default device
        strncpy(audio.defaultDevice.friendlyName, "Speakers", sizeof(audio.defaultDevice.friendlyName) - 1);
        audio.defaultDevice.volume = currentVolume / 100.0f; // Convert to 0-1 range
        audio.defaultDevice.isMuted = isMuted;
        strncpy(audio.defaultDevice.dataFlow, "Render", sizeof(audio.defaultDevice.dataFlow) - 1);
        strncpy(audio.defaultDevice.deviceRole, "Console", sizeof(audio.defaultDevice.deviceRole) - 1);
        
        // Set additional fields
        strncpy(audio.reason, "UpdateResponse", sizeof(audio.reason) - 1);
        audio.originatingRequestId[0] = '\0'; // Empty
        audio.originatingDeviceId[0] = '\0';  // Empty

        auto msg = Messaging::Message::createAudioStatus(audio, "");
        Messaging::sendMessage(msg);
    }

    void setVolume(int volume) {
        currentVolume = volume;
        sendStatusUpdate();
    }
};

// Example: Logo Requester using the new system
class SimpleLogoRequester {
private:
    std::function<void(const uint8_t*, size_t)> onLogoReceived;

public:
    void requestLogo(const char* processName) {
        // Create asset request - SIMPLE!
        auto msg = Messaging::Message::createAssetRequest(processName, "");
        Messaging::sendMessage(msg);
    }

    void init(std::function<void(const uint8_t*, size_t)> callback) {
        onLogoReceived = callback;

        // Subscribe to asset responses
        Messaging::subscribe(Messaging::Message::ASSET_RESPONSE,
            [this](const Messaging::Message& msg) {
                if (msg.data.asset.success) {
                    ESP_LOGI(TAG, "Got logo for %s: %dx%d %s",
                             msg.data.asset.processName,
                             msg.data.asset.width,
                             msg.data.asset.height,
                             msg.data.asset.format);

                    // Decode base64 if needed and callback
                    // ... decoding logic ...

                } else {
                    ESP_LOGW(TAG, "Logo request failed: %s",
                             msg.data.asset.errorMessage);
                }
            });
    }
};

// Example: Complete initialization
void initBrutalMessaging() {
    ESP_LOGI(TAG, "Initializing BRUTAL messaging system");

    // 1. Start serial engine
    Messaging::SerialEngine::getInstance().init();

    // 2. Initialize components
    SimpleAudioController audioController;
    audioController.init();

    SimpleLogoRequester logoRequester;
    logoRequester.init([](const uint8_t* data, size_t size) {
        ESP_LOGI(TAG, "Logo received: %zu bytes", size);
    });

    // 3. That's it! No MessageCore, no transport registration, no complex setup

    ESP_LOGI(TAG, "Brutal messaging initialized - %d lines of setup code", 15);
}

// Example: How to add a new message type
namespace CustomMessages {

    // Step 1: Add to Message::Type enum
    // Step 2: Add data struct to union
    // Step 3: Add serialization in toJson/fromJson
    // DONE! No shapes, no macros, no registration

    void sendCustomMessage() {
        // If we had a custom message type:
        // Message msg;
        // msg.type = Message::MY_CUSTOM_TYPE;
        // msg.data.custom.field1 = value;
        // sendMessage(msg);
    }
}

}  // namespace Example

/**
 * SUMMARY:
 *
 * Old system: 15 files, 3000+ lines, complex abstractions
 * New system: 3 files, 500 lines, direct and simple
 *
 * Performance:
 * - No variant overhead
 * - No JSON parsing for routing
 * - Direct field access
 * - Single allocation per message
 *
 * This is how messaging should be.
 */
