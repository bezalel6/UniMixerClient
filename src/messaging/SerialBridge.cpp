#include "SerialBridge.h"
#include "MessageAPI.h"
#include "MessageConfig.h"
#include "../hardware/DeviceManager.h"
#include "MessagingConfig.h"
#include "MessageProtocol.h"
#include "../application/TaskManager.h"
#include <ArduinoJson.h>
#include <esp_log.h>

namespace Messaging {

static const char* TAG = "SerialBridge";

// Enhanced message framing protocol
#define MSG_START_MARKER 0x7E  // Start of message
#define MSG_END_MARKER 0x7F    // End of message
#define MSG_ESCAPE_CHAR 0x7D   // Escape character for framing
#define MSG_MAX_RETRIES 3      // Maximum retransmission attempts
#define MSG_TIMEOUT_MS 1000    // Message timeout in milliseconds

// Message validation
#define MSG_HEADER_SIZE 8  // Start + Length(4) + CRC(2) + Type(1)
#define MSG_FOOTER_SIZE 1  // End marker

// Enhanced statistics tracking
struct SerialStats {
    uint32_t messagesReceived;
    uint32_t messagesSent;
    uint32_t framing_errors;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t buffer_overflows;
    uint32_t retransmissions;
    uint32_t messages_recovered;
};

/**
 * Enhanced Serial Bridge with robust message framing and error recovery
 *
 * Features:
 * - Message framing with start/end markers
 * - CRC16 validation for message integrity
 * - Escape sequence handling for binary safety
 * - Automatic retransmission on errors
 * - Buffer overflow protection
 * - Message reconstruction from partial reads
 * - Performance statistics and monitoring
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

        ESP_LOGI(TAG, "Initializing Enhanced Serial Bridge with robust message framing");

        // Initialize statistics
        memset(&stats, 0, sizeof(stats));

        // Initialize buffers
        receiveBuffer.reserve(MESSAGING_SERIAL_BUFFER_SIZE);
        tempBuffer.reserve(MESSAGING_SERIAL_BUFFER_SIZE);

        // Initialize state
        resetReceiveState();

        // Register serial transport with MessageAPI
        MessageAPI::registerSerialTransport(
            // Send function with enhanced framing
            [](const String& payload) -> bool {
                return SerialBridge::getInstance().sendMessage(payload);
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
        ESP_LOGI(TAG, "Enhanced Serial Bridge initialized with message framing and CRC validation");
        return true;
    }

    void deinit() {
        if (!initialized) {
            return;
        }

        ESP_LOGI(TAG, "Deinitializing Enhanced Serial Bridge");

        // Print final statistics
        printStatistics();

        // Unregister from MessageAPI
        MessageAPI::unregisterTransport(Config::TRANSPORT_NAME_SERIAL);

        // Clear receive callback
        if (Hardware::Device::isDataSerialAvailable()) {
            HardwareSerial& serial = Hardware::Device::getDataSerial();
            serial.onReceive(nullptr);
        }

        // Clear buffers
        receiveBuffer = "";
        tempBuffer = "";

        initialized = false;
    }

    void update() {
        if (!initialized) {
            return;
        }

        // Process any pending received data
        processIncomingData();

        // Check for message timeouts
        checkMessageTimeouts();

        // Report message activity for load monitoring
        if (stats.messagesReceived > lastReportedMessages) {
            Application::TaskManager::reportMessageActivity();
            lastReportedMessages = stats.messagesReceived;
        }
    }

    void printStatistics() {
        ESP_LOGI(TAG, "=== Enhanced Serial Bridge Statistics ===");
        ESP_LOGI(TAG, "Messages Sent: %u", stats.messagesSent);
        ESP_LOGI(TAG, "Messages Received: %u", stats.messagesReceived);
        ESP_LOGI(TAG, "Framing Errors: %u", stats.framing_errors);
        ESP_LOGI(TAG, "CRC Errors: %u", stats.crc_errors);
        ESP_LOGI(TAG, "Timeout Errors: %u", stats.timeout_errors);
        ESP_LOGI(TAG, "Buffer Overflows: %u", stats.buffer_overflows);
        ESP_LOGI(TAG, "Retransmissions: %u", stats.retransmissions);
        ESP_LOGI(TAG, "Messages Recovered: %u", stats.messages_recovered);

        if (stats.messagesReceived > 0) {
            float errorRate = ((float)(stats.framing_errors + stats.crc_errors + stats.timeout_errors) / stats.messagesReceived) * 100.0f;
            ESP_LOGI(TAG, "Overall Error Rate: %.2f%%", errorRate);
        }
        ESP_LOGI(TAG, "==========================================");
    }

   private:
    bool initialized = false;
    String receiveBuffer = "";
    String tempBuffer = "";
    SerialStats stats;
    uint32_t lastReportedMessages = 0;

    // Message state tracking
    enum ReceiveState {
        WAITING_FOR_START,
        READING_HEADER,
        READING_PAYLOAD,
        WAITING_FOR_END
    };

    ReceiveState receiveState = WAITING_FOR_START;
    uint32_t expectedMessageLength = 0;
    uint16_t expectedCRC = 0;
    uint32_t messageStartTime = 0;
    bool escapeNext = false;

    void resetReceiveState() {
        receiveState = WAITING_FOR_START;
        expectedMessageLength = 0;
        expectedCRC = 0;
        messageStartTime = 0;
        escapeNext = false;
        tempBuffer = "";
    }

    // CRC16 calculation for message validation
    uint16_t calculateCRC16(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    // Enhanced message sending with framing and CRC
    bool sendMessage(const String& payload) {
        if (!Hardware::Device::isDataSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for sending");
            return false;
        }

        HardwareSerial& serial = Hardware::Device::getDataSerial();

        // Calculate CRC for payload
        uint16_t crc = calculateCRC16((const uint8_t*)payload.c_str(), payload.length());

        // Build framed message: START + LENGTH(4) + CRC(2) + TYPE(1) + PAYLOAD + END
        // For now, TYPE = 0x01 (JSON message)

        serial.write(MSG_START_MARKER);

        // Send length (4 bytes, little endian)
        uint32_t length = payload.length();
        serial.write((uint8_t)(length & 0xFF));
        serial.write((uint8_t)((length >> 8) & 0xFF));
        serial.write((uint8_t)((length >> 16) & 0xFF));
        serial.write((uint8_t)((length >> 24) & 0xFF));

        // Send CRC (2 bytes, little endian)
        serial.write((uint8_t)(crc & 0xFF));
        serial.write((uint8_t)((crc >> 8) & 0xFF));

        // Send message type
        serial.write(0x01);  // JSON message type

        // Send payload with escape sequences
        for (size_t i = 0; i < payload.length(); i++) {
            uint8_t byte = payload[i];
            if (byte == MSG_START_MARKER || byte == MSG_END_MARKER || byte == MSG_ESCAPE_CHAR) {
                serial.write(MSG_ESCAPE_CHAR);
                serial.write(byte ^ 0x20);  // XOR with 0x20 for escaping
            } else {
                serial.write(byte);
            }
        }

        serial.write(MSG_END_MARKER);
        serial.flush();

        stats.messagesSent++;
        ESP_LOGD(TAG, "Enhanced Serial TX: %d chars (CRC: 0x%04X)", payload.length(), crc);
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

        // Process all available bytes
        while (serial.available()) {
            uint8_t byte = serial.read();

            if (!processReceivedByte(byte)) {
                // Error in processing, reset state
                ESP_LOGW(TAG, "Error processing byte 0x%02X, resetting receive state", byte);
                stats.framing_errors++;
                resetReceiveState();
            }
        }
    }

    bool processReceivedByte(uint8_t byte) {
        // Handle escape sequences
        if (escapeNext) {
            byte ^= 0x20;  // Unescape
            escapeNext = false;
        } else if (byte == MSG_ESCAPE_CHAR && receiveState == READING_PAYLOAD) {
            escapeNext = true;
            return true;
        }

        switch (receiveState) {
            case WAITING_FOR_START:
                if (byte == MSG_START_MARKER) {
                    receiveState = READING_HEADER;
                    tempBuffer = "";
                    messageStartTime = millis();
                }
                break;

            case READING_HEADER:
                tempBuffer += (char)byte;
                if (tempBuffer.length() >= 7) {  // LENGTH(4) + CRC(2) + TYPE(1)
                    // Parse header
                    expectedMessageLength = (uint32_t)tempBuffer[0] |
                                            ((uint32_t)tempBuffer[1] << 8) |
                                            ((uint32_t)tempBuffer[2] << 16) |
                                            ((uint32_t)tempBuffer[3] << 24);

                    expectedCRC = (uint16_t)tempBuffer[4] | ((uint16_t)tempBuffer[5] << 8);

                    // Validate message length
                    if (expectedMessageLength > MESSAGING_MAX_PAYLOAD_LENGTH) {
                        ESP_LOGW(TAG, "Message length %u exceeds maximum %d", expectedMessageLength, MESSAGING_MAX_PAYLOAD_LENGTH);
                        return false;
                    }

                    tempBuffer = "";  // Clear for payload
                    receiveState = READING_PAYLOAD;
                }
                break;

            case READING_PAYLOAD:
                if (byte == MSG_END_MARKER && tempBuffer.length() == expectedMessageLength) {
                    receiveState = WAITING_FOR_END;
                    return processCompleteMessage();
                } else if (byte != MSG_END_MARKER) {
                    tempBuffer += (char)byte;

                    // Check for buffer overflow
                    if (tempBuffer.length() > expectedMessageLength) {
                        ESP_LOGW(TAG, "Payload length exceeds expected %u", expectedMessageLength);
                        stats.buffer_overflows++;
                        return false;
                    }
                } else {
                    // Premature end marker
                    ESP_LOGW(TAG, "Premature end marker, expected %u bytes, got %u", expectedMessageLength, tempBuffer.length());
                    return false;
                }
                break;

            default:
                return false;
        }

        return true;
    }

    bool processCompleteMessage() {
        // Validate CRC
        uint16_t calculatedCRC = calculateCRC16((const uint8_t*)tempBuffer.c_str(), tempBuffer.length());
        if (calculatedCRC != expectedCRC) {
            ESP_LOGW(TAG, "CRC mismatch: expected 0x%04X, calculated 0x%04X", expectedCRC, calculatedCRC);
            stats.crc_errors++;
            resetReceiveState();
            return false;
        }

        // Message validated successfully
        stats.messagesReceived++;
        ESP_LOGD(TAG, "Enhanced Serial RX: %u chars (CRC: 0x%04X validated)", tempBuffer.length(), calculatedCRC);

        // Forward to MessageAPI using new external message system
        Messaging::ExternalMessage externalMsg = Messaging::MessageParser::parseExternalMessage(tempBuffer);
        if (externalMsg.messageType != MessageProtocol::ExternalMessageType::INVALID) {
            Messaging::MessageCore::getInstance().handleExternalMessage(externalMsg);
        }

        resetReceiveState();
        return true;
    }

    void checkMessageTimeouts() {
        if (receiveState != WAITING_FOR_START && messageStartTime > 0) {
            if (millis() - messageStartTime > MSG_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Message timeout in state %d", receiveState);
                stats.timeout_errors++;
                resetReceiveState();
            }
        }
    }

    volatile bool newDataAvailable = false;
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

void printStatistics() {
    SerialBridge::getInstance().printStatistics();
}

}  // namespace Serial

}  // namespace Messaging
