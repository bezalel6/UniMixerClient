#include "LogoManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include "BSODHandler.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_log.h>
#include <algorithm>
#include <cctype>
#include <vector>

static const char *TAG = "LogoManager";

LogoManager *LogoManager::instance = nullptr;
const char *LogoManager::LOGOS_DIR = "/logos";

LogoManager &LogoManager::getInstance() {
    if (!instance) {
        instance = new LogoManager();
    }
    return *instance;
}

bool LogoManager::init() {
    ESP_LOGI(TAG, "Initializing LogoManager");

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

    // Initial logo cache update
    updateLogoCache();

    initialized = true;
    ESP_LOGI(TAG, "LogoManager initialized");
    return true;
}

void LogoManager::deinit() {
    if (!initialized)
        return;

    // Clear tracking data
    requestTimestamps.clear();

    // Clear regex cache
    clearRegexCache();
    logoCache.clear();

    initialized = false;
    ESP_LOGI(TAG, "LogoManager deinitialized");
}

void LogoManager::requestLogo(const String &processName) {
    if (!initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return;
    }

    ESP_LOGI(TAG, "Requesting logo for: %s", processName.c_str());

    // Check if we already have the logo locally
    String matchedLogo = findMatchingLogo(processName);
    if (!matchedLogo.isEmpty()) {
        ESP_LOGI(TAG, "Logo already exists locally: %s", matchedLogo.c_str());
        return;
    }

    // Check if we've recently requested this logo
    auto it = requestTimestamps.find(processName);
    if (it != requestTimestamps.end()) {
        unsigned long timeSinceRequest = millis() - it->second;
        if (timeSinceRequest < RETRY_ASSET_REQUEST_MS) {
            ESP_LOGD(TAG, "Already requested %s recently (%lu ms ago), skipping",
                     processName.c_str(), timeSinceRequest);
            return;
        }
    }

    // Update timestamp and send request
    requestTimestamps[processName] = millis();

    ESP_LOGI(TAG, "Sending asset request for: %s", processName.c_str());
    auto msg = Messaging::Message::createAssetRequest(processName.c_str(), "");
    Messaging::sendMessage(msg);

    requestsSubmitted++;
}

bool LogoManager::hasLogo(const String &processName) {
    if (!initialized)
        return false;

    String matchedLogo = findMatchingLogo(processName);
    return !matchedLogo.isEmpty();
}

String LogoManager::getLVGLPath(const String &processName) {
    String matchedLogo = findMatchingLogo(processName);

    if (!matchedLogo.isEmpty()) {
        return "S:" + String(LOGOS_DIR) + "/" + matchedLogo;
    }

    // No match found - return empty string
    return "";
}

void LogoManager::handleAssetResponse(const Messaging::Message &msg) {
    ESP_LOGI(TAG, "Processing asset response for requestId: %s", msg.requestId.c_str());

    const auto &asset = msg.data.asset;

    if (asset.success && asset.assetDataBase64 && asset.assetDataLength > 0) {
        ESP_LOGI(TAG, "Asset response received for: %s", asset.processName);

        // Decode base64 to binary PNG data
        size_t base64Len = asset.assetDataLength;
        size_t decodedSize = (base64Len * 3) / 4;

        uint8_t *decodedData = (uint8_t *)malloc(decodedSize);
        if (!decodedData) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for decoded data", decodedSize);
            return;
        }

        size_t actualDecodedSize = base64Decode(asset.assetDataBase64, decodedData, decodedSize);
        if (actualDecodedSize == 0) {
            ESP_LOGE(TAG, "Base64 decode failed");
            free(decodedData);
            return;
        }

        // Generate filename
        String processName = String(asset.processName);
        String baseFilename = processName + ".png";
        String filePath = String(LOGOS_DIR) + "/" + baseFilename;

        ESP_LOGI(TAG, "Saving logo: %s (%d bytes)", filePath.c_str(), actualDecodedSize);

        // Save the logo
        Hardware::SD::SDFileResult writeResult = Hardware::SD::writeBinaryFile(
            filePath.c_str(), decodedData, actualDecodedSize, false);

        free(decodedData);

        if (writeResult.success) {
            ESP_LOGI(TAG, "Successfully saved logo: %s", filePath.c_str());
            // Update cache immediately
            updateLogoCache();
            responsesReceived++;
        } else {
            ESP_LOGE(TAG, "Failed to save logo: %s", writeResult.errorMessage);
        }
    } else {
        ESP_LOGW(TAG, "Asset response failed for: %s - %s",
                 asset.processName,
                 strlen(asset.errorMessage) > 0 ? asset.errorMessage : "Unknown error");
    }
}

