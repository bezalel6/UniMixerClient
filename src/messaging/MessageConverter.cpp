#include "MessageData.h"
#include <MessageProtocol.h>  // Direct import instead of relative path
#include <esp_log.h>

static const char* TAG = "MessageConverter";

namespace Messaging {

// =============================================================================
// EXTERNAL MESSAGE IMPLEMENTATION
// =============================================================================

// parseFromJSON method removed - transport handles parsing

bool ExternalMessage::validate() {
    if (validated) {
        return true;  // Already validated
    }

    // Basic validation checks
    if (messageType == MessageProtocol::ExternalMessageType::INVALID) {
        ESP_LOGW(TAG, "Invalid message type in external message");
        return false;
    }

    // Validate core fields are present
    if (deviceId.isEmpty()) {
        ESP_LOGW(TAG, "Missing deviceId in external message");
        return false;
    }

    // Security validation - check parsed data size
    String serializedCheck;
    serializeJson(parsedData, serializedCheck);
    if (serializedCheck.length() > 8192) {  // Max 8KB per message
        ESP_LOGW(TAG, "External message data too large: %d bytes", serializedCheck.length());
        return false;
    }

    // Check for malicious JSON patterns in device identifiers
    if (deviceId.indexOf("__proto__") >= 0 ||
        deviceId.indexOf("constructor") >= 0 ||
        originatingDeviceId.indexOf("prototype") >= 0) {
        ESP_LOGW(TAG, "Potentially malicious data detected in identifiers");
        return false;
    }

    validated = true;
    ESP_LOGD(TAG, "External message validated successfully: %s",
             MessageProtocol::externalMessageTypeToString(messageType));
    return true;
}

bool ExternalMessage::isSelfOriginated() const {
    return originatingDeviceId == Config::getDeviceId() || deviceId == Config::getDeviceId();
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

std::vector<InternalMessage> MessageConverter::externalToInternal(const ExternalMessage& external) {
    std::vector<InternalMessage> results;

    if (!external.validated) {
        ESP_LOGW(TAG, "Attempting to convert unvalidated external message");
        return results;
    }

    // Convert based on message type
    switch (external.messageType) {
        case MessageProtocol::ExternalMessageType::STATUS_UPDATE: {
            // Audio status update -> Multiple internal messages
            AudioStatusData audioData = parseStatusResponse(external);

            // Create internal message for audio manager
            InternalMessage audioMsg(MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE);
            audioMsg.setTypedData(audioData);
            results.push_back(audioMsg);

            // Create internal UI update message
            InternalMessage uiMsg(MessageProtocol::InternalMessageType::UI_UPDATE);
            results.push_back(uiMsg);
            break;
        }

        case MessageProtocol::ExternalMessageType::ASSET_RESPONSE: {
            // Asset response -> Internal asset message
            InternalMessage msg(MessageProtocol::InternalMessageType::UI_UPDATE);
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

    ESP_LOGD(TAG, "Converted external message to %d internal messages", results.size());
    return results;
}

ExternalMessage MessageConverter::internalToExternal(const InternalMessage& internal) {
    ExternalMessage external(MessageProtocol::ExternalMessageType::STATUS_MESSAGE,
                             Config::generateRequestId(),
                             Config::getDeviceId());
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

    ESP_LOGD(TAG, "Converted internal message to external: %s",
             MessageProtocol::externalMessageTypeToString(external.messageType));

    return external;
}

InternalMessage MessageConverter::createAudioVolumeMessage(const String& processName, int volume) {
    InternalMessage msg(MessageProtocol::InternalMessageType::AUDIO_STATE_UPDATE);

    // Create audio volume data structure (simplified)
    struct AudioVolumeData {
        char processName[64];
        int volume;
    };

    static AudioVolumeData data;
    strncpy(data.processName, processName.c_str(), sizeof(data.processName) - 1);
    data.processName[sizeof(data.processName) - 1] = '\0';
    data.volume = volume;

    msg.data = &data;
    msg.dataSize = sizeof(AudioVolumeData);

    ESP_LOGD(TAG, "Created audio volume message: %s = %d", processName.c_str(), volume);
    return msg;
}

InternalMessage MessageConverter::createUIUpdateMessage(const String& component, const String& data) {
    InternalMessage msg(MessageProtocol::InternalMessageType::UI_UPDATE);

    // Create UI update data structure (simplified)
    struct UIUpdateData {
        char component[32];
        char data[128];
    };

    static UIUpdateData uiData;
    strncpy(uiData.component, component.c_str(), sizeof(uiData.component) - 1);
    uiData.component[sizeof(uiData.component) - 1] = '\0';
    strncpy(uiData.data, data.c_str(), sizeof(uiData.data) - 1);
    uiData.data[sizeof(uiData.data) - 1] = '\0';

    msg.data = &uiData;
    msg.dataSize = sizeof(UIUpdateData);

    ESP_LOGD(TAG, "Created UI update message: %s", component.c_str());
    return msg;
}

InternalMessage MessageConverter::createSystemStatusMessage(const String& status) {
    InternalMessage msg(MessageProtocol::InternalMessageType::MEMORY_STATUS);

    // Create system status data structure (simplified)
    struct SystemStatusData {
        char status[128];
    };

    static SystemStatusData data;
    strncpy(data.status, status.c_str(), sizeof(data.status) - 1);
    data.status[sizeof(data.status) - 1] = '\0';

    msg.data = &data;
    msg.dataSize = sizeof(SystemStatusData);

    ESP_LOGD(TAG, "Created system status message: %s", status.c_str());
    return msg;
}

InternalMessage MessageConverter::createWifiStatusMessage(const String& status, bool connected) {
    InternalMessage msg(MessageProtocol::InternalMessageType::WIFI_STATUS);

    // Create WiFi status data structure
    struct WiFiStatusData {
        char status[64];
        bool connected;
    };

    static WiFiStatusData data;
    strncpy(data.status, status.c_str(), sizeof(data.status) - 1);
    data.status[sizeof(data.status) - 1] = '\0';
    data.connected = connected;

    msg.data = &data;
    msg.dataSize = sizeof(WiFiStatusData);

    ESP_LOGD(TAG, "Created WiFi status message: %s (connected: %s)", status.c_str(), connected ? "yes" : "no");
    return msg;
}

InternalMessage MessageConverter::createNetworkInfoMessage(const String& ssid, const String& ip) {
    InternalMessage msg(MessageProtocol::InternalMessageType::NETWORK_INFO);

    // Create network info data structure
    struct NetworkInfoData {
        char ssid[32];
        char ip[16];
    };

    static NetworkInfoData data;
    strncpy(data.ssid, ssid.c_str(), sizeof(data.ssid) - 1);
    data.ssid[sizeof(data.ssid) - 1] = '\0';
    strncpy(data.ip, ip.c_str(), sizeof(data.ip) - 1);
    data.ip[sizeof(data.ip) - 1] = '\0';

    msg.data = &data;
    msg.dataSize = sizeof(NetworkInfoData);

    ESP_LOGD(TAG, "Created network info message: %s (%s)", ssid.c_str(), ip.c_str());
    return msg;
}

InternalMessage MessageConverter::createSDStatusMessage(const String& status, bool mounted) {
    InternalMessage msg(MessageProtocol::InternalMessageType::SD_STATUS);

    // Create SD status data structure
    struct SDStatusData {
        char status[64];
        bool mounted;
    };

    static SDStatusData data;
    strncpy(data.status, status.c_str(), sizeof(data.status) - 1);
    data.status[sizeof(data.status) - 1] = '\0';
    data.mounted = mounted;

    msg.data = &data;
    msg.dataSize = sizeof(SDStatusData);

    ESP_LOGD(TAG, "Created SD status message: %s (mounted: %s)", status.c_str(), mounted ? "yes" : "no");
    return msg;
}

InternalMessage MessageConverter::createAudioDeviceChangeMessage(const String& deviceName) {
    InternalMessage msg(MessageProtocol::InternalMessageType::AUDIO_DEVICE_CHANGE);

    // Create audio device change data structure
    struct AudioDeviceChangeData {
        char deviceName[64];
    };

    static AudioDeviceChangeData data;
    strncpy(data.deviceName, deviceName.c_str(), sizeof(data.deviceName) - 1);
    data.deviceName[sizeof(data.deviceName) - 1] = '\0';

    msg.data = &data;
    msg.dataSize = sizeof(AudioDeviceChangeData);

    ESP_LOGD(TAG, "Created audio device change message: %s", deviceName.c_str());
    return msg;
}

InternalMessage MessageConverter::createCoreToCoreSyncMessage(uint8_t fromCore, uint8_t toCore) {
    MessageProtocol::InternalMessageType msgType = (toCore == 1) ? MessageProtocol::InternalMessageType::CORE0_TO_CORE1 : MessageProtocol::InternalMessageType::CORE1_TO_CORE0;

    InternalMessage msg(msgType);

    // Create core sync data structure
    struct CoreSyncData {
        uint8_t fromCore;
        uint8_t toCore;
        unsigned long timestamp;
    };

    static CoreSyncData data;
    data.fromCore = fromCore;
    data.toCore = toCore;
    data.timestamp = millis();

    msg.data = &data;
    msg.dataSize = sizeof(CoreSyncData);

    ESP_LOGD(TAG, "Created core sync message: Core %d -> Core %d", fromCore, toCore);
    return msg;
}

}  // namespace Messaging
