#include "LogoManager.h"
#include "LogoSupplier.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>
#include <mbedtls/md5.h>
#include <cstring>
#include <cctype>
#include <algorithm>

static const char* TAG = "LogoManager";

namespace Application {
namespace LogoAssets {

// =============================================================================
// DEFAULT FUZZY MATCHING PATTERNS
// =============================================================================

static const char* DEFAULT_PATTERNS[][2] = {
    // {canonical_name, patterns}
    {"chrome.exe", "chrome|chrome\\.exe|google-chrome|chrome_proxy|chromium|google.*chrome"},
    {"firefox.exe", "firefox|firefox\\.exe|mozilla|firefox-bin|mozilla.*firefox"},
    {"code.exe", "code|code\\.exe|vscode|visual.studio.code|vs.*code"},
    {"spotify.exe", "spotify|spotify\\.exe|spotify\\.app"},
    {"discord.exe", "discord|discord\\.exe|discordcanary|discordptb"},
    {"steam.exe", "steam|steam\\.exe|steam.*client"},
    {"notepad.exe", "notepad|notepad\\.exe|notepad\\+\\+"},
    {"explorer.exe", "explorer|explorer\\.exe|windows.*explorer"},
    {nullptr, nullptr}  // Terminator
};

// =============================================================================
// SINGLETON AND LIFECYCLE
// =============================================================================

LogoManager& LogoManager::getInstance() {
    static LogoManager instance;
    return instance;
}

bool LogoManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "LogoManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing LogoManager with fuzzy matching support");

    // Create mutex for thread-safe operations
    logoOperationMutex = xSemaphoreCreateMutex();
    if (!logoOperationMutex) {
        ESP_LOGE(TAG, "Failed to create logo operation mutex");
        return false;
    }

    // Check if SD card is available
    if (!Hardware::SD::isInitialized() || !Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not available - logo functionality will be limited");
        initialized = true;
        return true;  // Not fatal - can still work in memory-only mode
    }

    // Ensure directory structure exists
    if (!ensureDirectoryStructure()) {
        ESP_LOGE(TAG, "Failed to create logo directory structure");
        vSemaphoreDelete(logoOperationMutex);
        logoOperationMutex = nullptr;
        return false;
    }

    // Initialize user mappings storage
    userMappings = new JsonDocument();
    if (!userMappings) {
        ESP_LOGE(TAG, "Failed to allocate memory for user mappings");
        vSemaphoreDelete(logoOperationMutex);
        logoOperationMutex = nullptr;
        return false;
    }

    // Load existing user mappings
    loadUserMappings();

    initialized = true;
    ESP_LOGI(TAG, "LogoManager initialized successfully");
    return true;
}

void LogoManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing LogoManager");

    // Save user mappings
    if (userMappings) {
        saveUserMappings();
        delete userMappings;
        userMappings = nullptr;
    }

    // Clean up mutex
    if (logoOperationMutex) {
        vSemaphoreDelete(logoOperationMutex);
        logoOperationMutex = nullptr;
    }

    initialized = false;
    ESP_LOGI(TAG, "LogoManager deinitialized");
}

// =============================================================================
// CORE LOGO OPERATIONS
// =============================================================================

bool LogoManager::logoExists(const char* processName) {
    if (!initialized || !processName) {
        return false;
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for logoExists");
        return false;
    }

    bool exists = false;
    String logoPath = getLogoPath(processName);

    if (!logoPath.isEmpty()) {
        exists = Hardware::SD::fileExists(logoPath.c_str());
    }

    xSemaphoreGive(logoOperationMutex);
    return exists;
}

