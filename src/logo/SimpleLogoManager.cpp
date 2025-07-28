#include "SimpleLogoManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include "BSODHandler.h"
#include <Arduino.h>
#include <FS.h>
#include <esp_log.h>

static const char *TAG = "SimpleLogoManager";

SimpleLogoManager *SimpleLogoManager::instance = nullptr;
const char *SimpleLogoManager::LOGOS_DIR = "/logos";

SimpleLogoManager &SimpleLogoManager::getInstance() {
    if (!instance) {
        instance = new SimpleLogoManager();
    }
    return *instance;
}

bool SimpleLogoManager::init() {
    ESP_LOGI(TAG, "Initializing SimpleLogoManager");

    // Check if SD is mounted - trigger specific SD card error if not
    if (!Hardware::SD::isMounted()) {
        ESP_LOGE(TAG, "SD card not mounted - triggering SD card specific BSOD");

        // Get SD card status for details
        const char *sdStatus = Hardware::SD::getStatusString();
        char errorMessage[256];

        // Simple error message with status details
        snprintf(errorMessage, sizeof(errorMessage),
                 "SD card required for operation but not detected.\n\nTo resolve:\n1. Disconnect power completely\n2. Insert a properly formatted SD card\n3. Reconnect power\n\nStatus: %s", sdStatus);

        // Trigger BSOD with detailed message
        CRITICAL_FAILURE(errorMessage);

        // This will never return, but keep for clarity
        return false;
    }

    // Create logos directory
    if (!ensureLogosDirectory()) {
        ESP_LOGE(TAG, "Failed to create logos directory");
        return false;
    }

    // Subscribe to asset responses
    Messaging::subscribe(
        Messaging::Message::TYPE_ASSET_RESPONSE,
        [this](const Messaging::Message &msg) { handleAssetResponse(msg); });

    initialized = true;
    ESP_LOGI(TAG, "SimpleLogoManager initialized");
    return true;
}

void SimpleLogoManager::deinit() {
    if (!initialized)
        return;

    // Fail all pending requests
    for (auto &[requestId, request] : pendingRequests) {
        if (request.callback) {
            request.callback(false, nullptr, 0, "System shutting down");
        }
    }
    pendingRequests.clear();

    initialized = false;
    ESP_LOGI(TAG, "SimpleLogoManager deinitialized");
}

void SimpleLogoManager::update() {
    if (!initialized)
        return;

    unsigned long currentTime = millis();
    std::vector<String> expiredRequests;

    // Find expired requests
    for (auto &[requestId, request] : pendingRequests) {
        if ((currentTime - request.requestTime) > REQUEST_TIMEOUT_MS) {
            expiredRequests.push_back(requestId);
        }
    }

    // Process expired requests
    for (const String &requestId : expiredRequests) {
        auto it = pendingRequests.find(requestId);
        if (it != pendingRequests.end()) {
            if (it->second.callback) {
                it->second.callback(false, nullptr, 0, "Request timed out");
            }
            pendingRequests.erase(it);
            requestsTimedOut++;
        }
    }
}

bool SimpleLogoManager::requestLogo(const String &processName,
                                    LogoCallback callback) {
    if (!initialized) {
        if (callback)
            callback(false, nullptr, 0, "Not initialized");
        return false;
    }

    String sanitized = sanitizeProcessName(processName);

    // Check if logo already exists
    if (hasLogo(sanitized)) {
        String filePath = getLogoPath(sanitized);
        size_t fileSize = Hardware::SD::getFileSize(filePath.c_str());

        if (fileSize > 0 && fileSize <= 100000) {  // 100KB max
            uint8_t *buffer = (uint8_t *)malloc(fileSize);
            if (buffer) {
                Hardware::SD::SDFileResult result =
                    Hardware::SD::readFile(filePath.c_str(), (char *)buffer, fileSize);
                if (result.success) {
                    if (callback)
                        callback(true, buffer, result.bytesProcessed, "");
                    return true;
                }
                free(buffer);
            }
        }
    }

    // Create asset request
    auto msg = Messaging::Message::createAssetRequest(sanitized.c_str(), "");

    // Track the request
    LogoRequest request;
    request.processName = sanitized;
    request.requestId = msg.requestId;
    request.callback = callback;
    request.requestTime = millis();

    pendingRequests[msg.requestId] = request;

    // Send the message
    Messaging::sendMessage(msg);

    requestsSubmitted++;
    return true;
}

bool SimpleLogoManager::hasLogo(const String &processName) {
    if (!initialized)
        return false;
    String filePath = getLogoPath(sanitizeProcessName(processName));
    return Hardware::SD::fileExists(filePath.c_str());
}

String SimpleLogoManager::getLVGLPath(const String &processName) {
    return "S:" + getLogoPath(sanitizeProcessName(processName));
}

