#include "LogoBinaryStorage.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>

static const char* TAG = "LogoBinaryStorage";

namespace Logo {

// Directory structure constants
const char* LogoBinaryStorage::LOGOS_ROOT = "/logos";
const char* LogoBinaryStorage::BINARIES_DIR = "/logos/binaries";
const char* LogoBinaryStorage::MAPPINGS_DIR = "/logos/mappings";
const char* LogoBinaryStorage::METADATA_DIR = "/logos/metadata";

LogoBinaryStorage& LogoBinaryStorage::getInstance() {
    static LogoBinaryStorage instance;
    return instance;
}

// =============================================================================
// BINARY FILE OPERATIONS (/logos/binaries/)
// =============================================================================

bool LogoBinaryStorage::saveBinaryFile(const String& binaryFileName, const uint8_t* data, size_t size) {
    if (binaryFileName.isEmpty() || !data || size == 0) {
        ESP_LOGW(TAG, "Invalid parameters for saveBinaryFile");
        return false;
    }

    if (!ensureDirectoryStructure()) {
        ESP_LOGE(TAG, "Failed to ensure directory structure");
        return false;
    }

    String fullPath = String(BINARIES_DIR) + "/" + binaryFileName;
    ESP_LOGI(TAG, "Saving binary file: %s (%zu bytes)", fullPath.c_str(), size);

    File file = Hardware::SD::openFile(fullPath.c_str(), "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", fullPath.c_str());
        return false;
    }

    size_t bytesWritten = file.write(data, size);
    file.close();

