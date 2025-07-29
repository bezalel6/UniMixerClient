#pragma once

#include <Arduino.h>
#include <StringAbstraction.h>
#include <esp_log.h>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Messaging {

/**
 * BRUTALLY SIMPLE MESSAGE SYSTEM
 * No abstractions. No variants. No shapes. Just data.
 */
struct Message {
    // String-based message types instead of enums
    static const char *TYPE_INVALID;
    static const char *TYPE_AUDIO_STATUS;
    static const char *TYPE_VOLUME_CHANGE;
    static const char *TYPE_MUTE_TOGGLE;
    static const char *TYPE_ASSET_REQUEST;
    static const char *TYPE_ASSET_RESPONSE;
    static const char *TYPE_ASSET_LOCAL_REF;
    static const char *TYPE_GET_STATUS;
    static const char *TYPE_SET_VOLUME;
    static const char *TYPE_SET_DEFAULT_DEVICE;

    // Core fields every message has
    String type = TYPE_INVALID;
    String deviceId;
    String requestId;
    uint32_t timestamp = 0;

    // Simple payload data - NEW FORMAT for complex audio status
    struct SessionData {
        int processId;
        char processName[64];
        char displayName[64];
        float volume;
        bool isMuted;
        char state[32];
    };

    struct DefaultDeviceData {
        char friendlyName[128];
        float volume;
        bool isMuted;
        char dataFlow[16];
        char deviceRole[16];
    };

    struct AudioData {
        SessionData sessions[16];  // Max 16 sessions
        int sessionCount;
        DefaultDeviceData defaultDevice;
        bool hasDefaultDevice;
        int activeSessionCount;
        char reason[32];
        char originatingRequestId[64];
        char originatingDeviceId[64];
    };

    struct AssetData {
        char processName[64];
        bool success;
        char errorMessage[128];
        char *assetDataBase64;   // Pointer to shared buffer
        size_t assetDataLength;  // Track actual data length
        int width;
        int height;
        char format[16];
    };

    struct VolumeData {
        char processName[64];
        int volume;
        char target[64];  // "default" or specific device
    };

    struct LocalAssetRef {
        char processName[64];    // Process requesting the asset
        char localName[128];     // Name/path relative to logos directory
        bool exists;             // Whether the asset exists locally
        char errorMessage[128];  // Error if not found
    };

    // Tagged union for payload
    union {
        AudioData audio;
        AssetData asset;
        VolumeData volume;
        LocalAssetRef localAsset;
    } data;

    // Constructor - manually initialize the data
    Message() : type(TYPE_INVALID), timestamp(0) { initializeAudioData(); }

    Message(const String &messageType) : type(messageType), timestamp(0) {
        if (messageType == TYPE_AUDIO_STATUS) {
            initializeAudioData();
        } else if (messageType == TYPE_ASSET_REQUEST ||
                   messageType == TYPE_ASSET_RESPONSE) {
            initializeAssetData();
        } else if (messageType == TYPE_ASSET_LOCAL_REF) {
            initializeLocalAssetData();
        } else if (messageType == TYPE_SET_VOLUME ||
                   messageType == TYPE_VOLUME_CHANGE) {
            initializeVolumeData();
        } else {
            initializeAudioData();
        }
    }

    // Helper functions to initialize union members
    void initializeAudioData() {
        memset(&data.audio, 0, sizeof(AudioData));
        data.audio.sessionCount = 0;
        data.audio.hasDefaultDevice = false;
        data.audio.activeSessionCount = 0;
    }

    void initializeAssetData() {
        memset(&data.asset, 0, sizeof(AssetData));
        data.asset.assetDataBase64 = nullptr;
        data.asset.assetDataLength = 0;
    }

    void initializeVolumeData() { memset(&data.volume, 0, sizeof(VolumeData)); }

    void initializeLocalAssetData() { memset(&data.localAsset, 0, sizeof(LocalAssetRef)); }

    // Constructors for common messages
    static Message createStatusRequest(const String &deviceId);
    static Message createAssetRequest(const String &processName,
                                      const String &deviceId);
    static Message createVolumeChange(const String &processName, int volume,
                                      const String &deviceId);
    static Message createAudioStatus(const AudioData &audioData,
                                     const String &deviceId);
    static Message createAssetResponse(const AssetData &assetData,
                                       const String &requestId,
                                       const String &deviceId);
    static Message createLocalAssetRef(const String &processName,
                                       const String &localName,
                                       bool exists,
                                       const String &requestId,
                                       const String &deviceId);

    // Direct JSON serialization (no shapes, no macros)
    String toJson() const;
    static Message fromJson(const String &json);

    // STREAMLINED MESSAGE SENDING - Send this message via SerialEngine
    void send() const;

    // Utility
    const char *typeToString() const;
    static String stringToType(const String &str);
    String toString() const;
    bool isValid() const { return type != TYPE_INVALID; }

   private:
    // Static buffer for asset data - shared across all messages
    static char sharedAssetBuffer[];

    // Get pointer to shared static buffer
    static char *getAssetBuffer() {
        return sharedAssetBuffer;
    }
};

/**
 * SIMPLE MESSAGE ROUTER
 * No MessageCore complexity. Just route messages to handlers.
 */
class MessageRouter {
   private:
    struct HandlerEntry {
        String type;
        std::function<void(const Message &)> handler;
    };
    std::vector<HandlerEntry> handlers;
    static MessageRouter *instance;

   public:
    static MessageRouter &getInstance() {
        if (!instance) {
            instance = new MessageRouter();
        }
        return *instance;
    }

    // Subscribe to message type
    void subscribe(const String &type,
                   std::function<void(const Message &)> handler) {
        handlers.push_back({type, handler});
    }

    // Route incoming message to handlers
    void route(const Message &msg) {
        if (!msg.isValid()) {
            ESP_LOGW("Router", "Invalid message type");
            return;
        }

        for (auto &entry : handlers) {
            if (entry.type == msg.type) {
                entry.handler(msg);
            }
        }
    }

    // Send message out via serial
    void send(const Message &msg);

    // Get handler count for status
    size_t getHandlerCount() const { return handlers.size(); }
};

// =============================================================================
// INLINE HELPERS
// =============================================================================

inline void sendMessage(const Message &msg) {
    MessageRouter::getInstance().send(msg);
}

inline void subscribe(const String &type,
                      std::function<void(const Message &)> handler) {
    MessageRouter::getInstance().subscribe(type, handler);
}

}  // namespace Messaging
