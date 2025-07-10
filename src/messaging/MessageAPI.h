#pragma once

#include "system/MessageCore.h"
#include "protocol/MessageData.h"

namespace Messaging {

/**
 * High-level messaging API for application layer
 * Provides simplified access to messaging functionality
 */
class MessageAPI {
public:
    // === CORE MANAGEMENT ===
    static bool initialize();
    static void shutdown();
    static bool isHealthy();
    static void update();

    // === TRANSPORT MANAGEMENT ===
    static void registerSerialTransport(
        std::function<bool(const String&)> sendFunction,
        std::function<bool()> isConnectedFunction,
        std::function<void()> updateFunction
    );

    // === EXTERNAL MESSAGE PUBLISHING ===
    static bool publishExternal(const ExternalMessage& message);
    static bool requestAudioStatus();

    // === INTERNAL MESSAGE SUBSCRIPTION ===
    static void subscribeToInternal(
        MessageProtocol::InternalMessageType messageType,
        std::function<void(const InternalMessage&)> callback
    );

    static void subscribeToAllInternal(
        std::function<void(const InternalMessage&)> callback
    );

    // === INTERNAL MESSAGE PUBLISHING ===
    static bool publishInternal(const InternalMessage& message);
    static bool publishWifiStatus(const String& status, bool connected);
    static bool publishNetworkInfo(const String& ssid, const String& ip);
    static bool publishSDStatus(const String& status, bool mounted);
    static bool publishAudioDeviceChange(const String& deviceName);
    static bool publishUIUpdate(const String& component, const String& data);
    static bool publishSystemStatus(const String& status);
    static bool publishDebugUILog(const String& logMessage);

    // === MESSAGE PARSING (Updated for new ParseResult API) ===
    static ParseResult<ExternalMessage> parseExternalMessage(const String& jsonPayload);
    static ParseResult<MessageProtocol::ExternalMessageType> parseExternalMessageType(const String& jsonPayload);

    // === MESSAGE CREATION (Updated for new ParseResult API) ===
    static ParseResult<String> createStatusResponse(const AudioStatusData& data);
    static ParseResult<String> createAssetRequest(const String& processName, const String& deviceId = "");

    // === AUDIO DATA PARSING (Updated for new ParseResult API) ===
    static ParseResult<AudioStatusData> parseAudioStatus(const ExternalMessage& message);

    // === STATISTICS AND STATUS ===
    static String getStats();
    static String getTransportStatus();

private:
    static bool initialized;
    static MessageCore* messageCore;

    // Helper functions
    static MessageCore& getMessageCore();
    static bool ensureInitialized();
};

} // namespace Messaging
