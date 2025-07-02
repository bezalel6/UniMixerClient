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

// Test multiple CRC-16 variants to find the one that matches the server
uint16_t calculateCRC16Variant(const uint8_t* data, size_t length, uint16_t polynomial, uint16_t initial, bool reflect = false) {
    uint16_t crc = initial;

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        if (reflect) {
            // Reflect the byte
            byte = ((byte & 0x01) << 7) | ((byte & 0x02) << 5) | ((byte & 0x04) << 3) | ((byte & 0x08) << 1) |
                   ((byte & 0x10) >> 1) | ((byte & 0x20) >> 3) | ((byte & 0x40) >> 5) | ((byte & 0x80) >> 7);
        }

        crc ^= (byte << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc = crc << 1;
            }
        }
    }

    if (reflect) {
        // Reflect the result
        crc = ((crc & 0x0001) << 15) | ((crc & 0x0002) << 13) | ((crc & 0x0004) << 11) | ((crc & 0x0008) << 9) |
              ((crc & 0x0010) << 7) | ((crc & 0x0020) << 5) | ((crc & 0x0040) << 3) | ((crc & 0x0080) << 1) |
              ((crc & 0x0100) >> 1) | ((crc & 0x0200) >> 3) | ((crc & 0x0400) >> 5) | ((crc & 0x0800) >> 7) |
              ((crc & 0x1000) >> 9) | ((crc & 0x2000) >> 11) | ((crc & 0x4000) >> 13) | ((crc & 0x8000) >> 15);
    }

    return crc;
}

void testBinaryProtocol() {
    ESP_LOGI(TAG, "=== Binary Protocol Test ===");

    // Test with the exact JSON from user
    String testJson = "{\"messageType\":2,\"requestId\":\"esp32_157586\",\"deviceId\":\"ESP32S3-CONTROL-CENTER\",\"timestamp\":157586}";

    ESP_LOGI(TAG, "Original JSON: %s", testJson.c_str());
    ESP_LOGI(TAG, "JSON Length: %d bytes", testJson.length());

    // Test different CRC-16 variants to find the one that matches server expectation (0xB93F)
    const uint8_t* testData = reinterpret_cast<const uint8_t*>(testJson.c_str());
    size_t testLength = testJson.length();

    ESP_LOGI(TAG, "\n=== CRC-16 Variant Testing ===");
    ESP_LOGI(TAG, "Server expects: 0xB93F, Server calculated: 0x2E15");

    struct CRCVariant {
        const char* name;
        uint16_t polynomial;
        uint16_t initial;
        bool reflect;
    };

    CRCVariant variants[] = {
        {"CRC-16-CCITT (0x0000)", 0x1021, 0x0000, false},
        {"CRC-16-CCITT (0xFFFF)", 0x1021, 0xFFFF, false},
        {"CRC-16-CCITT (0x1D0F)", 0x1021, 0x1D0F, false},
        {"CRC-16-IBM/ANSI", 0x8005, 0x0000, false},
        {"CRC-16-IBM/ANSI (reflected)", 0x8005, 0x0000, true},
        {"CRC-16-MODBUS", 0x8005, 0xFFFF, false},
        {"CRC-16-XMODEM", 0x1021, 0x0000, false},
        {"CRC-16-ARC", 0x8005, 0x0000, true},
    };

    for (const auto& variant : variants) {
        uint16_t crc = calculateCRC16Variant(testData, testLength, variant.polynomial, variant.initial, variant.reflect);
        ESP_LOGI(TAG, "%s: 0x%04X %s", variant.name, crc,
                 (crc == 0xB93F) ? "*** MATCH! ***" : "");
    }

    // Also test our current implementation
    uint16_t ourCrc = CRC16Calculator::calculate(testData, testLength);
    ESP_LOGI(TAG, "Our current implementation: 0x%04X %s", ourCrc,
             (ourCrc == 0xB93F) ? "*** MATCH! ***" : "");

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
