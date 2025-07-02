#include "../../include/BinaryProtocol.h"
#include "../../include/MessagingConfig.h"
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
    if (frame[0] != MSG_START_MARKER || frame[frameLength - 1] != MSG_END_MARKER) {
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

    // NEW APPROACH: Build frame exactly like working SerialBridge
    // Don't pre-escape payload - escape during transmission instead
    std::vector<uint8_t> frame;
    frame.reserve(1 + HEADER_SIZE + payloadLength * 1.1 + 1);  // Estimate with escape expansion

    // Start marker
    frame.push_back(MSG_START_MARKER);

    // Length (4 bytes, little-endian) - length of ORIGINAL payload before escaping
    uint32_t length = static_cast<uint32_t>(payloadLength);
    frame.push_back(static_cast<uint8_t>(length & 0xFF));
    frame.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
    frame.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));

    // CRC (2 bytes, little-endian)
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

    // Message type
    frame.push_back(0x01);  // JSON message type

    // Payload with escape sequences (like working SerialBridge)
    for (size_t i = 0; i < payloadLength; i++) {
        uint8_t byte = payloadBytes[i];
        if (byte == MSG_START_MARKER || byte == MSG_END_MARKER || byte == MSG_ESCAPE_CHAR) {
            frame.push_back(MSG_ESCAPE_CHAR);
            frame.push_back(byte ^ MSG_ESCAPE_XOR);  // XOR with 0x20 for escaping (like SerialBridge)
        } else {
            frame.push_back(byte);
        }
    }

    // End marker
    frame.push_back(MSG_END_MARKER);

    statistics_.incrementMessagesSent();
    statistics_.addBytesTransmitted(frame.size());

    ESP_LOGD(TAG, "Encoded message: %zu bytes payload -> %zu bytes frame (CRC: 0x%04X)",
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

bool BinaryProtocolFramer::transmitMessageDirect(const String& jsonPayload, std::function<bool(uint8_t)> writeByteFunc) {
    if (jsonPayload.isEmpty()) {
        ESP_LOGE(TAG, "JSON payload cannot be empty for direct transmission");
        return false;
    }
    const uint8_t* payloadBytes = reinterpret_cast<const uint8_t*>(jsonPayload.c_str());
    size_t payloadLength = jsonPayload.length();

    if (payloadLength > MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Payload exceeds maximum size for direct transmission: %zu", payloadLength);
        return false;
    }

    // Calculate CRC16 of original payload
    uint16_t crc = CRC16Calculator::calculate(payloadBytes, payloadLength);

    ESP_LOGI(TAG, "=== DIRECT TRANSMISSION (SerialBridge Method) ===");
    ESP_LOGI(TAG, "Payload: %zu bytes, CRC: 0x%04X", payloadLength, crc);

    try {
        // Send start marker
        if (!writeByteFunc(MSG_START_MARKER)) {
            ESP_LOGE(TAG, "Failed to send start marker");
            return false;
        }
        ESP_LOGI(TAG, "Sent START: 0x%02X", MSG_START_MARKER);

        // Send length (4 bytes, little endian) - like working SerialBridge
        uint32_t length = static_cast<uint32_t>(payloadLength);
        uint8_t lengthBytes[4] = {
            static_cast<uint8_t>(length & 0xFF),
            static_cast<uint8_t>((length >> 8) & 0xFF),
            static_cast<uint8_t>((length >> 16) & 0xFF),
            static_cast<uint8_t>((length >> 24) & 0xFF)};

        for (int i = 0; i < 4; i++) {
            if (!writeByteFunc(lengthBytes[i])) {
                ESP_LOGE(TAG, "Failed to send length byte %d: 0x%02X", i, lengthBytes[i]);
                return false;
            }
            ESP_LOGI(TAG, "Sent LENGTH[%d]: 0x%02X %s", i, lengthBytes[i],
                     (lengthBytes[i] == 0x00) ? "← NULL BYTE" : "");
        }

        // Send CRC (2 bytes, little endian)
        uint8_t crcBytes[2] = {
            static_cast<uint8_t>(crc & 0xFF),
            static_cast<uint8_t>((crc >> 8) & 0xFF)};

        for (int i = 0; i < 2; i++) {
            if (!writeByteFunc(crcBytes[i])) {
                ESP_LOGE(TAG, "Failed to send CRC byte %d: 0x%02X", i, crcBytes[i]);
                return false;
            }
            ESP_LOGI(TAG, "Sent CRC[%d]: 0x%02X", i, crcBytes[i]);
        }

        // Send message type
        if (!writeByteFunc(0x01)) {
            ESP_LOGE(TAG, "Failed to send message type");
            return false;
        }
        ESP_LOGI(TAG, "Sent TYPE: 0x01");

        // Send payload with escape sequences (exactly like working SerialBridge)
        ESP_LOGI(TAG, "Sending payload with escape sequences...");
        for (size_t i = 0; i < payloadLength; i++) {
            uint8_t byte = payloadBytes[i];
            if (byte == MSG_START_MARKER || byte == MSG_END_MARKER || byte == MSG_ESCAPE_CHAR) {
                // Send escape character
                if (!writeByteFunc(MSG_ESCAPE_CHAR)) {
                    ESP_LOGE(TAG, "Failed to send escape char for byte %zu", i);
                    return false;
                }
                // Send escaped byte
                uint8_t escapedByte = byte ^ MSG_ESCAPE_XOR;
                if (!writeByteFunc(escapedByte)) {
                    ESP_LOGE(TAG, "Failed to send escaped byte %zu: 0x%02X", i, escapedByte);
                    return false;
                }
                ESP_LOGI(TAG, "Escaped payload[%zu]: 0x%02X -> ESC + 0x%02X", i, byte, escapedByte);
            } else {
                // Send regular byte
                if (!writeByteFunc(byte)) {
                    ESP_LOGE(TAG, "Failed to send payload byte %zu: 0x%02X", i, byte);
                    return false;
                }
            }
        }

        // Send end marker
        if (!writeByteFunc(MSG_END_MARKER)) {
            ESP_LOGE(TAG, "Failed to send end marker");
            return false;
        }
        ESP_LOGI(TAG, "Sent END: 0x%02X", MSG_END_MARKER);

        ESP_LOGI(TAG, "=== DIRECT TRANSMISSION COMPLETE ===");

        statistics_.incrementMessagesSent();
        return true;

    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception during direct transmission: %s", e.what());
        return false;
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception during direct transmission");
        return false;
    }
}

std::vector<String> BinaryProtocolFramer::processIncomingBytes(const uint8_t* data, size_t length) {
    std::vector<String> messages;

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];

        // Check for timeout
        if (currentState_ != ReceiveState::WaitingForStart && isTimeout()) {
            ESP_LOGI(TAG, "Message timeout - resetting state machine");
            statistics_.incrementTimeoutErrors();
            resetStateMachine();
        }

        try {
            switch (currentState_) {
                case ReceiveState::WaitingForStart:
                    if (byte == MSG_START_MARKER) {
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
                    if (byte == MSG_END_MARKER && !isEscapeNext_) {
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
            ESP_LOGI(TAG, "Payload length %lu exceeds maximum %lu", expectedPayloadLength_, MAX_PAYLOAD_SIZE);
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
    // Use working SerialBridge escape sequence handling
    if (isEscapeNext_) {
        // Un-escape the byte (SerialBridge used XOR with 0x20)
        uint8_t unescaped = byte ^ MSG_ESCAPE_XOR;
        payloadBuffer_.push_back(unescaped);
        isEscapeNext_ = false;

    } else if (byte == MSG_ESCAPE_CHAR) {
        // Next byte should be un-escaped (like working SerialBridge)
        isEscapeNext_ = true;

        return;  // Don't add escape marker to payload
    } else {
        // Regular byte
        payloadBuffer_.push_back(byte);
    }

    // Check if we've received enough unescaped bytes
    if (payloadBuffer_.size() > expectedPayloadLength_) {
        ESP_LOGI(TAG, "Payload buffer overflow - received %zu bytes, expected %lu",
                 payloadBuffer_.size(), expectedPayloadLength_);
        statistics_.incrementBufferOverflowErrors();
        resetStateMachine();
    }
}
String BinaryProtocolFramer::processCompleteMessage() {
    try {
        // CRITICAL CHECK: Verify we're not in the middle of an escape sequence
        if (isEscapeNext_) {
            ESP_LOGI(TAG, "Message ended with incomplete escape sequence - missing escaped byte");
            statistics_.incrementFramingErrors();
            return String();
        }

        // Verify we have the exact expected payload length
        if (payloadBuffer_.size() != expectedPayloadLength_) {
            ESP_LOGI(TAG, "Payload length mismatch - received %zu bytes, expected %lu",
                     payloadBuffer_.size(), expectedPayloadLength_);

            statistics_.incrementFramingErrors();
            return String();
        }

        // Handle empty payload case
        if (expectedPayloadLength_ == 0) {
            ESP_LOGD(TAG, "Processing empty payload message");

            // Still verify CRC for empty payload
            uint16_t calculatedCrc = CRC16Calculator::calculate(nullptr, 0);
            if (calculatedCrc != expectedCrc_) {
                ESP_LOGI(TAG, "CRC mismatch for empty payload - calculated 0x%04X, expected 0x%04X",
                         calculatedCrc, expectedCrc_);
                statistics_.incrementCrcErrors();
                return String();
            }

            // Verify message type
            if (messageType_ != JSON_MESSAGE_TYPE) {
                ESP_LOGI(TAG, "Unsupported message type for empty payload: 0x%02X", messageType_);
                statistics_.incrementFramingErrors();
                return String();
            }

            return String("");  // Return empty but valid string
        }

        // Calculate CRC16 of the received payload
        uint16_t calculatedCrc = CRC16Calculator::calculate(payloadBuffer_.data(), payloadBuffer_.size());

        // Verify CRC16
        if (calculatedCrc != expectedCrc_) {
            ESP_LOGI(TAG, "CRC mismatch - calculated 0x%04X, expected 0x%04X",
                     calculatedCrc, expectedCrc_);

            statistics_.incrementCrcErrors();
            return String();
        }

        // Verify message type
        if (messageType_ != JSON_MESSAGE_TYPE) {
            ESP_LOGI(TAG, "Unsupported message type: 0x%02X (expected 0x%02X)",
                     messageType_, JSON_MESSAGE_TYPE);
            statistics_.incrementFramingErrors();
            return String();
        }

        // Validate that payload contains valid UTF-8/ASCII for JSON
        for (size_t i = 0; i < payloadBuffer_.size(); i++) {
            uint8_t byte = payloadBuffer_[i];
            // Allow printable ASCII, whitespace, and basic UTF-8 start bytes
            if (byte == 0 || (byte < 32 && byte != '\t' && byte != '\n' && byte != '\r')) {
                ESP_LOGI(TAG, "Invalid character in JSON payload at position %zu: 0x%02X", i, byte);
                statistics_.incrementFramingErrors();
                return String();
            }
        }

        // Convert payload to JSON string with proper memory management
        String jsonMessage;
        jsonMessage.reserve(payloadBuffer_.size() + 1);  // +1 for null terminator

        try {
            // Use const char* constructor for efficiency
            jsonMessage = String(reinterpret_cast<const char*>(payloadBuffer_.data()), payloadBuffer_.size());
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Failed to create String from payload buffer");
            statistics_.incrementFramingErrors();
            return String();
        }

        // Basic JSON validation - check for balanced braces
        int braceCount = 0;
        int bracketCount = 0;
        bool inString = false;
        bool escaped = false;

        for (size_t i = 0; i < jsonMessage.length(); i++) {
            char c = jsonMessage.charAt(i);

            if (!inString) {
                if (c == '{')
                    braceCount++;
                else if (c == '}')
                    braceCount--;
                else if (c == '[')
                    bracketCount++;
                else if (c == ']')
                    bracketCount--;
                else if (c == '"')
                    inString = true;
            } else {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
            }
        }

        if (braceCount != 0 || bracketCount != 0 || inString) {
            ESP_LOGI(TAG, "JSON validation failed - braces: %d, brackets: %d, inString: %s",
                     braceCount, bracketCount, inString ? "true" : "false");
            statistics_.incrementFramingErrors();
            return String();
        }

        ESP_LOGD(TAG, "Successfully decoded message: %zu bytes, CRC OK", payloadBuffer_.size());

        return jsonMessage;

    } catch (const std::bad_alloc& e) {
        ESP_LOGE(TAG, "Memory allocation failed during message processing");
        statistics_.incrementFramingErrors();
        return String();
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception during message processing: %s", e.what());
        statistics_.incrementFramingErrors();
        return String();
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception during message processing");
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
    // CRC16Calculator::tableInitialized = false;
}

}  // namespace BinaryProtocol
