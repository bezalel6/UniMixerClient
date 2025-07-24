#include "LogoManager.h"
#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include "BSODHandler.h"
#include <Arduino.h>
#include <FS.h>
#include <esp_log.h>
#include <algorithm>
#include <regex>

static const char *TAG = "LogoManager";

namespace AssetManagement {

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

    // Check if SD is mounted
    if (!Hardware::SD::isMounted()) {
        ESP_LOGE(TAG, "SD card not mounted - triggering SD card specific BSOD");
        const char *sdStatus = Hardware::SD::getStatusString();
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage),
                 "SD card required for operation but not detected.\n\nTo resolve:\n1. Disconnect power completely\n2. Insert a properly formatted SD card\n3. Reconnect power\n\nStatus: %s", sdStatus);
        CRITICAL_FAILURE(errorMessage);
        return false;
    }

    // Create logos directory
    if (!ensureLogosDirectory()) {
        ESP_LOGE(TAG, "Failed to create logos directory");
        return false;
    }

    // Build logo database from directory
    buildLogoDatabase();

    // Load known aliases
    initializeKnownAliases();

    // Load custom mappings from config
    loadCustomMappings();

    // Subscribe to asset responses
    Messaging::subscribe(
        Messaging::Message::TYPE_ASSET_RESPONSE,
        [this](const Messaging::Message &msg) { handleAssetResponse(msg); });

    initialized = true;
    ESP_LOGI(TAG, "LogoManager initialized with %d logos", logoDatabase.size());
    return true;
}

void LogoManager::deinit() {
    if (!initialized) return;

    // Fail all pending requests
    for (auto &[requestId, request] : pendingRequests) {
        if (request.callback) {
            request.callback(false, nullptr, 0, "System shutting down");
        }
    }
    pendingRequests.clear();
    logoDatabase.clear();
    aliasMap.clear();

    initialized = false;
    ESP_LOGI(TAG, "LogoManager deinitialized");
}

