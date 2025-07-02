#include "../../include/BinaryProtocol.h"
#include <esp_log.h>

static const char* TAG = "BinaryProtocol";

namespace BinaryProtocol {

// =============================================================================
// CRC16 CALCULATOR IMPLEMENTATION
// =============================================================================

// CRC16 implementation simplified - using direct algorithm from working SerialBridge

uint16_t CRC16Calculator::calculate(const uint8_t* data, size_t length) {
    // Use the working CRC-16-MODBUS algorithm from previous SerialBridge implementation
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

uint16_t CRC16Calculator::calculate(const std::vector<uint8_t>& data) {
    return calculate(data.data(), data.size());
}

uint16_t CRC16Calculator::calculate(const String& data) {
    return calculate(reinterpret_cast<const uint8_t*>(data.c_str()), data.length());
}

// =============================================================================
// UTILITY FUNCTIONS IMPLEMENTATION
// =============================================================================

namespace Utils {

uint32_t bytesToUInt32LE(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[3]) << 24) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           static_cast<uint32_t>(bytes[0]);
}

uint16_t bytesToUInt16LE(const uint8_t* bytes) {
    return (static_cast<uint16_t>(bytes[1]) << 8) |
           static_cast<uint16_t>(bytes[0]);
}

void uint32ToLEBytes(uint32_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void uint16ToLEBytes(uint16_t value, uint8_t* bytes) {
    bytes[0] = static_cast<uint8_t>(value & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

bool validateFrame(const uint8_t* frame, size_t frameLength) {
    if (!frame || frameLength < HEADER_SIZE + 2) {  // +2 for start/end markers
        return false;
    }

    // Check start and end markers
    if (frame[0] != START_MARKER || frame[frameLength - 1] != END_MARKER) {
        return false;
    }

    return true;
}

}  // namespace Utils

// =============================================================================
// BINARY PROTOCOL FRAMER IMPLEMENTATION
// =============================================================================

BinaryProtocolFramer::BinaryProtocolFramer()
    : currentState_(ReceiveState::WaitingForStart), expectedPayloadLength_(0), expectedCrc_(0), messageType_(0), messageStartTime_(0), isEscapeNext_(false) {
    headerBuffer_.reserve(HEADER_SIZE);
    payloadBuffer_.reserve(MAX_PAYLOAD_SIZE);

    ESP_LOGD(TAG, "BinaryProtocolFramer initialized");
}

void BinaryProtocolFramer::resetStateMachine() {
    currentState_ = ReceiveState::WaitingForStart;
    headerBuffer_.clear();
    payloadBuffer_.clear();
    isEscapeNext_ = false;
    expectedPayloadLength_ = 0;
    expectedCrc_ = 0;
    messageType_ = 0;
    messageStartTime_ = 0;
}

std::vector<uint8_t> BinaryProtocolFramer::encodeMessage(const String& jsonPayload) {
    if (jsonPayload.isEmpty()) {
        ESP_LOGE(TAG, "JSON payload cannot be empty");
        return {};
    }

    // Convert JSON to bytes
    const uint8_t* payloadBytes = reinterpret_cast<const uint8_t*>(jsonPayload.c_str());
    size_t payloadLength = jsonPayload.length();

    if (payloadLength > MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Payload exceeds maximum size of %lu bytes: %lu", MAX_PAYLOAD_SIZE, payloadLength);
        return {};
    }

    // Calculate CRC16 of original payload
    uint16_t crc = CRC16Calculator::calculate(payloadBytes, payloadLength);

    // Apply escape sequences to payload
    std::vector<uint8_t> escapedPayload = applyEscapeSequences(payloadBytes, payloadLength);

    // Build frame
    std::vector<uint8_t> frame;
    frame.reserve(1 + HEADER_SIZE + escapedPayload.size() + 1);  // Estimate capacity

    // Start marker
    frame.push_back(START_MARKER);

    // Length (4 bytes, little-endian) - length of ORIGINAL payload before escaping
    uint8_t lengthBytes[4];
    Utils::uint32ToLEBytes(static_cast<uint32_t>(payloadLength), lengthBytes);
    frame.insert(frame.end(), lengthBytes, lengthBytes + 4);

    // CRC (2 bytes, little-endian)
    uint8_t crcBytes[2];
    Utils::uint16ToLEBytes(crc, crcBytes);
    frame.insert(frame.end(), crcBytes, crcBytes + 2);

    // Message type
    frame.push_back(JSON_MESSAGE_TYPE);

    // Escaped payload
    frame.insert(frame.end(), escapedPayload.begin(), escapedPayload.end());

    // End marker
    frame.push_back(END_MARKER);

    statistics_.incrementMessagesSent();
    statistics_.addBytesTransmitted(frame.size());

    ESP_LOGD(TAG, "Encoded message: %lu bytes payload -> %lu bytes frame (CRC: 0x%04X)",
             payloadLength, frame.size(), crc);

    return frame;
}

bool BinaryProtocolFramer::encodeMessage(const String& jsonPayload, uint8_t* outputBuffer,
                                         size_t bufferSize, size_t& frameLength) {
    auto frame = encodeMessage(jsonPayload);
    if (frame.empty() || frame.size() > bufferSize) {
        frameLength = 0;
        return false;
    }

    std::copy(frame.begin(), frame.end(), outputBuffer);
    frameLength = frame.size();
    return true;
}

std::vector<String> BinaryProtocolFramer::processIncomingBytes(const uint8_t* data, size_t length) {
    std::vector<String> messages;

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];

        // Check for timeout
        if (currentState_ != ReceiveState::WaitingForStart && isTimeout()) {
            ESP_LOGW(TAG, "Message timeout - resetting state machine");
            statistics_.incrementTimeoutErrors();
            resetStateMachine();
        }

        try {
            switch (currentState_) {
                case ReceiveState::WaitingForStart:
                    if (byte == START_MARKER) {
                        currentState_ = ReceiveState::ReadingHeader;
                        headerBuffer_.clear();
                        payloadBuffer_.clear();
                        messageStartTime_ = millis();
                        isEscapeNext_ = false;
                        ESP_LOGD(TAG, "Found start marker, reading header");
                    }
                    break;

                case ReceiveState::ReadingHeader:
                    headerBuffer_.push_back(byte);
                    if (headerBuffer_.size() >= HEADER_SIZE) {
                        if (processHeader()) {
                            currentState_ = ReceiveState::ReadingPayload;
                            ESP_LOGD(TAG, "Header processed, reading payload of %lu bytes", expectedPayloadLength_);
                        } else {
                            statistics_.incrementFramingErrors();
                            resetStateMachine();
                        }
                    }
                    break;

                case ReceiveState::ReadingPayload:
                    if (byte == END_MARKER && !isEscapeNext_) {
                        // Message complete
                        String decodedMessage = processCompleteMessage();
                        if (!decodedMessage.isEmpty()) {
                            messages.push_back(decodedMessage);
                            statistics_.incrementMessagesReceived();
                            statistics_.addBytesReceived(payloadBuffer_.size() + HEADER_SIZE + 2);  // +2 for start/end markers
                        }
                        resetStateMachine();
                    } else {
                        processPayloadByte(byte);
                    }
                    break;
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Error processing byte 0x%02X in state %d", byte, static_cast<int>(currentState_));
            statistics_.incrementFramingErrors();
            resetStateMachine();
        }
    }

    return messages;
}

std::vector<String> BinaryProtocolFramer::processIncomingBytes(const std::vector<uint8_t>& data) {
    return processIncomingBytes(data.data(), data.size());
}

std::vector<uint8_t> BinaryProtocolFramer::applyEscapeSequences(const uint8_t* data, size_t length) {
    std::vector<uint8_t> escaped;
    escaped.reserve(length * 1.1);  // Estimate 10% expansion

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        if (byte == START_MARKER || byte == END_MARKER || byte == ESCAPE_MARKER) {
            escaped.push_back(ESCAPE_MARKER);
            escaped.push_back(byte ^ ESCAPE_XOR);
        } else {
            escaped.push_back(byte);
        }
    }

    return escaped;
}

bool BinaryProtocolFramer::processHeader() {
    if (headerBuffer_.size() < HEADER_SIZE) {
        return false;
    }

    try {
        // Extract length (4 bytes, little-endian)
        expectedPayloadLength_ = Utils::bytesToUInt32LE(headerBuffer_.data());

        // Extract CRC (2 bytes, little-endian)
        expectedCrc_ = Utils::bytesToUInt16LE(headerBuffer_.data() + 4);

        // Extract message type
        messageType_ = headerBuffer_[6];

        // Validate length
        if (expectedPayloadLength_ > MAX_PAYLOAD_SIZE) {
            ESP_LOGW(TAG, "Payload length %lu exceeds maximum %lu", expectedPayloadLength_, MAX_PAYLOAD_SIZE);
            statistics_.incrementBufferOverflowErrors();
            return false;
        }

        ESP_LOGD(TAG, "Header: Length=%lu, CRC=0x%04X, Type=0x%02X",
                 expectedPayloadLength_, expectedCrc_, messageType_);

        return true;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error processing header");
        return false;
    }
}

void BinaryProtocolFramer::processPayloadByte(uint8_t byte) {
    if (isEscapeNext_) {
        // Un-escape the byte
        uint8_t unescaped = byte ^ ESCAPE_XOR;
        payloadBuffer_.push_back(unescaped);
        isEscapeNext_ = false;
        ESP_LOGD(TAG, "Un-escaped byte: 0x%02X -> 0x%02X", byte, unescaped);
    } else if (byte == ESCAPE_MARKER) {
        // Next byte should be un-escaped
        isEscapeNext_ = true;
    } else {
        // Regular byte
        payloadBuffer_.push_back(byte);
    }

    // Check if we've received enough unescaped bytes
    if (payloadBuffer_.size() > expectedPayloadLength_) {
        ESP_LOGW(TAG, "Payload buffer overflow - received %lu bytes, expected %lu",
                 payloadBuffer_.size(), expectedPayloadLength_);
        statistics_.incrementBufferOverflowErrors();
        resetStateMachine();
    }
}

String BinaryProtocolFramer::processCompleteMessage() {
    try {
        // Verify we have the exact expected payload length
        if (payloadBuffer_.size() != expectedPayloadLength_) {
            ESP_LOGW(TAG, "Payload length mismatch - received %lu bytes, expected %lu",
                     payloadBuffer_.size(), expectedPayloadLength_);
            statistics_.incrementFramingErrors();
            return String();
        }

        // Verify CRC16
        uint16_t calculatedCrc = CRC16Calculator::calculate(payloadBuffer_.data(), payloadBuffer_.size());

        if (calculatedCrc != expectedCrc_) {
            ESP_LOGW(TAG, "CRC mismatch - calculated 0x%04X, expected 0x%04X",
                     calculatedCrc, expectedCrc_);
            statistics_.incrementCrcErrors();
            return String();
        }

        // Verify message type
        if (messageType_ != JSON_MESSAGE_TYPE) {
            ESP_LOGW(TAG, "Unsupported message type: 0x%02X", messageType_);
            statistics_.incrementFramingErrors();
            return String();
        }

        // Convert payload to JSON string
        String jsonMessage;
        jsonMessage.reserve(payloadBuffer_.size());
        for (uint8_t byte : payloadBuffer_) {
            jsonMessage += static_cast<char>(byte);
        }

        ESP_LOGD(TAG, "Successfully decoded message: %lu bytes, CRC OK", payloadBuffer_.size());

        return jsonMessage;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error processing complete message");
        statistics_.incrementFramingErrors();
        return String();
    }
}

bool BinaryProtocolFramer::isTimeout() const {
    return (millis() - messageStartTime_) > MESSAGE_TIMEOUT_MS;
}

// Global variables to store the correct CRC parameters
static uint16_t activeCRCPolynomial = 0x1021;
static uint16_t activeCRCInitial = 0xFFFF;
static bool activeCRCReflect = false;

void updateCRCAlgorithm(uint16_t polynomial, uint16_t initial, bool reflect) {
    ESP_LOGI("BinaryProtocol", "Updating CRC algorithm: Poly=0x%04X, Init=0x%04X, Reflect=%s",
             polynomial, initial, reflect ? "true" : "false");
    activeCRCPolynomial = polynomial;
    activeCRCInitial = initial;
    activeCRCReflect = reflect;

    // Force recalculation of lookup table
    CRC16Calculator::tableInitialized = false;
}

}  // namespace BinaryProtocol
