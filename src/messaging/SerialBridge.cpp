#include "SerialBridge.h"
#include "MessageAPI.h"
#include "MessageConfig.h"
#include "../hardware/DeviceManager.h"
#include "../include/MessagingConfig.h"
#include <ArduinoJson.h>
#include <esp_log.h>

namespace Messaging {

static const char* TAG = "SerialBridge";

/**
 * Simple Serial Bridge for MessageAPI
 *
 * This replaces the complex SerialTransport with a minimal bridge
 * that integrates serial communication with the new MessageAPI.
 */
class SerialBridge {
   public:
    static SerialBridge& getInstance() {
        static SerialBridge instance;
        return instance;
    }

    bool init() {
        if (initialized) {
            return true;
        }

        ESP_LOGI(TAG, "Initializing Serial Bridge");

        // Register serial transport with MessageAPI
        MessageAPI::registerSerialTransport(
            // Send function
            [](const String& topic, const String& payload) -> bool {
                return SerialBridge::getInstance().sendMessage(topic, payload);
            },
            // IsConnected function
            []() -> bool {
                return Hardware::Device::isDataSerialAvailable();
            },
            // Update function
            []() -> void {
                SerialBridge::getInstance().update();
            });

        // Set up serial receive handling
        if (Hardware::Device::isDataSerialAvailable()) {
            HardwareSerial& serial = Hardware::Device::getDataSerial();
            serial.onReceive([this]() {
                this->onSerialReceive();
            });
        }

        initialized = true;
        ESP_LOGI(TAG, "Serial Bridge initialized");
        return true;
    }

    void deinit() {
        if (!initialized) {
            return;
        }

        ESP_LOGI(TAG, "Deinitializing Serial Bridge");

        // Unregister from MessageAPI
        MessageAPI::unregisterTransport(Config::TRANSPORT_NAME_SERIAL);

        // Clear receive callback
        if (Hardware::Device::isDataSerialAvailable()) {
            HardwareSerial& serial = Hardware::Device::getDataSerial();
            serial.onReceive(nullptr);
        }

        initialized = false;
    }

    void update() {
        if (!initialized) {
            return;
        }

        // Process any pending received data
        processIncomingData();
    }

   private:
    bool initialized = false;
    String receiveBuffer = "";
    volatile bool newDataAvailable = false;

    bool sendMessage(const String& topic, const String& payload) {
        if (!Hardware::Device::isDataSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for sending");
            return false;
        }

        // Simple protocol: send JSON payload followed by newline
        HardwareSerial& serial = Hardware::Device::getDataSerial();
        serial.println(payload);
        serial.flush();

        ESP_LOGI(TAG, "Serial TX: %d chars", payload.length());
        return true;
    }

    void onSerialReceive() {
        newDataAvailable = true;
    }

    void processIncomingData() {
        if (!newDataAvailable || !Hardware::Device::isDataSerialAvailable()) {
            return;
        }

        newDataAvailable = false;

        HardwareSerial& serial = Hardware::Device::getDataSerial();

        // Read available data
        while (serial.available()) {
            char c = serial.read();

            if (c == '\n') {
                // Complete message received
                if (receiveBuffer.length() > 0) {
                    processReceivedMessage(receiveBuffer);
                    receiveBuffer = "";
                }
            } else if (c != '\r') {
                receiveBuffer += c;

                // Prevent buffer overflow
                if (receiveBuffer.length() > MESSAGING_MAX_PAYLOAD_LENGTH) {
                    ESP_LOGW(TAG, "Serial buffer overflow, clearing");
                    receiveBuffer = "";
                }
            }
        }
    }

    void processReceivedMessage(const String& message) {
        ESP_LOGI(TAG, "Serial RX: %d chars", message.length());

        // Determine topic based on message content
        String topic = Config::TOPIC_AUDIO_STATUS_RESPONSE;  // Default topic

        // Check if it looks like an audio status message
        if (message.indexOf(Config::AUDIO_MESSAGE_KEYWORD_SESSIONS) >= 0 ||
            message.indexOf(Config::AUDIO_MESSAGE_KEYWORD_DEFAULT_DEVICE) >= 0) {
            topic = Config::TOPIC_AUDIO_STATUS_RESPONSE;
        }

        // Forward to MessageAPI
        MessageAPI::handleIncomingMessage(topic, message);
    }
};

// Public interface functions
namespace Serial {

bool init() {
    return SerialBridge::getInstance().init();
}

void deinit() {
    SerialBridge::getInstance().deinit();
}

void update() {
    SerialBridge::getInstance().update();
}

}  // namespace Serial

}  // namespace Messaging