LogoLoadResult LogoManager::loadLogo(const char* processName) {
    if (!initialized || !processName) {
        return createLoadResult(false, nullptr, 0, "LogoManager not initialized or invalid process name");
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return createLoadResult(false, nullptr, 0, "Failed to acquire mutex");
    }

    LogoLoadResult result;
    String logoPath = getLogoPath(processName);

    if (logoPath.isEmpty()) {
        result = createLoadResult(false, nullptr, 0, "Invalid logo path");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Check if file exists
    if (!Hardware::SD::fileExists(logoPath.c_str())) {
        result = createLoadResult(false, nullptr, 0, "Logo file not found");

        // Auto-request missing logo if enabled
        if (autoRequestEnabled) {
            requestLogoFromSupplier(processName);
        }

        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Get file size
    size_t fileSize = Hardware::SD::getFileSize(logoPath.c_str());
    if (fileSize == 0 || fileSize > MAX_LOGO_SIZE) {
        result = createLoadResult(false, nullptr, 0, "Invalid logo file size");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Allocate memory (prefer PSRAM if available)
    uint8_t* logoData = nullptr;
#if CONFIG_SPIRAM_USE_MALLOC
    logoData = (uint8_t*)ps_malloc(fileSize);
#else
    logoData = (uint8_t*)malloc(fileSize);
#endif

    if (!logoData) {
        result = createLoadResult(false, nullptr, 0, "Failed to allocate memory for logo");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Read file
    Hardware::SD::SDFileResult fileResult = Hardware::SD::readFile(logoPath.c_str(),
                                                                   (char*)logoData, fileSize);

    if (!fileResult.success) {
        free(logoData);
        result = createLoadResult(false, nullptr, 0, "Failed to read logo file");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Load metadata
    LogoMetadataResult metadataResult = loadMetadataFile(processName);

    result = createLoadResult(true, logoData, fileResult.bytesProcessed, nullptr);
    if (metadataResult.success) {
        result.metadata = metadataResult.metadata;
    }

    ESP_LOGI(TAG, "Successfully loaded logo for '%s': %zu bytes", processName, fileResult.bytesProcessed);

    xSemaphoreGive(logoOperationMutex);
    return result;
}

// =============================================================================
// FUZZY MATCHING OPERATIONS
// =============================================================================

FuzzyMatchResult LogoManager::findLogoFuzzy(const char* processName) {
    if (!initialized || !processName) {
        return createFuzzyResult(false, nullptr, nullptr, 0);
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for fuzzy search");
        return createFuzzyResult(false, nullptr, nullptr, 0);
    }

    FuzzyMatchResult result = performFuzzyMatch(processName);

    xSemaphoreGive(logoOperationMutex);
    return result;
}

LogoLoadResult LogoManager::loadLogoFuzzy(const char* processName) {
    // First try exact match
    if (logoExists(processName)) {
        return loadLogo(processName);
    }

    // Try fuzzy matching
    FuzzyMatchResult fuzzyResult = findLogoFuzzy(processName);
    if (!fuzzyResult.found) {
        return createLoadResult(false, nullptr, 0, "No matching logo found");
    }

    // Load the matched logo
    LogoLoadResult loadResult = loadLogo(fuzzyResult.canonicalName);
    if (loadResult.success) {
        // Add fuzzy match information to result
        loadResult.fuzzyMatch = fuzzyResult;
    } else if (autoRequestEnabled) {
        // Try to request the canonical logo via supplier
        requestLogoFromSupplier(fuzzyResult.canonicalName);
    }

    return loadResult;
}

bool LogoManager::hasMatchingPattern(const char* processName) {
    FuzzyMatchResult result = findLogoFuzzy(processName);
    return result.found && result.confidence >= MIN_MATCH_CONFIDENCE;
}

// =============================================================================
// USER CUSTOMIZATION
// =============================================================================

LogoSaveResult LogoManager::assignLogo(const char* processName, const char* sourceLogoName) {
    if (!initialized || !processName || !sourceLogoName) {
        return createSaveResult(false, 0, "Invalid parameters");
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return createSaveResult(false, 0, "Failed to acquire mutex");
    }

    LogoSaveResult result;

    // Check if source logo exists
    if (!logoExists(sourceLogoName)) {
        result = createSaveResult(false, 0, "Source logo not found");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Copy logo file
    if (!copyLogoFile(sourceLogoName, processName)) {
        result = createSaveResult(false, 0, "Failed to copy logo file");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Load source metadata and update for new assignment
    LogoMetadataResult metadataResult = loadMetadataFile(sourceLogoName);
    if (metadataResult.success) {
        LogoMetadata newMetadata = metadataResult.metadata;
        strncpy(newMetadata.processName, processName, sizeof(newMetadata.processName) - 1);
        newMetadata.userFlags.manualAssignment = true;
        newMetadata.modifiedTimestamp = Hardware::Device::getMillis();

        saveMetadataFile(processName, newMetadata);
    }

    // Update user mappings
    if (userMappings) {
        (*userMappings)[processName] = sourceLogoName;
        saveUserMappings();
    }

    result = createSaveResult(true, 0, nullptr);
    ESP_LOGI(TAG, "Logo assigned: '%s' -> '%s'", processName, sourceLogoName);

    xSemaphoreGive(logoOperationMutex);
    return result;
}

LogoSaveResult LogoManager::saveLogo(const char* processName, const uint8_t* data,
                                     size_t size, const LogoMetadata& metadata) {
    if (!initialized || !processName || !data || size == 0 || size > MAX_LOGO_SIZE) {
        return createSaveResult(false, 0, "Invalid parameters");
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return createSaveResult(false, 0, "Failed to acquire mutex");
    }

    LogoSaveResult result;
    String logoPath = getLogoPath(processName);

    if (logoPath.isEmpty()) {
        result = createSaveResult(false, 0, "Invalid logo path");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Write logo file
    Hardware::SD::SDFileResult fileResult = Hardware::SD::writeFile(logoPath.c_str(),
                                                                    (const char*)data, false);

    if (!fileResult.success) {
        result = createSaveResult(false, 0, "Failed to write logo file");
        xSemaphoreGive(logoOperationMutex);
        return result;
    }

    // Save metadata
    LogoMetadata updatedMetadata = metadata;
    strncpy(updatedMetadata.processName, processName, sizeof(updatedMetadata.processName) - 1);
    updatedMetadata.fileSize = size;
    updatedMetadata.modifiedTimestamp = Hardware::Device::getMillis();

    // Calculate checksum
    String checksum = calculateChecksum(data, size);
    strncpy(updatedMetadata.checksum, checksum.c_str(), sizeof(updatedMetadata.checksum) - 1);

    saveMetadataFile(processName, updatedMetadata);

    result = createSaveResult(true, fileResult.bytesProcessed, nullptr);
    ESP_LOGI(TAG, "Logo saved: '%s' (%zu bytes)", processName, size);

    xSemaphoreGive(logoOperationMutex);
    return result;
}

bool LogoManager::flagLogoIncorrect(const char* processName, bool incorrect) {
    if (!initialized || !processName) {
        return false;
    }

    LogoMetadataResult metadataResult = getLogoMetadata(processName);
    if (!metadataResult.success) {
        return false;
    }

    LogoMetadata metadata = metadataResult.metadata;
    metadata.userFlags.incorrect = incorrect;
    metadata.modifiedTimestamp = Hardware::Device::getMillis();

    bool success = updateLogoMetadata(processName, metadata);
    if (success) {
        ESP_LOGI(TAG, "Logo flagged as %s: '%s'", incorrect ? "incorrect" : "correct", processName);
    }

    return success;
}

bool LogoManager::markLogoVerified(const char* processName, bool verified) {
    if (!initialized || !processName) {
        return false;
    }

    LogoMetadataResult metadataResult = getLogoMetadata(processName);
    if (!metadataResult.success) {
        return false;
    }

    LogoMetadata metadata = metadataResult.metadata;
    metadata.userFlags.verified = verified;
    metadata.userFlags.incorrect = false;  // Clear incorrect flag when verified
    metadata.modifiedTimestamp = Hardware::Device::getMillis();

    bool success = updateLogoMetadata(processName, metadata);
    if (success) {
        ESP_LOGI(TAG, "Logo marked as %s: '%s'", verified ? "verified" : "unverified", processName);
    }

    return success;
}

bool LogoManager::setManualAssignment(const char* processName, const char* targetLogo) {
    if (!initialized || !processName || !targetLogo || !userMappings) {
        return false;
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return false;
    }

    (*userMappings)[processName] = targetLogo;
    bool success = saveUserMappings();

    ESP_LOGI(TAG, "Manual assignment set: '%s' -> '%s'", processName, targetLogo);

    xSemaphoreGive(logoOperationMutex);
    return success;
}

// =============================================================================
// PATTERN MANAGEMENT
// =============================================================================

bool LogoManager::addMatchingPattern(const char* canonicalName, const char* pattern) {
    if (!initialized || !canonicalName || !pattern) {
        return false;
    }

    LogoMetadataResult metadataResult = getLogoMetadata(canonicalName);
    if (!metadataResult.success) {
        return false;
    }

    LogoMetadata metadata = metadataResult.metadata;

    // Add pattern to existing patterns (comma-separated)
    String currentPatterns(metadata.patterns);
    if (!currentPatterns.isEmpty()) {
        currentPatterns += ",";
    }
    currentPatterns += pattern;

    // Truncate if too long
    if (currentPatterns.length() >= sizeof(metadata.patterns)) {
        currentPatterns = currentPatterns.substring(0, sizeof(metadata.patterns) - 1);
    }

    strncpy(metadata.patterns, currentPatterns.c_str(), sizeof(metadata.patterns) - 1);
    metadata.modifiedTimestamp = Hardware::Device::getMillis();

    return updateLogoMetadata(canonicalName, metadata);
}

bool LogoManager::removeMatchingPattern(const char* canonicalName, const char* pattern) {
    if (!initialized || !canonicalName || !pattern) {
        return false;
    }

    LogoMetadataResult metadataResult = getLogoMetadata(canonicalName);
    if (!metadataResult.success) {
        return false;
    }

    LogoMetadata metadata = metadataResult.metadata;
    String currentPatterns(metadata.patterns);
    String patternToRemove(pattern);

    // Remove pattern from comma-separated list
    int index = currentPatterns.indexOf(patternToRemove);
    if (index >= 0) {
        // Handle comma placement
        if (index > 0 && currentPatterns.charAt(index - 1) == ',') {
            currentPatterns.remove(index - 1, patternToRemove.length() + 1);
        } else if (index + patternToRemove.length() < currentPatterns.length() &&
                   currentPatterns.charAt(index + patternToRemove.length()) == ',') {
            currentPatterns.remove(index, patternToRemove.length() + 1);
        } else {
            currentPatterns.remove(index, patternToRemove.length());
        }

        strncpy(metadata.patterns, currentPatterns.c_str(), sizeof(metadata.patterns) - 1);
        metadata.modifiedTimestamp = Hardware::Device::getMillis();

        return updateLogoMetadata(canonicalName, metadata);
    }

    return false;  // Pattern not found
}

bool LogoManager::updateMatchingPatterns(const char* canonicalName, const char* patterns) {
    if (!initialized || !canonicalName || !patterns) {
        return false;
    }

    LogoMetadataResult metadataResult = getLogoMetadata(canonicalName);
    if (!metadataResult.success) {
        return false;
    }

    LogoMetadata metadata = metadataResult.metadata;
    strncpy(metadata.patterns, patterns, sizeof(metadata.patterns) - 1);
    metadata.modifiedTimestamp = Hardware::Device::getMillis();

    return updateLogoMetadata(canonicalName, metadata);
}

// =============================================================================
// UTILITY OPERATIONS
// =============================================================================

bool LogoManager::deleteLogo(const char* processName) {
    if (!initialized || !processName) {
        return false;
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return false;
    }

    bool success = true;

    // Delete logo file
    String logoPath = getLogoPath(processName);
    if (!logoPath.isEmpty() && Hardware::SD::fileExists(logoPath.c_str())) {
        Hardware::SD::SDFileResult result = Hardware::SD::deleteFile(logoPath.c_str());
        if (!result.success) {
            success = false;
        }
    }

    // Delete metadata file
    String metadataPath = getMetadataPath(processName);
    if (!metadataPath.isEmpty() && Hardware::SD::fileExists(metadataPath.c_str())) {
        Hardware::SD::SDFileResult result = Hardware::SD::deleteFile(metadataPath.c_str());
        if (!result.success) {
            success = false;
        }
    }

    // Remove from user mappings
    if (userMappings && (*userMappings)[processName].is<const char*>()) {
        userMappings->remove(processName);
        saveUserMappings();
    }

    if (success) {
        ESP_LOGI(TAG, "Logo deleted: '%s'", processName);
    }

    xSemaphoreGive(logoOperationMutex);
    return success;
}

LogoMetadataResult LogoManager::getLogoMetadata(const char* processName) {
    if (!initialized || !processName) {
        return createMetadataResult(false, {}, "Invalid parameters");
    }

    return loadMetadataFile(processName);
}

bool LogoManager::updateLogoMetadata(const char* processName, const LogoMetadata& metadata) {
    if (!initialized || !processName) {
        return false;
    }

    return saveMetadataFile(processName, metadata);
}

bool LogoManager::listLogos(std::function<void(const char* processName, const LogoMetadata& metadata)> callback) {
    if (!initialized || !callback) {
        return false;
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return false;
    }

    // Now we can directly use the callback with proper std::function support
    bool success = Hardware::SD::listDirectory(LOGOS_METADATA_DIR,
                                               [callback](const char* name, bool isDir, size_t size) {
                                                   if (!isDir && strstr(name, METADATA_EXT)) {
                                                       // Extract process name (remove .json extension)
                                                       String processName(name);
                                                       int dotIndex = processName.lastIndexOf('.');
                                                       if (dotIndex > 0) {
                                                           processName = processName.substring(0, dotIndex);

                                                           // Load metadata and call callback
                                                           LogoMetadataResult result = LogoManager::getInstance().loadMetadataFile(processName.c_str());
                                                           if (result.success) {
                                                               callback(processName.c_str(), result.metadata);
                                                           }
                                                       }
                                                   }
                                               });

    xSemaphoreGive(logoOperationMutex);
    return success;
}

bool LogoManager::validateLogoIntegrity(const char* processName) {
    if (!initialized || !processName) {
        return false;
    }

    // Load logo and metadata
    LogoLoadResult logoResult = loadLogo(processName);
    if (!logoResult.success) {
        return false;
    }

    // Calculate current checksum
    String currentChecksum = calculateChecksum(logoResult.data, logoResult.size);

    // Compare with stored checksum
    bool valid = (currentChecksum == String(logoResult.metadata.checksum));

    // Clean up
    free(logoResult.data);

    if (!valid) {
        ESP_LOGW(TAG, "Logo integrity check failed for '%s'", processName);
    }

    return valid;
}

size_t LogoManager::getTotalStorageUsed() {
    if (!initialized) {
        return 0;
    }

    size_t totalSize = 0;

    listLogos([&totalSize](const char* processName, const LogoMetadata& metadata) {
        totalSize += metadata.fileSize;
    });

    return totalSize;
}

bool LogoManager::cleanupInvalidLogos() {
    if (!initialized) {
        return false;
    }

    ESP_LOGI(TAG, "Starting logo cleanup process");

    bool success = true;
    std::vector<String> logosToDelete;

    // Find invalid logos
    listLogos([this, &logosToDelete](const char* processName, const LogoMetadata& metadata) {
        if (!validateLogoIntegrity(processName)) {
            logosToDelete.push_back(String(processName));
        }
    });

    // Delete invalid logos
    for (const String& logoName : logosToDelete) {
        if (!deleteLogo(logoName.c_str())) {
            success = false;
        }
    }

    ESP_LOGI(TAG, "Logo cleanup completed: %d invalid logos removed", logosToDelete.size());
    return success;
}

// =============================================================================
// PRIVATE IMPLEMENTATION
// =============================================================================

FuzzyMatchResult LogoManager::performFuzzyMatch(const char* processName) {
    char preprocessed[128];
    preprocessProcessName(processName, preprocessed, sizeof(preprocessed));

    ESP_LOGD(TAG, "Performing fuzzy match for: '%s' (preprocessed: '%s')", processName, preprocessed);

    FuzzyMatchResult bestMatch = createFuzzyResult(false);
    uint8_t bestConfidence = 0;

    // Check user mappings first
    if (userMappings && (*userMappings)[processName].is<const char*>()) {
        const char* mappedLogo = (*userMappings)[processName];
        if (logoExists(mappedLogo)) {
            LogoMetadataResult metadataResult = loadMetadataFile(mappedLogo);
            if (metadataResult.success) {
                bestMatch = createFuzzyResult(true, "user_mapping", mappedLogo, 100);
                bestMatch.metadata = metadataResult.metadata;
                return bestMatch;
            }
        }
    }

    // Check default patterns
    for (int i = 0; DEFAULT_PATTERNS[i][0] != nullptr; i++) {
        const char* canonicalName = DEFAULT_PATTERNS[i][0];
        const char* patterns = DEFAULT_PATTERNS[i][1];

        if (!logoExists(canonicalName)) {
            continue;
        }

        if (compileAndTestPattern(patterns, preprocessed)) {
            uint8_t confidence = calculateMatchConfidence(preprocessed, patterns);
            if (confidence > bestConfidence && confidence >= MIN_MATCH_CONFIDENCE) {
                bestConfidence = confidence;
                LogoMetadataResult metadataResult = loadMetadataFile(canonicalName);
                if (metadataResult.success) {
                    bestMatch = createFuzzyResult(true, patterns, canonicalName, confidence);
                    bestMatch.metadata = metadataResult.metadata;
                }
            }
        }
    }

    // Check stored logo patterns
    listLogos([this, preprocessed, &bestMatch, &bestConfidence](const char* logoProcessName, const LogoMetadata& metadata) {
        if (strlen(metadata.patterns) > 0) {
            if (compileAndTestPattern(metadata.patterns, preprocessed)) {
                uint8_t confidence = calculateMatchConfidence(preprocessed, metadata.patterns);
                if (confidence > bestConfidence && confidence >= MIN_MATCH_CONFIDENCE) {
                    bestConfidence = confidence;
                    bestMatch = createFuzzyResult(true, metadata.patterns, logoProcessName, confidence);
                    bestMatch.metadata = metadata;
                }
            }
        }
    });

    if (bestMatch.found) {
        ESP_LOGI(TAG, "Fuzzy match found: '%s' -> '%s' (confidence: %d%%)",
                 processName, bestMatch.canonicalName, bestMatch.confidence);
    } else {
        ESP_LOGD(TAG, "No fuzzy match found for: '%s'", processName);
    }

    return bestMatch;
}

bool LogoManager::compileAndTestPattern(const char* pattern, const char* testString) {
    if (!pattern || !testString) {
        return false;
    }

    // Split comma-separated patterns and test each one
    String patternStr(pattern);
    int startIndex = 0;

    while (startIndex < patternStr.length()) {
        int commaIndex = patternStr.indexOf(',', startIndex);
        String singlePattern;

        if (commaIndex == -1) {
            singlePattern = patternStr.substring(startIndex);
            startIndex = patternStr.length();
        } else {
            singlePattern = patternStr.substring(startIndex, commaIndex);
            startIndex = commaIndex + 1;
        }

        singlePattern.trim();
        if (singlePattern.length() == 0) {
            continue;
        }

        // Compile and test pattern using tiny-regex-c
        re_t compiledPattern = re_compile(singlePattern.c_str());
        if (compiledPattern) {
            int matchLength;
            int matchIndex = re_matchp(compiledPattern, testString, &matchLength);
            if (matchIndex != -1) {
                return true;  // Found a match
            }
        }
    }

    return false;
}

uint8_t LogoManager::calculateMatchConfidence(const char* processName, const char* pattern) {
    if (!processName || !pattern) {
        return 0;
    }

    // Simple confidence calculation based on string similarity
    size_t processLen = strlen(processName);
    size_t patternLen = strlen(pattern);

    if (processLen == 0 || patternLen == 0) {
        return 0;
    }

    // Exact match gets highest confidence
    if (strcmp(processName, pattern) == 0) {
        return 100;
    }

    // Simple substring matching for confidence scoring
    if (strstr(pattern, processName) || strstr(processName, pattern)) {
        return 85;
    }

    // Default confidence for regex matches
    return 75;
}

void LogoManager::preprocessProcessName(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
        return;
    }

    // Convert to lowercase
    strncpy(output, input, outputSize - 1);
    output[outputSize - 1] = '\0';

    for (size_t i = 0; output[i]; i++) {
        output[i] = tolower(output[i]);
    }

    // Remove common suffixes
    const char* suffixes[] = {".exe", ".app", "-bin", "_proxy", ".32", ".64", nullptr};
    for (int i = 0; suffixes[i]; i++) {
        size_t suffixLen = strlen(suffixes[i]);
        size_t outputLen = strlen(output);
        if (outputLen > suffixLen && strcmp(output + outputLen - suffixLen, suffixes[i]) == 0) {
            output[outputLen - suffixLen] = '\0';
            break;
        }
    }

    // Remove common prefixes
    const char* prefixes[] = {"com.", "org.", "net.", nullptr};
    for (int i = 0; prefixes[i]; i++) {
        size_t prefixLen = strlen(prefixes[i]);
        if (strncmp(output, prefixes[i], prefixLen) == 0) {
            memmove(output, output + prefixLen, strlen(output) - prefixLen + 1);
            break;
        }
    }
}

// =============================================================================
// FILE SYSTEM HELPERS
// =============================================================================

String LogoManager::getLogoPath(const char* processName) {
    if (!processName) {
        return String();
    }

    return String(LOGOS_BINARY_DIR) + "/" + String(processName) + LOGO_BINARY_EXT;
}

String LogoManager::getMetadataPath(const char* processName) {
    if (!processName) {
        return String();
    }

    return String(LOGOS_METADATA_DIR) + "/" + String(processName) + METADATA_EXT;
}

String LogoManager::getMappingsPath() {
    return String(LOGOS_MAPPINGS_DIR) + "/assignments.json";
}

bool LogoManager::ensureDirectoryStructure() {
    if (!Hardware::SD::isMounted()) {
        return false;
    }

    bool success = true;

    // Create main directories
    const char* directories[] = {
        LOGOS_ROOT_DIR,
        LOGOS_BINARY_DIR,
        LOGOS_METADATA_DIR,
        LOGOS_MAPPINGS_DIR,
        LOGOS_CACHE_DIR,
        nullptr};

    for (int i = 0; directories[i]; i++) {
        if (!Hardware::SD::directoryExists(directories[i])) {
            if (!Hardware::SD::createDirectory(directories[i])) {
                ESP_LOGE(TAG, "Failed to create directory: %s", directories[i]);
                success = false;
            } else {
                ESP_LOGI(TAG, "Created directory: %s", directories[i]);
            }
        }
    }

    return success;
}

// =============================================================================
// METADATA OPERATIONS
// =============================================================================

bool LogoManager::saveMetadataFile(const char* processName, const LogoMetadata& metadata) {
    if (!processName || !Hardware::SD::isMounted()) {
        return false;
    }

    String metadataPath = getMetadataPath(processName);
    if (metadataPath.isEmpty()) {
        return false;
    }

    // Create JSON document
    JsonDocument doc;

    doc["processName"] = metadata.processName;
    doc["patterns"] = metadata.patterns;
    doc["fileSize"] = metadata.fileSize;
    doc["width"] = metadata.width;
    doc["height"] = metadata.height;
    doc["format"] = metadata.format;
    doc["checksum"] = metadata.checksum;
    doc["createdTimestamp"] = metadata.createdTimestamp;
    doc["modifiedTimestamp"] = metadata.modifiedTimestamp;
    doc["matchConfidence"] = metadata.matchConfidence;
    doc["version"] = metadata.version;

    JsonObject userFlags = doc["userFlags"].to<JsonObject>();
    userFlags["incorrect"] = metadata.userFlags.incorrect;
    userFlags["verified"] = metadata.userFlags.verified;
    userFlags["custom"] = metadata.userFlags.custom;
    userFlags["autoDetected"] = metadata.userFlags.autoDetected;
    userFlags["manualAssignment"] = metadata.userFlags.manualAssignment;

    // Serialize to string
    String jsonString;
    serializeJson(doc, jsonString);

    // Write to file
    Hardware::SD::SDFileResult result = Hardware::SD::writeFile(metadataPath.c_str(),
                                                                jsonString.c_str(), false);

    return result.success;
}

LogoMetadataResult LogoManager::loadMetadataFile(const char* processName) {
    if (!processName || !Hardware::SD::isMounted()) {
        return createMetadataResult(false, {}, "Invalid parameters or SD not mounted");
    }

    String metadataPath = getMetadataPath(processName);
    if (metadataPath.isEmpty() || !Hardware::SD::fileExists(metadataPath.c_str())) {
        return createMetadataResult(false, {}, "Metadata file not found");
    }

    // Read file
    size_t fileSize = Hardware::SD::getFileSize(metadataPath.c_str());
    if (fileSize == 0 || fileSize > 2048) {
        return createMetadataResult(false, {}, "Invalid metadata file size");
    }

    char* jsonBuffer = (char*)malloc(fileSize + 1);
    if (!jsonBuffer) {
        return createMetadataResult(false, {}, "Failed to allocate memory for metadata");
    }

    Hardware::SD::SDFileResult fileResult = Hardware::SD::readFile(metadataPath.c_str(),
                                                                   jsonBuffer, fileSize);
    if (!fileResult.success) {
        free(jsonBuffer);
        return createMetadataResult(false, {}, "Failed to read metadata file");
    }

    jsonBuffer[fileResult.bytesProcessed] = '\0';

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonBuffer);
    free(jsonBuffer);

    if (error) {
        return createMetadataResult(false, {}, "Failed to parse metadata JSON");
    }

    // Extract metadata
    LogoMetadata metadata = {};

    strncpy(metadata.processName, doc["processName"] | "", sizeof(metadata.processName) - 1);
    strncpy(metadata.patterns, doc["patterns"] | "", sizeof(metadata.patterns) - 1);
    metadata.fileSize = doc["fileSize"] | 0;
    metadata.width = doc["width"] | 0;
    metadata.height = doc["height"] | 0;
    strncpy(metadata.format, doc["format"] | "lvgl_bin", sizeof(metadata.format) - 1);
    strncpy(metadata.checksum, doc["checksum"] | "", sizeof(metadata.checksum) - 1);
    metadata.createdTimestamp = doc["createdTimestamp"] | 0;
    metadata.modifiedTimestamp = doc["modifiedTimestamp"] | 0;
    metadata.matchConfidence = doc["matchConfidence"] | 0;
    metadata.version = doc["version"] | METADATA_VERSION;

    JsonObject userFlags = doc["userFlags"];
    metadata.userFlags.incorrect = userFlags["incorrect"] | false;
    metadata.userFlags.verified = userFlags["verified"] | false;
    metadata.userFlags.custom = userFlags["custom"] | false;
    metadata.userFlags.autoDetected = userFlags["autoDetected"] | false;
    metadata.userFlags.manualAssignment = userFlags["manualAssignment"] | false;

    return createMetadataResult(true, metadata, nullptr);
}

bool LogoManager::loadUserMappings() {
    if (!userMappings || !Hardware::SD::isMounted()) {
        return false;
    }

    String mappingsPath = getMappingsPath();
    if (!Hardware::SD::fileExists(mappingsPath.c_str())) {
        // File doesn't exist yet, that's OK
        return true;
    }

    size_t fileSize = Hardware::SD::getFileSize(mappingsPath.c_str());
    if (fileSize == 0 || fileSize > 2048) {
        return false;
    }

    char* jsonBuffer = (char*)malloc(fileSize + 1);
    if (!jsonBuffer) {
        return false;
    }

    Hardware::SD::SDFileResult fileResult = Hardware::SD::readFile(mappingsPath.c_str(),
                                                                   jsonBuffer, fileSize);
    if (!fileResult.success) {
        free(jsonBuffer);
        return false;
    }

    jsonBuffer[fileResult.bytesProcessed] = '\0';

    DeserializationError error = deserializeJson(*userMappings, jsonBuffer);
    free(jsonBuffer);

    return !error;
}

bool LogoManager::saveUserMappings() {
    if (!userMappings || !Hardware::SD::isMounted()) {
        return false;
    }

    String mappingsPath = getMappingsPath();
    String jsonString;
    serializeJson(*userMappings, jsonString);

    Hardware::SD::SDFileResult result = Hardware::SD::writeFile(mappingsPath.c_str(),
                                                                jsonString.c_str(), false);

    return result.success;
}

// =============================================================================
// UTILITY HELPERS
// =============================================================================
String LogoManager::calculateChecksum(const uint8_t* data, size_t size) {
    if (!data || size == 0) return String();

    unsigned char hash[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);

    if (mbedtls_md5_starts_ret(&ctx) != 0 ||
        mbedtls_md5_update_ret(&ctx, data, size) != 0 ||
        mbedtls_md5_finish_ret(&ctx, hash) != 0) {
        mbedtls_md5_free(&ctx);
        return String();
    }

    mbedtls_md5_free(&ctx);

    char hex[33];  // 16 bytes * 2 chars + null terminator
    for (int i = 0; i < 16; ++i) {
        sprintf(&hex[i * 2], "%02x", hash[i]);
    }

    return String(hex);
}

bool LogoManager::copyLogoFile(const char* sourceName, const char* destName) {
    if (!sourceName || !destName) {
        return false;
    }

    String sourcePath = getLogoPath(sourceName);
    String destPath = getLogoPath(destName);

    if (sourcePath.isEmpty() || destPath.isEmpty()) {
        return false;
    }

    return Hardware::SD::copyFile(sourcePath.c_str(), destPath.c_str());
}

// =============================================================================
// RESULT CREATION HELPERS
// =============================================================================

LogoLoadResult LogoManager::createLoadResult(bool success, uint8_t* data, size_t size, const char* error) {
    LogoLoadResult result = {};
    result.success = success;
    result.data = data;
    result.size = size;

    if (error) {
        strncpy(result.errorMessage, error, sizeof(result.errorMessage) - 1);
    }

    return result;
}

LogoSaveResult LogoManager::createSaveResult(bool success, size_t bytes, const char* error) {
    LogoSaveResult result = {};
    result.success = success;
    result.bytesWritten = bytes;

    if (error) {
        strncpy(result.errorMessage, error, sizeof(result.errorMessage) - 1);
    }

    return result;
}

LogoMetadataResult LogoManager::createMetadataResult(bool success, const LogoMetadata& metadata, const char* error) {
    LogoMetadataResult result = {};
    result.success = success;
    result.metadata = metadata;

    if (error) {
        strncpy(result.errorMessage, error, sizeof(result.errorMessage) - 1);
    }

    return result;
}

FuzzyMatchResult LogoManager::createFuzzyResult(bool found, const char* pattern, const char* canonical, uint8_t confidence) {
    FuzzyMatchResult result = {};
    result.found = found;
    result.confidence = confidence;

    if (pattern) {
        strncpy(result.matchedPattern, pattern, sizeof(result.matchedPattern) - 1);
    }

    if (canonical) {
        strncpy(result.canonicalName, canonical, sizeof(result.canonicalName) - 1);
    }

    return result;
}

// =============================================================================
// ASYNC LOGO LOADING
// =============================================================================

bool LogoManager::loadLogoAsync(const char* processName, LogoLoadCallback callback) {
    if (!initialized || !processName || !callback) {
        return false;
    }

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for async logo load");
        return false;
    }

    // First try immediate load
    LogoLoadResult immediateResult = loadLogo(processName);
    if (immediateResult.success) {
        // Logo is available immediately
        xSemaphoreGive(logoOperationMutex);
        callback(immediateResult);
        return true;
    }

    // Logo not available, check if we should request it
    if (!autoRequestEnabled) {
        // Auto-request disabled, just return the failure
        xSemaphoreGive(logoOperationMutex);
        callback(immediateResult);
        return true;
    }

    // Check if there's already a pending request for this logo
    String processKey(processName);
    auto it = pendingAsyncRequests.find(processKey);

    if (it != pendingAsyncRequests.end()) {
        // Request already in progress, add callback to the list
        it->second.callbacks.push_back(callback);
        ESP_LOGI(TAG, "Added callback to existing async request for: %s", processName);
        xSemaphoreGive(logoOperationMutex);
        return true;
    }

    // Create new async request
    AsyncRequest asyncRequest;
    asyncRequest.callbacks.push_back(callback);
    asyncRequest.requestTime = millis();
    asyncRequest.inProgress = false;

    pendingAsyncRequests[processKey] = asyncRequest;

    // Try to submit the request to supplier
    if (requestLogoFromSupplier(processName)) {
        pendingAsyncRequests[processKey].inProgress = true;
        ESP_LOGI(TAG, "Started async logo request for: %s", processName);
    } else {
        // Failed to submit request, complete with error
        ESP_LOGW(TAG, "Failed to submit logo request for: %s", processName);
        LogoLoadResult errorResult = createLoadResult(false, nullptr, 0, "Failed to submit request to supplier");
        callback(errorResult);
        pendingAsyncRequests.erase(processKey);
    }

    xSemaphoreGive(logoOperationMutex);
    return true;
}

void LogoManager::setLogoRequestCallback(std::function<void(const char* processName, bool success, const char* error)> callback) {
    logoRequestCallback = callback;
}

// =============================================================================
// LOGO SUPPLIER INTEGRATION
// =============================================================================

bool LogoManager::requestLogoFromSupplier(const char* processName) {
    if (!processName) {
        return false;
    }

    ESP_LOGI(TAG, "Requesting logo from supplier for: %s", processName);

    return LogoSupplierManager::getInstance().requestLogo(processName,
                                                          [this](const AssetResponse& response) {
                                                              this->onAssetReceived(response.processName.c_str(),
                                                                                    response.assetData, response.assetDataSize,
                                                                                    response.metadata, response.success,
                                                                                    response.errorMessage.c_str());
                                                          });
}

void LogoManager::onAssetReceived(const char* processName, const uint8_t* data, size_t size,
                                  const LogoMetadata& metadata, bool success, const char* error) {
    if (!initialized || !processName) {
        return;
    }

    ESP_LOGI(TAG, "Asset received for: %s (success: %s, size: %zu)",
             processName, success ? "true" : "false", size);

    if (!logoOperationMutex || xSemaphoreTake(logoOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for asset processing");
        return;
    }

    String processKey(processName);
    LogoLoadResult result;

    if (success && data && size > 0) {
        // Save the received logo
        LogoMetadata updatedMetadata = metadata;
        updatedMetadata.userFlags.autoDetected = true;
        updatedMetadata.createdTimestamp = Hardware::Device::getMillis();
        updatedMetadata.modifiedTimestamp = Hardware::Device::getMillis();

        LogoSaveResult saveResult = saveLogo(processName, data, size, updatedMetadata);
        if (saveResult.success) {
            // Successfully saved, now load it back for the result
            result = loadLogo(processName);
            if (!result.success) {
                result = createLoadResult(false, nullptr, 0, "Failed to load saved logo");
            }
        } else {
            result = createLoadResult(false, nullptr, 0, "Failed to save received logo");
        }
    } else {
        // Request failed
        result = createLoadResult(false, nullptr, 0, error ? error : "Logo request failed");
    }

    // Complete any pending async requests
    auto it = pendingAsyncRequests.find(processKey);
    if (it != pendingAsyncRequests.end()) {
        for (auto& callback : it->second.callbacks) {
            if (callback) {
                callback(result);
            }
        }
        pendingAsyncRequests.erase(it);
    }

    // Call notification callback if set
    if (logoRequestCallback) {
        logoRequestCallback(processName, success, error);
    }

    // Free asset data if provided (caller owns it temporarily)
    if (data && !result.success) {
        // Only free if we didn't successfully save/load (in which case LogoManager manages it)
        free(const_cast<uint8_t*>(data));
    }

    xSemaphoreGive(logoOperationMutex);

    ESP_LOGI(TAG, "Asset processing completed for: %s", processName);
}

}  // namespace LogoAssets
}  // namespace Application