void LogoManager::updateLogoCache() {
    logoCache.clear();

    ESP_LOGD(TAG, "Updating logo cache from %s", LOGOS_DIR);

    File root = SD.open(LOGOS_DIR);
    if (!root || !root.isDirectory()) {
        ESP_LOGE(TAG, "Failed to open logos directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            LogoEntry entry;
            entry.filename = filename;
            entry.lowerFilename = filename;
            entry.lowerFilename.toLowerCase();
            logoCache.push_back(entry);
        }
        file = root.openNextFile();
    }

    root.close();

    ESP_LOGI(TAG, "Logo cache updated: %d logos found", logoCache.size());
}

String LogoManager::findMatchingLogo(const String &processName) {
    if (logoCache.empty()) {
        updateLogoCache();
    }

    ESP_LOGD(TAG, "Finding logo for: %s (cache has %d entries)", processName.c_str(), logoCache.size());

    // Convert process name to lowercase for case-insensitive matching
    String lowerProcessName = processName;
    lowerProcessName.toLowerCase();

    // Strategy 1: Exact match with common suffixes
    std::vector<String> exactPatterns = {
        lowerProcessName + "\\.png$",            // chrome.png
        lowerProcessName + "_logo\\.png$",       // chrome_logo.png
        lowerProcessName + "_icon\\.png$",       // chrome_icon.png
        "logo_" + lowerProcessName + "\\.png$",  // logo_chrome.png
        "icon_" + lowerProcessName + "\\.png$",  // icon_chrome.png
        lowerProcessName,
        processName,
    };

    for (const auto &pattern : exactPatterns) {
        re_t regex = getCompiledRegex(pattern);
        if (!regex) continue;

        for (const auto &entry : logoCache) {
            int matchLength = 0;
            if (re_matchp(regex, entry.lowerFilename.c_str(), &matchLength) != -1) {
                ESP_LOGD(TAG, "Exact pattern match: %s matches %s",
                         entry.filename.c_str(), pattern.c_str());
                return entry.filename;
            }
        }
    }

    // Strategy 2: Word boundary match (chrome in google-chrome.png)
    String wordBoundaryPattern = "\\b" + processName + "\\b";
    re_t wordRegex = getCompiledRegex(wordBoundaryPattern);
    if (wordRegex) {
        for (const auto &entry : logoCache) {
            int matchLength = 0;
            if (re_matchp(wordRegex, entry.lowerFilename.c_str(), &matchLength) != -1) {
                ESP_LOGD(TAG, "Word boundary match: %s contains %s",
                         entry.filename.c_str(), processName.c_str());
                return entry.filename;
            }
        }
    }

    // Strategy 3: Contains match (last resort)
    String containsPattern = processName;
    re_t containsRegex = getCompiledRegex(containsPattern);
    if (containsRegex) {
        // Score matches by how well they match
        String bestMatch = "";
        size_t bestScore = 0;

        for (const auto &entry : logoCache) {
            int matchLength = 0;
            if (re_matchp(containsRegex, entry.lowerFilename.c_str(), &matchLength) != -1) {
                // Score based on match position and filename length
                size_t score = 100 - entry.lowerFilename.length() + matchLength;
                if (score > bestScore) {
                    bestScore = score;
                    bestMatch = entry.filename;
                }
            }
        }

        if (!bestMatch.isEmpty()) {
            ESP_LOGD(TAG, "Contains match: %s contains %s (score: %d)",
                     bestMatch.c_str(), processName.c_str(), bestScore);
            return bestMatch;
        }
    }

    // Try common variations
    std::vector<String> variations = {
        lowerProcessName + "32",   // chrome32
        lowerProcessName + "64",   // chrome64
        lowerProcessName + "x86",  // chromex86
        lowerProcessName + "x64"   // chromex64
    };

    for (const auto &variant : variations) {
        String varPattern = "\\b" + variant;
        re_t varRegex = getCompiledRegex(varPattern);
        if (!varRegex) continue;

        for (const auto &entry : logoCache) {
            int matchLength = 0;
            if (re_matchp(varRegex, entry.lowerFilename.c_str(), &matchLength) != -1) {
                ESP_LOGD(TAG, "Variant match: %s matches variant %s",
                         entry.filename.c_str(), variant.c_str());
                return entry.filename;
            }
        }
    }

    ESP_LOGD(TAG, "No logo match found for: %s", processName.c_str());
    return "";
}

re_t LogoManager::getCompiledRegex(const String &pattern) {
    // Check cache first
    auto it = regexCache.find(pattern);
    if (it != regexCache.end()) {
        return it->second;
    }

    // Compile new regex
    re_t regex = re_compile(pattern.c_str());
    if (regex) {
        // Cache it for future use
        regexCache[pattern] = regex;
    }

    return regex;
}

void LogoManager::clearRegexCache() {
    // tiny-regex-c allocates memory for compiled patterns
    // We should free them properly
    for (auto &[pattern, regex] : regexCache) {
        if (regex) {
            free(regex);
        }
    }
    regexCache.clear();
}

bool LogoManager::ensureLogosDirectory() {
    return Hardware::SD::ensureDirectory(LOGOS_DIR);
}

size_t LogoManager::base64Decode(const char *encoded, uint8_t *decoded,
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