    if (bytesWritten != size) {
        ESP_LOGE(TAG, "Failed to write complete data. Written: %zu, Expected: %zu", bytesWritten, size);
        Hardware::SD::deleteFile(fullPath.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Successfully saved binary file: %s (%zu bytes)", fullPath.c_str(), bytesWritten);
    return true;
}

bool LogoBinaryStorage::deleteBinaryFile(const String& binaryFileName) {
    if (binaryFileName.isEmpty()) {
        return false;
    }

    String fullPath = String(BINARIES_DIR) + "/" + binaryFileName;
    auto result = Hardware::SD::deleteFile(fullPath.c_str());

    if (result.success) {
        ESP_LOGI(TAG, "Deleted binary file: %s", fullPath.c_str());
    } else {
        ESP_LOGW(TAG, "Failed to delete binary file: %s", fullPath.c_str());
    }

    return result.success;
}

bool LogoBinaryStorage::binaryFileExists(const String& binaryFileName) {
    if (binaryFileName.isEmpty()) {
        return false;
    }

    String fullPath = String(BINARIES_DIR) + "/" + binaryFileName;
    return Hardware::SD::fileExists(fullPath.c_str());
}

size_t LogoBinaryStorage::getBinaryFileSize(const String& binaryFileName) {
    if (binaryFileName.isEmpty()) {
        return 0;
    }

    String fullPath = String(BINARIES_DIR) + "/" + binaryFileName;
    return Hardware::SD::getFileSize(fullPath.c_str());
}

String LogoBinaryStorage::generateUniqueBinaryName(const String& baseName) {
    String sanitized = sanitizeFileName(baseName);
    String candidate = sanitized + ".bin";

    // If file doesn't exist, use it
    if (!binaryFileExists(candidate)) {
        return candidate;
    }

    // Generate unique name with counter
    for (int i = 1; i < 1000; i++) {
        candidate = sanitized + "_v" + String(i) + ".bin";
        if (!binaryFileExists(candidate)) {
            return candidate;
        }
    }

    // Fallback with timestamp
    candidate = sanitized + "_" + String(Hardware::Device::getMillis()) + ".bin";
    return candidate;
}

std::vector<String> LogoBinaryStorage::listBinaryFiles() {
    std::vector<String> files;

    if (!isReady()) {
        return files;
    }

    Hardware::SD::listDirectory(BINARIES_DIR, [&files](const char* name, bool isDir, size_t size) {
        if (!isDir && String(name).endsWith(".bin")) {
            files.push_back(String(name));
        }
    });

    return files;
}

// =============================================================================
// MAPPING OPERATIONS (/logos/mappings/)
// =============================================================================

bool LogoBinaryStorage::saveProcessMapping(const String& processName, const String& binaryFileName) {
    if (processName.isEmpty() || binaryFileName.isEmpty()) {
        return false;
    }

    String mappingPath = getMappingPath(processName);

    JsonDocument doc;
    doc["processName"] = processName;
    doc["binaryFile"] = binaryFileName;
    doc["timestamp"] = Hardware::Device::getMillis();

    String jsonContent;
    serializeJson(doc, jsonContent);

    bool success = writeJsonFile(mappingPath, jsonContent);
    if (success) {
        ESP_LOGD(TAG, "Saved process mapping: %s -> %s", processName.c_str(), binaryFileName.c_str());
    }

    return success;
}

String LogoBinaryStorage::getProcessMapping(const String& processName) {
    if (processName.isEmpty()) {
        return "";
    }

    String mappingPath = getMappingPath(processName);
    String jsonContent = readJsonFile(mappingPath);

    if (jsonContent.isEmpty()) {
        return "";
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonContent);

    if (error) {
        ESP_LOGW(TAG, "Failed to parse mapping JSON for %s: %s", processName.c_str(), error.c_str());
        return "";
    }

    return doc["binaryFile"] | "";
}

bool LogoBinaryStorage::deleteProcessMapping(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String mappingPath = getMappingPath(processName);
    auto result = Hardware::SD::deleteFile(mappingPath.c_str());

    if (result.success) {
        ESP_LOGD(TAG, "Deleted process mapping: %s", processName.c_str());
    }

    return result.success;
}

bool LogoBinaryStorage::hasProcessMapping(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String mappingPath = getMappingPath(processName);
    return Hardware::SD::fileExists(mappingPath.c_str());
}

std::vector<String> LogoBinaryStorage::listMappedProcesses() {
    std::vector<String> processes;

    if (!isReady()) {
        return processes;
    }

    Hardware::SD::listDirectory(MAPPINGS_DIR, [&processes](const char* name, bool isDir, size_t size) {
        if (!isDir && String(name).endsWith(".json")) {
            String processName = String(name);
            processName = processName.substring(0, processName.length() - 5);  // Remove .json
            processes.push_back(processName);
        }
    });

    return processes;
}

// =============================================================================
// METADATA OPERATIONS (/logos/metadata/)
// =============================================================================

bool LogoBinaryStorage::saveMetadata(const String& processName, bool verified, bool flagged, uint64_t timestamp) {
    if (processName.isEmpty()) {
        return false;
    }

    if (timestamp == 0) {
        timestamp = Hardware::Device::getMillis();
    }

    String metadataPath = getMetadataPath(processName);

    JsonDocument doc;
    doc["processName"] = processName;
    doc["verified"] = verified;
    doc["flagged"] = flagged;
    doc["timestamp"] = timestamp;

    String jsonContent;
    serializeJson(doc, jsonContent);

    bool success = writeJsonFile(metadataPath, jsonContent);
    if (success) {
        ESP_LOGD(TAG, "Saved metadata: %s (verified=%s, flagged=%s)",
                 processName.c_str(), verified ? "true" : "false", flagged ? "true" : "false");
    }

    return success;
}

bool LogoBinaryStorage::getMetadata(const String& processName, bool& verified, bool& flagged, uint64_t& timestamp) {
    verified = false;
    flagged = false;
    timestamp = 0;

    if (processName.isEmpty()) {
        return false;
    }

    String metadataPath = getMetadataPath(processName);
    String jsonContent = readJsonFile(metadataPath);

    if (jsonContent.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonContent);

    if (error) {
        ESP_LOGW(TAG, "Failed to parse metadata JSON for %s: %s", processName.c_str(), error.c_str());
        return false;
    }

    verified = doc["verified"] | false;
    flagged = doc["flagged"] | false;
    timestamp = doc["timestamp"] | 0;

    return true;
}

bool LogoBinaryStorage::deleteMetadata(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String metadataPath = getMetadataPath(processName);
    auto result = Hardware::SD::deleteFile(metadataPath.c_str());

    if (result.success) {
        ESP_LOGD(TAG, "Deleted metadata: %s", processName.c_str());
    }

    return result.success;
}

bool LogoBinaryStorage::hasMetadata(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String metadataPath = getMetadataPath(processName);
    return Hardware::SD::fileExists(metadataPath.c_str());
}

// =============================================================================
// PATH HELPERS
// =============================================================================

String LogoBinaryStorage::getBinaryPath(const String& binaryFileName) {
    if (binaryFileName.isEmpty()) {
        return "";
    }
    return "S:" + String(BINARIES_DIR) + "/" + binaryFileName;  // LVGL format
}

String LogoBinaryStorage::getMappingPath(const String& processName) {
    if (processName.isEmpty()) {
        return "";
    }
    String sanitized = sanitizeFileName(processName);
    return String(MAPPINGS_DIR) + "/" + sanitized + ".json";
}

String LogoBinaryStorage::getMetadataPath(const String& processName) {
    if (processName.isEmpty()) {
        return "";
    }
    String sanitized = sanitizeFileName(processName);
    return String(METADATA_DIR) + "/" + sanitized + ".json";
}

// =============================================================================
// DIRECTORY MANAGEMENT
// =============================================================================

bool LogoBinaryStorage::ensureDirectoryStructure() {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    // Create all required directories
    bool success = true;
    success &= ensureDirectory(LOGOS_ROOT);
    success &= ensureDirectory(BINARIES_DIR);
    success &= ensureDirectory(MAPPINGS_DIR);
    success &= ensureDirectory(METADATA_DIR);

    if (success) {
        ESP_LOGD(TAG, "Directory structure verified");
    } else {
        ESP_LOGE(TAG, "Failed to create directory structure");
    }

    return success;
}

bool LogoBinaryStorage::isReady() {
    return Hardware::SD::isMounted() &&
           Hardware::SD::directoryExists(LOGOS_ROOT) &&
           Hardware::SD::directoryExists(BINARIES_DIR) &&
           Hardware::SD::directoryExists(MAPPINGS_DIR) &&
           Hardware::SD::directoryExists(METADATA_DIR);
}

void LogoBinaryStorage::cleanup() {
    // This could implement cleanup of orphaned files, etc.
    ESP_LOGI(TAG, "Cleanup requested - not implemented yet");
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String LogoBinaryStorage::sanitizeFileName(const String& input) {
    if (input.isEmpty()) {
        return "unknown";
    }

    String sanitized = input;

    // Replace problematic characters
    sanitized.replace("/", "_");
    sanitized.replace("\\", "_");
    sanitized.replace(":", "_");
    sanitized.replace("*", "_");
    sanitized.replace("?", "_");
    sanitized.replace("\"", "_");
    sanitized.replace("<", "_");
    sanitized.replace(">", "_");
    sanitized.replace("|", "_");
    sanitized.replace(" ", "_");

    // Convert to lowercase for consistency
    sanitized.toLowerCase();

    // Limit length
    if (sanitized.length() > 100) {
        sanitized = sanitized.substring(0, 100);
    }

    return sanitized;
}

// =============================================================================
// PRIVATE HELPER METHODS
// =============================================================================

bool LogoBinaryStorage::ensureDirectory(const String& path) {
    if (!Hardware::SD::directoryExists(path.c_str())) {
        ESP_LOGI(TAG, "Creating directory: %s", path.c_str());
        return Hardware::SD::createDirectory(path.c_str());
    }
    return true;
}

String LogoBinaryStorage::readJsonFile(const String& filePath) {
    if (!Hardware::SD::fileExists(filePath.c_str())) {
        return "";
    }

    char buffer[1024];  // Reasonable size for JSON files
    auto result = Hardware::SD::readFile(filePath.c_str(), buffer, sizeof(buffer));

    if (result.success) {
        return String(buffer);
    }

    return "";
}

bool LogoBinaryStorage::writeJsonFile(const String& filePath, const String& content) {
    if (!ensureDirectoryStructure()) {
        return false;
    }

    auto result = Hardware::SD::writeFile(filePath.c_str(), content.c_str(), false);
    return result.success;
}

}  // namespace Logo
