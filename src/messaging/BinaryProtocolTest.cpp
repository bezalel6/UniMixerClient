#include "../include/BinaryProtocol.h"
#include <esp_log.h>

static const char* TAG = "BinaryProtocolTest";

namespace BinaryProtocol {

void printHexDump(const char* title, const uint8_t* data, size_t length) {
    ESP_LOGI(TAG, "%s (%zu bytes):", title, length);

    for (size_t i = 0; i < length; i += 16) {
        // Print hex values
        char hexLine[64] = {0};
        char asciiLine[20] = {0};
        int hexPos = 0;
        int asciiPos = 0;

        for (size_t j = 0; j < 16 && (i + j) < length; j++) {
            uint8_t byte = data[i + j];
            hexPos += snprintf(hexLine + hexPos, sizeof(hexLine) - hexPos, "%02X ", byte);
            asciiLine[asciiPos++] = (byte >= 32 && byte <= 126) ? byte : '.';
        }

        ESP_LOGI(TAG, "%04X: %-48s |%s|", i, hexLine, asciiLine);
    }
}

void testBinaryProtocol() {
    ESP_LOGI(TAG, "=== Binary Protocol Test ===");

    // Test with the exact JSON from user
    String testJson = "{\"messageType\":2,\"requestId\":\"esp32_157586\",\"deviceId\":\"ESP32S3-CONTROL-CENTER\",\"timestamp\":157586}";

    ESP_LOGI(TAG, "Original JSON: %s", testJson.c_str());
    ESP_LOGI(TAG, "JSON Length: %d bytes", testJson.length());

    // Create framer and encode
    BinaryProtocolFramer framer;
    std::vector<uint8_t> binaryFrame = framer.encodeMessage(testJson);

    if (binaryFrame.empty()) {
        ESP_LOGE(TAG, "ERROR: Failed to encode message!");
        return;
    }

    // Print detailed frame analysis
    printHexDump("Encoded Binary Frame", binaryFrame.data(), binaryFrame.size());

    // Verify frame structure step by step
    ESP_LOGI(TAG, "\n=== Frame Structure Analysis ===");

    if (binaryFrame.size() < 1 + HEADER_SIZE + 1) {
        ESP_LOGE(TAG, "Frame too small: %zu bytes (minimum %d)", binaryFrame.size(), 1 + HEADER_SIZE + 1);
        return;
    }

    // Check start marker
    if (binaryFrame[0] == START_MARKER) {
        ESP_LOGI(TAG, "✓ Start marker: 0x%02X (correct)", binaryFrame[0]);
    } else {
        ESP_LOGE(TAG, "✗ Start marker: 0x%02X (expected 0x%02X)", binaryFrame[0], START_MARKER);
    }

    // Extract header fields
    uint32_t payloadLength = Utils::bytesToUInt32LE(&binaryFrame[1]);
    uint16_t crc = Utils::bytesToUInt16LE(&binaryFrame[5]);
    uint8_t messageType = binaryFrame[7];

    ESP_LOGI(TAG, "Header breakdown:");
    ESP_LOGI(TAG, "  Length bytes [1-4]: %02X %02X %02X %02X = %u",
             binaryFrame[1], binaryFrame[2], binaryFrame[3], binaryFrame[4], payloadLength);
    ESP_LOGI(TAG, "  CRC bytes [5-6]: %02X %02X = 0x%04X",
             binaryFrame[5], binaryFrame[6], crc);
    ESP_LOGI(TAG, "  Type byte [7]: %02X", messageType);

    // Verify header values
    if (payloadLength == testJson.length()) {
        ESP_LOGI(TAG, "✓ Payload length correct: %u", payloadLength);
    } else {
        ESP_LOGE(TAG, "✗ Payload length mismatch: %u vs %d", payloadLength, testJson.length());
    }

    if (messageType == JSON_MESSAGE_TYPE) {
        ESP_LOGI(TAG, "✓ Message type correct: 0x%02X", messageType);
    } else {
        ESP_LOGE(TAG, "✗ Message type wrong: 0x%02X (expected 0x%02X)", messageType, JSON_MESSAGE_TYPE);
    }

    // Check end marker
    uint8_t endMarker = binaryFrame[binaryFrame.size() - 1];
    if (endMarker == END_MARKER) {
        ESP_LOGI(TAG, "✓ End marker: 0x%02X (correct)", endMarker);
    } else {
        ESP_LOGE(TAG, "✗ End marker: 0x%02X (expected 0x%02X)", endMarker, END_MARKER);
    }

    // Extract and verify payload
    size_t payloadStart = 1 + HEADER_SIZE;
    size_t payloadEnd = binaryFrame.size() - 1;
    size_t escapedPayloadLength = payloadEnd - payloadStart;

    ESP_LOGI(TAG, "Payload section [%zu to %zu]: %zu bytes (escaped)",
             payloadStart, payloadEnd - 1, escapedPayloadLength);

    if (escapedPayloadLength > 0) {
        printHexDump("Escaped Payload", &binaryFrame[payloadStart], escapedPayloadLength);
    }

    // Test decoding
    ESP_LOGI(TAG, "\n=== Decoding Test ===");
    BinaryProtocolFramer decoder;
    std::vector<String> decodedMessages = decoder.processIncomingBytes(binaryFrame.data(), binaryFrame.size());

    if (decodedMessages.size() == 1) {
        ESP_LOGI(TAG, "✓ Decoded 1 message successfully");
        ESP_LOGI(TAG, "Decoded JSON: %s", decodedMessages[0].c_str());

        if (decodedMessages[0] == testJson) {
            ESP_LOGI(TAG, "✓ Round-trip test PASSED - JSON matches exactly!");
        } else {
            ESP_LOGE(TAG, "✗ Round-trip test FAILED - JSON mismatch!");
            ESP_LOGE(TAG, "Expected: %s", testJson.c_str());
            ESP_LOGE(TAG, "Got:      %s", decodedMessages[0].c_str());
        }
    } else {
        ESP_LOGE(TAG, "✗ Decoded %zu messages (expected 1)", decodedMessages.size());
    }

    // Show protocol statistics
    const auto& encodeStats = framer.getStatistics();
    const auto& decodeStats = decoder.getStatistics();

    ESP_LOGI(TAG, "\n=== Protocol Statistics ===");
    ESP_LOGI(TAG, "Encoder - Messages: %u, Bytes: %u, Errors: %u",
             encodeStats.messagesSent, encodeStats.bytesTransmitted,
             encodeStats.framingErrors + encodeStats.crcErrors);
    ESP_LOGI(TAG, "Decoder - Messages: %u, Bytes: %u, Errors: %u",
             decodeStats.messagesReceived, decodeStats.bytesReceived,
             decodeStats.framingErrors + decodeStats.crcErrors + decodeStats.timeoutErrors);

    // Test for specific byte values that might cause issues
    ESP_LOGI(TAG, "\n=== Special Byte Analysis ===");
    bool hasSpecialBytes = false;
    for (size_t i = 0; i < testJson.length(); i++) {
        uint8_t byte = testJson[i];
        if (byte == START_MARKER || byte == END_MARKER || byte == ESCAPE_MARKER) {
            ESP_LOGI(TAG, "Found special byte in JSON at pos %zu: 0x%02X ('%c')", i, byte, byte);
            hasSpecialBytes = true;
        }
    }
    if (!hasSpecialBytes) {
        ESP_LOGI(TAG, "No special bytes found in JSON - no escaping needed");
    }
}

}  // namespace BinaryProtocol
