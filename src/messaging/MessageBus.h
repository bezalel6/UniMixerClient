#ifndef MESSAGE_BUS_H
#define MESSAGE_BUS_H

#include <Arduino.h>
#include <functional>
#include "../../include/MessageProtocol.h"

namespace Messaging {

// Modern callback types
using MessageCallback = std::function<void(const char* topic, const char* payload)>;
using PublishFunction = std::function<bool(const char* topic, const char* payload)>;
using ConnectionFunction = std::function<bool()>;

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

    // Publishing
    static bool Publish(const char* topic, const char* payload);
    static bool PublishDelayed(const char* topic, const char* payload);

    // Status
    static bool IsConnected();
    static ConnectionStatus GetStatus();
    static const char* GetStatusString();

    // Handler management
    static bool RegisterHandler(const Handler& handler);
    static bool UnregisterHandler(const String& identifier);

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