void SimpleLogoManager::handleAssetResponse(const Messaging::Message &msg) {
    ESP_LOGE(TAG,
             "handleAssetResponse: Processing asset response for requestId: %s",
             msg.requestId.c_str());

    const auto &asset = msg.data.asset;

    auto it = pendingRequests.find(msg.requestId);
    if (it == pendingRequests.end()) {
        ESP_LOGE(TAG, "handleAssetResponse: Unknown requestId: %s",
                 msg.requestId.c_str());
        return;  // Unknown request
    }

    LogoRequest &request = it->second;
    ESP_LOGE(TAG, "handleAssetResponse: Found pending request for process: %s",
             request.processName.c_str());

    if (asset.success && strlen(asset.assetDataBase64) > 0) {
        ESP_LOGE(TAG, "handleAssetResponse: Asset success, base64 data length: %d",
                 strlen(asset.assetDataBase64));

        // Decode base64 to binary PNG data
        size_t base64Len = strlen(asset.assetDataBase64);
        size_t decodedSize = (base64Len * 3) / 4;  // Approximate size
        ESP_LOGE(TAG, "handleAssetResponse: Allocating %d bytes for decoded data",
                 decodedSize);

        uint8_t *decodedData = (uint8_t *)malloc(decodedSize);

        if (!decodedData) {
            ESP_LOGE(TAG, "Failed to allocate memory for decoded PNG");
            if (request.callback) {
                request.callback(false, nullptr, 0, "Memory allocation failed");
            }
            requestsFailed++;
            pendingRequests.erase(it);
            return;
        }

        // Decode base64
        ESP_LOGE(TAG, "handleAssetResponse: Decoding base64 data");
        size_t actualDecodedSize =
            base64Decode(asset.assetDataBase64, decodedData, decodedSize);

        if (actualDecodedSize == 0) {
            ESP_LOGE(TAG, "Failed to decode base64 data");
            free(decodedData);
            if (request.callback) {
                request.callback(false, nullptr, 0, "Base64 decode failed");
            }
            requestsFailed++;
            pendingRequests.erase(it);
            return;
        }

        ESP_LOGE(TAG, "handleAssetResponse: Successfully decoded %d bytes",
                 actualDecodedSize);

        // Save PNG directly to SD card
        String filePath = getLogoPath(request.processName);
        ESP_LOGE(TAG, "handleAssetResponse: Saving to file: %s", filePath.c_str());

        Hardware::SD::SDFileResult writeResult = Hardware::SD::writeBinaryFile(
            filePath.c_str(), decodedData, actualDecodedSize, false);

        if (writeResult.success) {
            ESP_LOGE(TAG,
                     "handleAssetResponse: Successfully saved logo file, bytes "
                     "written: %d",
                     writeResult.bytesProcessed);
            // Success! Call callback with decoded data
            if (request.callback) {
                request.callback(true, decodedData, actualDecodedSize, "");
            } else {
                // Free the data if no callback to consume it
                free(decodedData);
            }
            responsesReceived++;
        } else {
            ESP_LOGE(TAG, "handleAssetResponse: Failed to save logo file: %s",
                     writeResult.errorMessage);
            // Failed to save
            free(decodedData);
            if (request.callback) {
                request.callback(false, nullptr, 0, "Failed to save logo file");
            }
            requestsFailed++;
        }
    } else {
        ESP_LOGE(
            TAG,
            "handleAssetResponse: Asset request failed, success: %s, error: %s",
            asset.success ? "true" : "false",
            strlen(asset.errorMessage) > 0 ? asset.errorMessage
                                           : "no error message");
        // Server reported failure
        if (request.callback) {
            String error = strlen(asset.errorMessage) > 0 ? String(asset.errorMessage)
                                                          : "Server error";
            request.callback(false, nullptr, 0, error);
        }
        requestsFailed++;
    }

    // Remove completed request
    ESP_LOGE(TAG,
             "handleAssetResponse: Removing completed request for process: %s",
             request.processName.c_str());
    pendingRequests.erase(it);
}

String SimpleLogoManager::getLogoPath(const String &processName) {
    return String(LOGOS_DIR) + "/" + processName + ".png";
}

String SimpleLogoManager::sanitizeProcessName(const String &processName) {
    String sanitized = processName;

    // Remove any path separators
    sanitized.replace("/", "_");
    sanitized.replace("\\", "_");

    // Remove any special characters that might cause issues
    sanitized.replace(":", "_");
    sanitized.replace("*", "_");
    sanitized.replace("?", "_");
    sanitized.replace("\"", "_");
    sanitized.replace("<", "_");
    sanitized.replace(">", "_");
    sanitized.replace("|", "_");

    // Trim whitespace
    sanitized.trim();

    // If empty, use default
    if (sanitized.isEmpty()) {
        sanitized = "unknown";
    }

    return sanitized;
}

bool SimpleLogoManager::ensureLogosDirectory() {
    return Hardware::SD::ensureDirectory(LOGOS_DIR);
}

size_t SimpleLogoManager::base64Decode(const char *encoded, uint8_t *decoded,
                                       size_t maxDecodedSize) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // Create reverse lookup table
    uint8_t decode_table[256];
    memset(decode_table, 0xFF, sizeof(decode_table));
    for (int i = 0; i < 64; i++) {
        decode_table[(uint8_t)base64_chars[i]] = i;
    }

    size_t encoded_len = strlen(encoded);
    size_t decoded_len = 0;

    for (size_t i = 0; i < encoded_len; i += 4) {
        // Stop if we're going to exceed the output buffer
        if (decoded_len + 3 > maxDecodedSize) {
            break;
        }

        uint8_t a = decode_table[(uint8_t)encoded[i]];
        uint8_t b = decode_table[(uint8_t)encoded[i + 1]];
        uint8_t c = (i + 2 < encoded_len && encoded[i + 2] != '=')
                        ? decode_table[(uint8_t)encoded[i + 2]]
                        : 0;
        uint8_t d = (i + 3 < encoded_len && encoded[i + 3] != '=')
                        ? decode_table[(uint8_t)encoded[i + 3]]
                        : 0;

        if (a == 0xFF || b == 0xFF) {
            // Invalid character
            return 0;
        }

        decoded[decoded_len++] = (a << 2) | (b >> 4);

        if (i + 2 < encoded_len && encoded[i + 2] != '=') {
            decoded[decoded_len++] = (b << 4) | (c >> 2);

            if (i + 3 < encoded_len && encoded[i + 3] != '=') {
                decoded[decoded_len++] = (c << 6) | d;
            }
        }
    }

    return decoded_len;
}
