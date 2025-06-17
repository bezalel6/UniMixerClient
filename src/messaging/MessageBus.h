#ifndef MESSAGE_BUS_H
#define MESSAGE_BUS_H

#include <Arduino.h>
#include <functional>
#include "../../include/MessageProtocol.h"
#include "Messages.h"

namespace Messaging {

// Modern callback types
using MessageCallback = std::function<void(const char* topic, const char* payload)>;
using PublishFunction = std::function<bool(const char* topic, const char* payload)>;
using ConnectionFunction = std::function<bool()>;

// Typed callback types
template<typename T>
using TypedMessageCallback = std::function<void(const T& message)>;

// Connection status enum
enum class ConnectionStatus {
    Disconnected = 0,
    Connecting,
    Connected,
    Failed,
    Error
};

// Handler structure (modern)
struct Handler {
    String Identifier;
    String SubscribeTopic;
    String PublishTopic;
    MessageCallback Callback;
    bool Active = true;
};

// Transport interface (modern)
struct Transport {
    PublishFunction Publish;
    PublishFunction PublishDelayed;
    ConnectionFunction IsConnected;
    std::function<bool(const Handler& handler)> RegisterHandler;
    std::function<bool(const String& identifier)> UnregisterHandler;
    std::function<void()> Update;
    std::function<ConnectionStatus()> GetStatus;
    std::function<const char*()> GetStatusString;
    std::function<void()> Init;
    std::function<void()> Deinit;
};

class MessageBus {
   public:
    // Initialization and control
    static bool Init();
    static void Deinit();
    static void Update();

    // Publishing (existing string-based methods)
    static bool Publish(const char* topic, const char* payload);
    static bool PublishDelayed(const char* topic, const char* payload);

    // Typed publishing methods
    template<typename T>
    static bool PublishTyped(const char* topic, const T& message) {
        static_assert(std::is_base_of<Messages::BaseMessage, T>::value, "T must derive from BaseMessage");
        String payload = message.toJson();
        return Publish(topic, payload.c_str());
    }

    template<typename T>
    static bool PublishTypedDelayed(const char* topic, const T& message) {
        static_assert(std::is_base_of<Messages::BaseMessage, T>::value, "T must derive from BaseMessage");
        String payload = message.toJson();
        return PublishDelayed(topic, payload.c_str());
    }

    // Convenience methods for specific message types
    static bool PublishAudioStatusRequest(const Messages::AudioStatusRequest& request);
    static bool PublishAudioStatusResponse(const Messages::AudioStatusResponse& response);

    // Status
    static bool IsConnected();
    static ConnectionStatus GetStatus();
    static const char* GetStatusString();

    // Handler management (existing string-based methods)
    static bool RegisterHandler(const Handler& handler);
    static bool UnregisterHandler(const String& identifier);

    // Typed handler registration methods
    template<typename T>
    static bool RegisterTypedHandler(const char* topic, const String& identifier, TypedMessageCallback<T> callback) {
        static_assert(std::is_base_of<Messages::BaseMessage, T>::value, "T must derive from BaseMessage");
        
        Handler handler;
        handler.Identifier = identifier;
        handler.SubscribeTopic = topic;
        handler.PublishTopic = "";
        handler.Active = true;
        
        // Create wrapper callback that deserializes JSON to typed message
        handler.Callback = [callback](const char* topic, const char* payload) {
            T message = T::fromJson(payload);
            callback(message);
        };
        
        return RegisterHandler(handler);
    }

    // Convenience methods for specific message type handlers
    static bool RegisterAudioStatusHandler(const String& identifier, TypedMessageCallback<Messages::AudioStatusResponse> callback);

    // Transport selection
    static void SetTransport(Transport* transport);
    static void EnableMqttTransport();
    static void EnableSerialTransport();
    static void EnableBothTransports();

    // Utility
    static unsigned long GetLastActivity();

   private:
    static Transport* primaryTransport;
    static Transport* secondaryTransport;
    static bool dualTransportMode;
    static unsigned long lastActivity;
    static bool initialized;

    // Internal helpers
    static bool PublishToTransport(Transport* transport, const char* topic, const char* payload);
    static void UpdateActivity();
};

// Forward declarations for transport creation
namespace Transports {
Transport* GetMqttTransport();
Transport* GetSerialTransport();
}  // namespace Transports

}  // namespace Messaging

#endif  // MESSAGE_BUS_H