void LogoManager::update() {
    if (!initialized) return;

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

String LogoManager::normalizeProcessName(const String& processName) const {
    String result = processName;
    
    // Step 1: Remove file extensions
    static const std::vector<String> extensions = {
        ".exe", ".app", ".dmg", ".AppImage", ".deb", ".rpm"
    };
    for (const auto& ext : extensions) {
        if (result.endsWith(ext)) {
            result = result.substring(0, result.length() - ext.length());
            break;
        }
    }
    
    // Step 2: Remove version numbers (e.g., "Firefox 95" -> "Firefox")
    // Simple approach: remove trailing numbers
    int lastNonDigit = result.length() - 1;
    while (lastNonDigit >= 0 && (isdigit(result[lastNonDigit]) || result[lastNonDigit] == ' ' || result[lastNonDigit] == '.')) {
        lastNonDigit--;
    }
    if (lastNonDigit < result.length() - 1) {
        result = result.substring(0, lastNonDigit + 1);
    }
    
    // Step 3: Remove common company prefixes
    static const std::vector<String> prefixes = {
        "Microsoft ", "Google ", "Mozilla ", "Apple ", "Adobe "
    };
    for (const auto& prefix : prefixes) {
        if (result.startsWith(prefix)) {
            result = result.substring(prefix.length());
            break;
        }
    }
    
    // Step 4: Convert to lowercase
    result.toLowerCase();
    
    // Step 5: Remove special characters, keep only alphanumeric
    String cleaned = "";
    for (int i = 0; i < result.length(); i++) {
        char c = result[i];
        if (isalnum(c)) {
            cleaned += c;
        }
    }
    
    return cleaned;
}

String LogoManager::getLogoPath(const String& processName) {
    return findLogoForProcess(processName);
}

String LogoManager::findLogoForProcess(const String& processName) {
    // 1. Direct lookup (exact match)
    auto it = logoDatabase.find(processName);
    if (it != logoDatabase.end()) {
        return getLVGLPath(it->second.fileName);
    }
    
    // 2. Alias lookup
    auto aliasIt = aliasMap.find(processName);
    if (aliasIt != aliasMap.end()) {
        auto logoIt = logoDatabase.find(aliasIt->second);
        if (logoIt != logoDatabase.end()) {
            return getLVGLPath(logoIt->second.fileName);
        }
    }
    
    // 3. Normalized lookup
    String normalized = normalizeProcessName(processName);
    it = logoDatabase.find(normalized);
    if (it != logoDatabase.end()) {
        return getLVGLPath(it->second.fileName);
    }
    
    // 4. Check normalized against aliases
    aliasIt = aliasMap.find(normalized);
    if (aliasIt != aliasMap.end()) {
        auto logoIt = logoDatabase.find(aliasIt->second);
        if (logoIt != logoDatabase.end()) {
            return getLVGLPath(logoIt->second.fileName);
        }
    }
    
    // 5. Fuzzy match (only if close enough)
    auto match = findClosestMatch(normalized);
    if (match.has_value()) {
        return getLVGLPath(match->fileName);
    }
    
    // 6. Return placeholder
    return "S:/logos/placeholder.png";
}

std::optional<LogoManager::LogoEntry> LogoManager::findClosestMatch(const String& normalized) {
    const int MAX_DISTANCE = 2;  // Maximum allowed edit distance
    
    int bestDistance = MAX_DISTANCE + 1;
    std::optional<LogoEntry> bestMatch;
    
    for (const auto& [name, entry] : logoDatabase) {
        // Quick length check
        int lengthDiff = abs((int)name.length() - (int)normalized.length());
        if (lengthDiff > MAX_DISTANCE) continue;
        
        // Calculate edit distance
        int distance = calculateEditDistance(normalized, name);
        if (distance <= MAX_DISTANCE && distance < bestDistance) {
            bestDistance = distance;
            bestMatch = entry;
        }
    }
    
    return bestMatch;
}

int LogoManager::calculateEditDistance(const String& s1, const String& s2) const {
    if (s1 == s2) return 0;
    
    const size_t len1 = s1.length();
    const size_t len2 = s2.length();
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    // For short strings, use simple algorithm
    if (len1 <= 10 && len2 <= 10) {
        // Simple Levenshtein distance for small strings
        std::vector<std::vector<int>> dp(len1 + 1, std::vector<int>(len2 + 1));
        
        for (size_t i = 0; i <= len1; i++) dp[i][0] = i;
        for (size_t j = 0; j <= len2; j++) dp[0][j] = j;
        
        for (size_t i = 1; i <= len1; i++) {
            for (size_t j = 1; j <= len2; j++) {
                int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
                dp[i][j] = std::min({
                    dp[i-1][j] + 1,     // deletion
                    dp[i][j-1] + 1,     // insertion
                    dp[i-1][j-1] + cost // substitution
                });
            }
        }
        
        return dp[len1][len2];
    }
    
    // For longer strings, just check prefix similarity
    String prefix1 = s1.substring(0, min(3, (int)len1));
    String prefix2 = s2.substring(0, min(3, (int)len2));
    return (prefix1 == prefix2) ? 1 : 3;
}

bool LogoManager::hasLogo(const String& processName) {
    String normalized = normalizeProcessName(processName);
    
    // Check direct match
    if (logoDatabase.count(normalized) > 0) return true;
    
    // Check aliases
    if (aliasMap.count(normalized) > 0) return true;
    if (aliasMap.count(processName) > 0) return true;
    
    // Check if file exists
    String path = getLogoPath(processName);
    return Hardware::SD::fileExists(path.c_str());
}

bool LogoManager::requestLogo(const String& processName, LogoCallback callback) {
    if (!initialized) {
        if (callback) callback(false, nullptr, 0, "Not initialized");
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
                    if (callback) callback(true, buffer, result.bytesProcessed, "");
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

void LogoManager::buildLogoDatabase() {
    ESP_LOGI(TAG, "Building logo database from %s", LOGOS_DIR);
    
    auto files = Hardware::SD::listFiles(LOGOS_DIR);
    
    for (const auto& file : files) {
        // Skip non-image files
        if (!file.endsWith(".png") && !file.endsWith(".jpg") && 
            !file.endsWith(".jpeg") && !file.endsWith(".bmp")) {
            continue;
        }
        
        // Extract base name without extension
        String baseName = file;
        int dotIndex = baseName.lastIndexOf('.');
        if (dotIndex > 0) {
            baseName = baseName.substring(0, dotIndex);
        }
        
        // Normalize the name
        String normalized = normalizeProcessName(baseName);
        
        // Create logo entry
        LogoEntry entry;
        entry.canonicalName = normalized;
        entry.fileName = baseName + ".png";  // Assume .png for consistency
        
        logoDatabase[normalized] = entry;
        
        ESP_LOGD(TAG, "Added logo: %s -> %s", normalized.c_str(), entry.fileName.c_str());
    }
    
    ESP_LOGI(TAG, "Logo database built with %d entries", logoDatabase.size());
}

void LogoManager::initializeKnownAliases() {
    // Common aliases for popular applications
    struct AliasSet {
        const char* canonical;
        std::vector<const char*> aliases;
    };
    
    static const std::vector<AliasSet> knownAliases = {
        {"chrome", {"googlechrome", "chromebrowser", "chromium", "gc"}},
        {"firefox", {"firefoxbrowser", "mozilla", "firefoxesr", "ff"}},
        {"code", {"vscode", "visualstudiocode", "codeinsiders", "vsc"}},
        {"edge", {"microsoftedge", "edgebrowser", "msedge"}},
        {"spotify", {"spotifymusic", "spotifyclient"}},
        {"discord", {"discordapp", "discordcanary", "discordptb"}},
        {"slack", {"slackapp", "slackclient"}},
        {"teams", {"microsoftteams", "msteams"}},
        {"photoshop", {"adobephotoshop", "ps"}},
        {"illustrator", {"adobeillustrator", "ai"}},
        {"premiere", {"adobepremiere", "premierepro"}},
        {"obs", {"obsstudio", "obsproject"}},
        {"vlc", {"vlcmediaplayer", "videolan"}},
        {"steam", {"steamclient", "valve"}},
        {"telegram", {"telegramdesktop", "tg"}},
        {"whatsapp", {"whatsappdesktop", "wa"}},
        {"zoom", {"zoomclient", "zoommeeting"}},
        {"skype", {"skypeforbusiness", "microsoft.skype"}},
        {"notepad", {"notepadplusplus", "npp"}},
        {"terminal", {"windowsterminal", "wt", "cmd", "powershell"}}
    };
    
    for (const auto& aliasSet : knownAliases) {
        // Check if we have this logo
        if (logoDatabase.count(aliasSet.canonical) > 0) {
            for (const char* alias : aliasSet.aliases) {
                aliasMap[String(alias)] = String(aliasSet.canonical);
                ESP_LOGD(TAG, "Added alias: %s -> %s", alias, aliasSet.canonical);
            }
        }
    }
}

void LogoManager::loadCustomMappings() {
    // Try to load custom mappings from config file
    const char* mappingsFile = "/config/logo_mappings.txt";
    
    if (!Hardware::SD::fileExists(mappingsFile)) {
        ESP_LOGD(TAG, "No custom mappings file found");
        return;
    }
    
    // Read mappings file
    size_t fileSize = Hardware::SD::getFileSize(mappingsFile);
    if (fileSize == 0 || fileSize > 10000) return; // Sanity check
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (!buffer) return;
    
    auto result = Hardware::SD::readFile(mappingsFile, buffer, fileSize);
    if (result.success) {
        buffer[result.bytesProcessed] = '\0';
        
        // Parse line by line
        char* line = strtok(buffer, "\n");
        while (line != nullptr) {
            // Skip comments and empty lines
            if (line[0] != '#' && line[0] != '\0') {
                // Format: alias=canonical
                char* equals = strchr(line, '=');
                if (equals != nullptr) {
                    *equals = '\0';
                    String alias = String(line).trim();
                    String canonical = String(equals + 1).trim();
                    
                    if (alias.length() > 0 && canonical.length() > 0) {
                        aliasMap[alias] = canonical;
                        ESP_LOGD(TAG, "Loaded custom alias: %s -> %s", alias.c_str(), canonical.c_str());
                    }
                }
            }
            line = strtok(nullptr, "\n");
        }
    }
    
    free(buffer);
}

String LogoManager::getLVGLPath(const String& processName) {
    // Return LVGL-compatible SD card path
    String path = findLogoForProcess(processName);
    if (!path.startsWith("S:")) {
        return "S:" + String(LOGOS_DIR) + "/" + path;
    }
    return path;
}

bool LogoManager::deleteLogo(const String& processName) {
    String normalized = normalizeProcessName(processName);
    
    auto it = logoDatabase.find(normalized);
    if (it != logoDatabase.end()) {
        String filePath = String(LOGOS_DIR) + "/" + it->second.fileName;
        if (Hardware::SD::deleteFile(filePath.c_str())) {
            logoDatabase.erase(it);
            
            // Remove associated aliases
            auto aliasIt = aliasMap.begin();
            while (aliasIt != aliasMap.end()) {
                if (aliasIt->second == normalized) {
                    aliasIt = aliasMap.erase(aliasIt);
                } else {
                    ++aliasIt;
                }
            }
            
            return true;
        }
    }
    
    return false;
}

// Search functionality (on-demand, not reactive)
std::vector<LogoManager::SearchResult> LogoManager::searchLogos(const String& query, size_t limit) {
    std::vector<SearchResult> results;
    String normalizedQuery = normalizeProcessName(query);
    
    // First pass: exact and prefix matches
    for (const auto& [name, entry] : logoDatabase) {
        SearchResult result;
        result.processName = entry.canonicalName;
        result.logoPath = getLVGLPath(entry.fileName);
        
        // Exact match
        if (name == normalizedQuery) {
            result.confidence = 1.0f;
            results.push_back(result);
        }
        // Prefix match
        else if (name.startsWith(normalizedQuery)) {
            result.confidence = 0.9f;
            results.push_back(result);
        }
        // Contains match
        else if (name.indexOf(normalizedQuery) >= 0) {
            result.confidence = 0.8f;
            results.push_back(result);
        }
    }
    
    // Second pass: fuzzy matches if needed
    if (results.size() < limit / 2) {
        for (const auto& [name, entry] : logoDatabase) {
            int distance = calculateEditDistance(normalizedQuery, name);
            if (distance <= 2 && distance > 0) {
                SearchResult result;
                result.processName = entry.canonicalName;
                result.logoPath = getLVGLPath(entry.fileName);
                result.confidence = 1.0f - (distance * 0.2f); // 0.8 for distance 1, 0.6 for distance 2
                
                // Check if not already in results
                bool alreadyAdded = false;
                for (const auto& existing : results) {
                    if (existing.processName == result.processName) {
                        alreadyAdded = true;
                        break;
                    }
                }
                
                if (!alreadyAdded) {
                    results.push_back(result);
                }
            }
        }
    }
    
    // Sort by confidence
    std::sort(results.begin(), results.end(), 
              [](const SearchResult& a, const SearchResult& b) {
                  return a.confidence > b.confidence;
              });
    
    // Limit results
    if (results.size() > limit) {
        results.resize(limit);
    }
    
    return results;
}

// Existing methods that need minor updates...

String LogoManager::getStatus() const {
    char statusBuffer[256];
    snprintf(statusBuffer, sizeof(statusBuffer),
             "LogoManager: %d logos, %d aliases, %d pending requests\n"
             "Requests: %u submitted, %u received, %u timed out, %u failed",
             logoDatabase.size(), aliasMap.size(), pendingRequests.size(),
             requestsSubmitted, responsesReceived, requestsTimedOut, requestsFailed);
    return String(statusBuffer);
}

void LogoManager::handleAssetResponse(const Messaging::Message& msg) {
    // Extract request ID
    auto requestIdOpt = msg.getStringField("requestId");
    if (!requestIdOpt.has_value()) {
        ESP_LOGE(TAG, "Asset response missing requestId");
        requestsFailed++;
        return;
    }

    String requestId = requestIdOpt.value();

    // Find pending request
    auto it = pendingRequests.find(requestId);
    if (it == pendingRequests.end()) {
        ESP_LOGW(TAG, "Received response for unknown request: %s", requestId.c_str());
        return;
    }

    LogoRequest request = it->second;
    pendingRequests.erase(it);
    responsesReceived++;

    // Check success
    auto successOpt = msg.getBoolField("success");
    if (!successOpt.has_value() || !successOpt.value()) {
        auto errorOpt = msg.getStringField("error");
        String error = errorOpt.value_or("Unknown error");
        ESP_LOGE(TAG, "Asset request failed: %s", error.c_str());
        if (request.callback) {
            request.callback(false, nullptr, 0, error);
        }
        requestsFailed++;
        return;
    }

    // Get base64 data
    auto dataOpt = msg.getStringField("data");
    if (!dataOpt.has_value()) {
        ESP_LOGE(TAG, "Asset response missing data");
        if (request.callback) {
            request.callback(false, nullptr, 0, "No data in response");
        }
        requestsFailed++;
        return;
    }

    String base64Data = dataOpt.value();

    // Decode base64
    size_t maxDecodedSize = (base64Data.length() * 3) / 4 + 1;
    uint8_t* decodedData = (uint8_t*)malloc(maxDecodedSize);
    if (!decodedData) {
        ESP_LOGE(TAG, "Failed to allocate memory for decoded data");
        if (request.callback) {
            request.callback(false, nullptr, 0, "Out of memory");
        }
        requestsFailed++;
        return;
    }

    size_t decodedSize = base64Decode(base64Data.c_str(), decodedData, maxDecodedSize);
    if (decodedSize == 0) {
        ESP_LOGE(TAG, "Failed to decode base64 data");
        free(decodedData);
        if (request.callback) {
            request.callback(false, nullptr, 0, "Failed to decode data");
        }
        requestsFailed++;
        return;
    }

    // Save to file
    String filePath = getLogoPath(request.processName);
    Hardware::SD::SDFileResult writeResult = 
        Hardware::SD::writeFile(filePath.c_str(), (const char*)decodedData, decodedSize, false);

    if (!writeResult.success) {
        ESP_LOGE(TAG, "Failed to save logo: %s", writeResult.error.c_str());
        free(decodedData);
        if (request.callback) {
            request.callback(false, nullptr, 0, "Failed to save file");
        }
        requestsFailed++;
        return;
    }

    ESP_LOGI(TAG, "Successfully saved logo for %s (%d bytes)", 
             request.processName.c_str(), decodedSize);

    // Update database
    String normalized = normalizeProcessName(request.processName);
    LogoEntry entry;
    entry.canonicalName = normalized;
    entry.fileName = request.processName + ".png";
    logoDatabase[normalized] = entry;

    // Call callback with success
    if (request.callback) {
        request.callback(true, decodedData, decodedSize, "");
    } else {
        free(decodedData);
    }
}

String LogoManager::getLogoPath(const String& processName) {
    return String(LOGOS_DIR) + "/" + sanitizeProcessName(processName) + ".png";
}

String LogoManager::sanitizeProcessName(const String& processName) {
    String sanitized = processName;
    
    // Remove dangerous characters
    const char* dangerousChars = "../\\:*?\"<>|";
    for (size_t i = 0; i < strlen(dangerousChars); i++) {
        sanitized.replace(String(dangerousChars[i]), "_");
    }
    
    // Limit length
    if (sanitized.length() > 50) {
        sanitized = sanitized.substring(0, 50);
    }
    
    return sanitized;
}

bool LogoManager::ensureLogosDirectory() {
    if (!Hardware::SD::directoryExists(LOGOS_DIR)) {
        return Hardware::SD::createDirectory(LOGOS_DIR);
    }
    return true;
}

// Logo browsing support (existing methods with minor updates)
bool LogoManager::scanLogosOnce() {
    // Check cache timeout
    unsigned long currentTime = millis();
    if (logoListCached && (currentTime - lastScanTime) < CACHE_TIMEOUT_MS) {
        return true; // Use cached data
    }

    ESP_LOGI(TAG, "Scanning logos directory");
    cachedLogoPaths.clear();

    auto files = Hardware::SD::listFiles(LOGOS_DIR);
    for (const auto& file : files) {
        if (file.endsWith(".png") || file.endsWith(".jpg") || 
            file.endsWith(".jpeg") || file.endsWith(".bmp")) {
            cachedLogoPaths.push_back(file);
        }
    }

    // Sort alphabetically
    std::sort(cachedLogoPaths.begin(), cachedLogoPaths.end());

    logoListCached = true;
    lastScanTime = currentTime;

    ESP_LOGI(TAG, "Found %d logo files", cachedLogoPaths.size());
    return true;
}

std::vector<String> LogoManager::getPagedLogos(int pageIndex, int itemsPerPage) {
    if (!logoListCached) {
        scanLogosOnce();
    }

    std::vector<String> pagedLogos;
    int startIndex = pageIndex * itemsPerPage;
    int endIndex = min(startIndex + itemsPerPage, (int)cachedLogoPaths.size());

    for (int i = startIndex; i < endIndex; i++) {
        pagedLogos.push_back(cachedLogoPaths[i]);
    }

    return pagedLogos;
}

int LogoManager::getTotalLogoCount() {
    if (!logoListCached) {
        scanLogosOnce();
    }
    return cachedLogoPaths.size();
}

String LogoManager::getLogoLVGLPath(const String& logoPath) {
    if (logoPath.startsWith("S:")) {
        return logoPath;
    }
    
    if (logoPath.startsWith("/")) {
        return "S:" + logoPath;
    }
    
    return "S:" + String(LOGOS_DIR) + "/" + logoPath;
}

void LogoManager::refreshLogoList() {
    logoListCached = false;
    cachedLogoPaths.clear();
    scanLogosOnce();
}

int LogoManager::getFilteredLogoCount(const String& filter) {
    if (filter.isEmpty()) {
        return getTotalLogoCount();
    }

    int count = 0;
    String lowerFilter = filter;
    lowerFilter.toLowerCase();

    for (const auto& logoPath : cachedLogoPaths) {
        String lowerPath = logoPath;
        lowerPath.toLowerCase();
        if (lowerPath.indexOf(lowerFilter) >= 0) {
            count++;
        }
    }

    return count;
}

std::vector<String> LogoManager::getFilteredPagedLogos(const String& filter, int pageIndex, int itemsPerPage) {
    if (filter.isEmpty()) {
        return getPagedLogos(pageIndex, itemsPerPage);
    }

    std::vector<String> filteredLogos;
    String lowerFilter = filter;
    lowerFilter.toLowerCase();

    // First, collect all filtered logos
    for (const auto& logoPath : cachedLogoPaths) {
        String lowerPath = logoPath;
        lowerPath.toLowerCase();
        if (lowerPath.indexOf(lowerFilter) >= 0) {
            filteredLogos.push_back(logoPath);
        }
    }

    // Then page them
    std::vector<String> pagedLogos;
    int startIndex = pageIndex * itemsPerPage;
    int endIndex = min(startIndex + itemsPerPage, (int)filteredLogos.size());

    for (int i = startIndex; i < endIndex; i++) {
        pagedLogos.push_back(filteredLogos[i]);
    }

    return pagedLogos;
}

// Base64 decode helper (existing implementation)
size_t LogoManager::base64Decode(const char* encoded, uint8_t* decoded, size_t maxDecodedSize) {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t encodedLen = strlen(encoded);
    size_t decodedLen = 0;
    uint32_t buf = 0;
    int nbits = 0;
    
    for (size_t i = 0; i < encodedLen; i++) {
        if (encoded[i] == '=') break;
        
        const char* p = strchr(base64_chars, encoded[i]);
        if (!p) continue;
        
        buf = (buf << 6) | (p - base64_chars);
        nbits += 6;
        
        if (nbits >= 8) {
            nbits -= 8;
            if (decodedLen < maxDecodedSize) {
                decoded[decodedLen++] = (buf >> nbits) & 0xFF;
            }
        }
    }
    
    return decodedLen;
}

} // namespace AssetManagement