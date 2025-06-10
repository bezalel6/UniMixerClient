#include "MessageBus.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>
#include <vector>
#include <ArduinoJson.h>

static const char* TAG = "SerialTransport";

namespace Messaging::Transports {

// Handler storage for serial message routing
static std::vector<Handler> serialHandlers;
static bool serialInitialized = false;
static unsigned long lastSerialCheck = 0;
static String incomingBuffer = "";

// Helper functions
static void ProcessIncomingSerial();
static void ParseSerialMessage(const String& message);
static Handler* FindSerialHandler(const String& messageType);
static bool IsSerialAvailable();

// Serial protocol implementation - updated for new CMD/STATUS/RESULT format
static Transport SerialTransport = {
    .Publish = [](const char* messageType, const char* payload) -> bool {
        if (!IsSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for publishing");
            return false;
        }

        // New format: "CMD:{json}\n" for commands
        String message = String(Protocol::CMD_PREFIX) + String(payload) + String(Protocol::SERIAL_TERMINATOR);

        Hardware::Device::getDataSerial().print(message);
        Hardware::Device::getDataSerial().flush();

        ESP_LOGI(TAG, "Published command via serial: %s", payload);
        return true;
    },

    .PublishDelayed = [](const char* messageType, const char* payload) -> bool {
        // For serial, immediate publishing is fine (no connection state to worry about)
        return SerialTransport.Publish(messageType, payload);
    },

    .IsConnected = []() -> bool {
        return IsSerialAvailable();
    },

    .RegisterHandler = [](const Handler& handler) -> bool {
        ESP_LOGI(TAG, "Registering serial handler: %s for type: %s",
                 handler.Identifier.c_str(), handler.SubscribeTopic.c_str());

        // Check if handler already exists
        for (const auto& existingHandler : serialHandlers) {
            if (existingHandler.Identifier == handler.Identifier) {
                ESP_LOGW(TAG, "Handler %s already registered", handler.Identifier.c_str());
                return false;
            }
        }

        // Add to our handler list
        serialHandlers.push_back(handler);
        ESP_LOGI(TAG, "Successfully registered serial handler: %s", handler.Identifier.c_str());
        return true;
    },

    .UnregisterHandler = [](const String& identifier) -> bool {
        ESP_LOGI(TAG, "Unregistering serial handler: %s", identifier.c_str());

        for (auto it = serialHandlers.begin(); it != serialHandlers.end(); ++it) {
            if (it->Identifier == identifier) {
                serialHandlers.erase(it);
                ESP_LOGI(TAG, "Successfully unregistered serial handler: %s", identifier.c_str());
                return true;
            }
        }

        ESP_LOGW(TAG, "Handler not found: %s", identifier.c_str());
        return false;
    },

    .Update = []() -> void {
        if (!serialInitialized) {
            return;
        }

        // Process incoming serial messages
        ProcessIncomingSerial();
    },

    .GetStatus = []() -> ConnectionStatus {
        return IsSerialAvailable() ? ConnectionStatus::Connected : ConnectionStatus::Disconnected;
    },

    .GetStatusString = []() -> const char* {
        return IsSerialAvailable() ? "Connected" : "Disconnected";
    },

    .Init = []() -> void {
        ESP_LOGI(TAG, "Initializing Serial transport");

        // Serial is already initialized by DeviceManager, so we just set our flag
        serialInitialized = true;
        lastSerialCheck = millis();
        incomingBuffer = "";
        serialHandlers.clear();

        // Check if data serial is available
        bool dataAvailable = IsSerialAvailable();
        ESP_LOGI(TAG, "Serial transport initialized - Data serial available: %s",
                 dataAvailable ? "true" : "false");
    },

    .Deinit = []() -> void {
        ESP_LOGI(TAG, "Deinitializing Serial transport");

        serialInitialized = false;
        serialHandlers.clear();
        incomingBuffer = "";

        ESP_LOGI(TAG, "Serial transport deinitialized");
    }};

// Helper function implementations
static bool IsSerialAvailable() {
    return Hardware::Device::isDataSerialAvailable();
}

static void ProcessIncomingSerial() {
    unsigned long now = millis();

    // Check for incoming data on the serial interface
    HardwareSerial& dataSerial = Hardware::Device::getDataSerial();
    while (dataSerial.available() > 0) {
        char c = dataSerial.read();

        if (c == Protocol::SERIAL_TERMINATOR) {
            // Complete message received
            if (incomingBuffer.length() > 0) {
                ParseSerialMessage(incomingBuffer);
                incomingBuffer = "";
            }
        } else if (c != '\r') {  // Ignore carriage returns
            incomingBuffer += c;

            // Prevent buffer overflow
            if (incomingBuffer.length() > Protocol::MAX_PAYLOAD_LENGTH + 20) {
                ESP_LOGW(TAG, "Serial buffer overflow, clearing");
                incomingBuffer = "";
            }
        }

        lastSerialCheck = now;
    }

    // Clear stale buffer data
    if (incomingBuffer.length() > 0 && (now - lastSerialCheck) > Protocol::SERIAL_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Serial buffer timeout, clearing stale data");
        incomingBuffer = "";
    }
}

static void ParseSerialMessage(const String& message) {
    ESP_LOGI(TAG, "Processing serial message: %s", message.c_str());

    String messageType = "";
    String payload = "";

    // Parse message format: PREFIX:JSON
    if (message.startsWith(Protocol::STATUS_PREFIX)) {
        messageType = "STATUS";
        payload = message.substring(strlen(Protocol::STATUS_PREFIX));
    } else if (message.startsWith(Protocol::RESULT_PREFIX)) {
        messageType = "RESULT";
        payload = message.substring(strlen(Protocol::RESULT_PREFIX));
    } else {
        ESP_LOGW(TAG, "Unknown message format: %s", message.c_str());
        return;
    }

    if (payload.length() == 0) {
        ESP_LOGW(TAG, "Empty payload in message");
        return;
    }

    ESP_LOGI(TAG, "Parsed message - Type: %s, Payload: %s", messageType.c_str(), payload.c_str());

    // Find appropriate handler
    Handler* handler = FindSerialHandler(messageType);

    if (handler && handler->Callback) {
        ESP_LOGI(TAG, "Calling handler %s for message type %s", handler->Identifier.c_str(), messageType.c_str());
        handler->Callback(messageType.c_str(), payload.c_str());
    } else {
        ESP_LOGW(TAG, "No handler found for message type: %s", messageType.c_str());
    }
}

static Handler* FindSerialHandler(const String& messageType) {
    for (auto& handler : serialHandlers) {
        if (handler.Active && handler.SubscribeTopic == messageType) {
            return &handler;
        }
    }
    return nullptr;
}

// Transport getter
Transport* GetSerialTransport() {
    return &SerialTransport;
}

}  // namespace Messaging::Transports