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
static Handler* FindSerialHandler(const String& topic);
static bool IsSerialAvailable();

// Serial protocol implementation - perfect mapping to MQTT
static Transport SerialTransport = {
    .Publish = [](const char* topic, const char* payload) -> bool {
        if (!IsSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for publishing");
            return false;
        }

        // Format: "TOPIC:PAYLOAD\n" - perfect mapping to MQTT topics
        Hardware::Device::getDataSerial().printf("%s%c%s%c", topic, Protocol::SERIAL_DELIMITER, payload, Protocol::SERIAL_TERMINATOR);
        Hardware::Device::getDataSerial().flush();

        ESP_LOGI(TAG, "Published to serial - Topic: %s, Payload: %s", topic, payload);
        return true;
    },

    .PublishDelayed = [](const char* topic, const char* payload) -> bool {
        // For serial, immediate publishing is fine (no connection state to worry about)
        return SerialTransport.Publish(topic, payload);
    },

    .IsConnected = []() -> bool {
        return IsSerialAvailable();
    },

    .RegisterHandler = [](const Handler& handler) -> bool {
        ESP_LOGI(TAG, "Registering serial handler: %s for topic: %s",
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

    // Check for incoming data on the clean data interface
    USBCDC& dataSerial = Hardware::Device::getDataSerial();
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
            if (incomingBuffer.length() > Protocol::MAX_TOPIC_LENGTH + Protocol::MAX_PAYLOAD_LENGTH + 10) {
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

    // Find delimiter
    int delimiterPos = message.indexOf(Protocol::SERIAL_DELIMITER);

    if (delimiterPos == -1) {
        ESP_LOGW(TAG, "Invalid serial message format (no delimiter): %s", message.c_str());
        return;
    }

    // Extract topic and payload
    String topic = message.substring(0, delimiterPos);
    String payload = message.substring(delimiterPos + 1);

    if (topic.length() == 0) {
        ESP_LOGW(TAG, "Empty topic in serial message");
        return;
    }

    ESP_LOGI(TAG, "Parsed serial message - Topic: %s, Payload: %s", topic.c_str(), payload.c_str());

    // Find appropriate handler
    Handler* handler = FindSerialHandler(topic);

    if (handler && handler->Callback) {
        ESP_LOGI(TAG, "Calling handler %s for topic %s", handler->Identifier.c_str(), topic.c_str());
        handler->Callback(topic.c_str(), payload.c_str());
    } else {
        ESP_LOGW(TAG, "No handler found for serial topic: %s", topic.c_str());
    }
}

static Handler* FindSerialHandler(const String& topic) {
    for (auto& handler : serialHandlers) {
        if (handler.Active && handler.SubscribeTopic == topic) {
            return &handler;
        }
    }
    return nullptr;
}

Transport* GetSerialTransport() {
    return &SerialTransport;
}

}  // namespace Messaging::Transports