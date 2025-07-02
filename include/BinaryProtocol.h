#pragma once

#include <Arduino.h>
#include <vector>
#include <cstdint>

namespace BinaryProtocol {

// =============================================================================
// PROTOCOL CONSTANTS (from C# implementation)
// =============================================================================

static const uint8_t START_MARKER = 0x7E;
static const uint8_t END_MARKER = 0x7F;
static const uint8_t ESCAPE_MARKER = 0x7D;
static const uint8_t ESCAPE_XOR = 0x20;
static const uint8_t JSON_MESSAGE_TYPE = 0x01;
static const uint32_t MAX_PAYLOAD_SIZE = 4096 * 2;  // 8192 bytes
static const uint8_t HEADER_SIZE = 7;               // LENGTH(4) + CRC(2) + TYPE(1)
static const uint32_t MESSAGE_TIMEOUT_MS = 1000;

// Frame format: [0x7E][LENGTH_4_BYTES][CRC_2_BYTES][TYPE_1_BYTE][ESCAPED_PAYLOAD][0x7F]

// =============================================================================
// ENUMS
// =============================================================================

enum class ReceiveState {
    WaitingForStart,
    ReadingHeader,
    ReadingPayload
};

// =============================================================================
// STATISTICS TRACKING
// =============================================================================

struct ProtocolStatistics {
    uint32_t messagesReceived = 0;
    uint32_t messagesSent = 0;
    uint32_t bytesReceived = 0;
    uint32_t bytesTransmitted = 0;
    uint32_t framingErrors = 0;
    uint32_t crcErrors = 0;
    uint32_t timeoutErrors = 0;
    uint32_t bufferOverflowErrors = 0;

    void incrementMessagesReceived() { messagesReceived++; }
    void incrementMessagesSent() { messagesSent++; }
    void addBytesReceived(uint32_t bytes) { bytesReceived += bytes; }
    void addBytesTransmitted(uint32_t bytes) { bytesTransmitted += bytes; }
    void incrementFramingErrors() { framingErrors++; }
    void incrementCrcErrors() { crcErrors++; }
    void incrementTimeoutErrors() { timeoutErrors++; }
    void incrementBufferOverflowErrors() { bufferOverflowErrors++; }

    void reset() {
        messagesReceived = 0;
        messagesSent = 0;
        bytesReceived = 0;
        bytesTransmitted = 0;
        framingErrors = 0;
        crcErrors = 0;
        timeoutErrors = 0;
        bufferOverflowErrors = 0;
    }
};

// =============================================================================
// CRC16 CALCULATOR
// =============================================================================

class CRC16Calculator {
   public:
    static uint16_t calculate(const uint8_t* data, size_t length);
    static uint16_t calculate(const std::vector<uint8_t>& data);
    static uint16_t calculate(const String& data);

   private:
    static const uint16_t CRC16_POLYNOMIAL = 0x1021;  // CRC-16-CCITT
    static uint16_t crc16Table[256];
    static bool tableInitialized;
    static void initializeTable();
};

// =============================================================================
// BINARY PROTOCOL FRAMER
// =============================================================================

class BinaryProtocolFramer {
   public:
    explicit BinaryProtocolFramer();
    ~BinaryProtocolFramer() = default;

    // Encoding
    std::vector<uint8_t> encodeMessage(const String& jsonPayload);
    bool encodeMessage(const String& jsonPayload, uint8_t* outputBuffer, size_t bufferSize, size_t& frameLength);

    // Decoding
    std::vector<String> processIncomingBytes(const uint8_t* data, size_t length);
    std::vector<String> processIncomingBytes(const std::vector<uint8_t>& data);

    // State and statistics
    ReceiveState getCurrentState() const { return currentState_; }
    const ProtocolStatistics& getStatistics() const { return statistics_; }
    void resetStatistics() { statistics_.reset(); }

    // Reset state machine
    void resetStateMachine();

   private:
    // Reception state machine
    ReceiveState currentState_;
    std::vector<uint8_t> headerBuffer_;
    std::vector<uint8_t> payloadBuffer_;
    uint32_t expectedPayloadLength_;
    uint16_t expectedCrc_;
    uint8_t messageType_;
    unsigned long messageStartTime_;
    bool isEscapeNext_;

    // Statistics
    ProtocolStatistics statistics_;

    // Internal methods
    std::vector<uint8_t> applyEscapeSequences(const uint8_t* data, size_t length);
    std::vector<uint8_t> removeEscapeSequences(const std::vector<uint8_t>& data);
    bool processHeader();
    void processPayloadByte(uint8_t byte);
    String processCompleteMessage();
    bool isTimeout() const;
};

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

namespace Utils {
// Convert little-endian bytes to uint32_t
uint32_t bytesToUInt32LE(const uint8_t* bytes);

// Convert little-endian bytes to uint16_t
uint16_t bytesToUInt16LE(const uint8_t* bytes);

// Convert uint32_t to little-endian bytes
void uint32ToLEBytes(uint32_t value, uint8_t* bytes);

// Convert uint16_t to little-endian bytes
void uint16ToLEBytes(uint16_t value, uint8_t* bytes);

// Validate frame integrity
bool validateFrame(const uint8_t* frame, size_t frameLength);
}  // namespace Utils

// =============================================================================
// TESTING AND DEBUGGING
// =============================================================================

// Test function for debugging binary protocol
void testBinaryProtocol();

}  // namespace BinaryProtocol
