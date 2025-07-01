#include "LogoStorage.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>

static const char* TAG = "LogoStorage";

namespace Logo {

// Directory structure constants
const char* LogoStorage::LOGOS_ROOT = "/logos";
const char* LogoStorage::FILES_DIR = "/logos/files";
const char* LogoStorage::MAPPINGS_DIR = "/logos/mappings";
const char* LogoStorage::METADATA_DIR = "/logos/metadata";

LogoStorage& LogoStorage::getInstance() {
    static LogoStorage instance;
    return instance;
}

// =============================================================================
// FILE OPERATIONS (/logos/files/)
// =============================================================================

bool LogoStorage::saveFile(const String& fileName, const uint8_t* data, size_t size) {
    if (fileName.isEmpty() || !data || size == 0) {
        ESP_LOGW(TAG, "Invalid parameters for saveFile");
        return false;
    }

    if (!isValidFileType(fileName)) {
        ESP_LOGW(TAG, "Invalid file type for: %s", fileName.c_str());
        return false;
    }

    if (!ensureDirectoryStructure()) {
        ESP_LOGE(TAG, "Failed to ensure directory structure");
        return false;
    }

    String fullPath = String(FILES_DIR) + "/" + fileName;
    ESP_LOGI(TAG, "Saving file: %s (%zu bytes)", fullPath.c_str(), size);

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

    ESP_LOGI(TAG, "Successfully saved file: %s (%zu bytes)", fullPath.c_str(), bytesWritten);
    return true;
}

bool LogoStorage::deleteFile(const String& fileName) {
    if (fileName.isEmpty()) {
        return false;
    }

    String fullPath = String(FILES_DIR) + "/" + fileName;
    auto result = Hardware::SD::deleteFile(fullPath.c_str());

    if (result.success) {
        ESP_LOGI(TAG, "Deleted file: %s", fullPath.c_str());
    } else {
        ESP_LOGW(TAG, "Failed to delete file: %s", fullPath.c_str());
    }

    return result.success;
}

bool LogoStorage::fileExists(const String& fileName) {
    if (fileName.isEmpty()) {
        return false;
    }

    String fullPath = String(FILES_DIR) + "/" + fileName;
    return Hardware::SD::fileExists(fullPath.c_str());
}

size_t LogoStorage::getFileSize(const String& fileName) {
    if (fileName.isEmpty()) {
        return 0;
    }

    String fullPath = String(FILES_DIR) + "/" + fileName;
    return Hardware::SD::getFileSize(fullPath.c_str());
}

String LogoStorage::generateUniqueFileName(const String& baseName, FileType type) {
    String sanitized = sanitizeFileName(baseName);
    String extension = getFileExtension(type);
    String candidate = sanitized + extension;

    // If file doesn't exist, use it
    if (!fileExists(candidate)) {
        return candidate;
    }

    // Generate unique name with counter
    for (int i = 1; i < 1000; i++) {
        candidate = sanitized + "_v" + String(i) + extension;
        if (!fileExists(candidate)) {
            return candidate;
        }
    }

    // Fallback with timestamp
    candidate = sanitized + "_" + String(Hardware::Device::getMillis()) + extension;
    return candidate;
}

std::vector<String> LogoStorage::listFiles() {
    std::vector<String> files;

    if (!isReady()) {
        return files;
    }

    Hardware::SD::listDirectory(FILES_DIR, [&files](const char* name, bool isDir, size_t size) {
        if (!isDir) {
            String fileName = String(name);
            if (fileName.endsWith(".bin") || fileName.endsWith(".png")) {
                files.push_back(fileName);
            }
        }
    });

    return files;
}

std::vector<String> LogoStorage::listFilesByType(FileType type) {
    std::vector<String> files;
    String extension = getFileExtension(type);

    if (!isReady()) {
        return files;
    }

    Hardware::SD::listDirectory(FILES_DIR, [&files, &extension](const char* name, bool isDir, size_t size) {
        if (!isDir && String(name).endsWith(extension)) {
            files.push_back(String(name));
        }
    });

    return files;
}

// =============================================================================
// FILE TYPE UTILITIES
// =============================================================================

LogoStorage::FileType LogoStorage::getFileType(const String& fileName) {
    if (fileName.endsWith(".png")) {
        return FileType::PNG;
    } else if (fileName.endsWith(".bin")) {
        return FileType::BINARY;
    }
    return FileType::PNG;  // Default fallback
}

String LogoStorage::getFileExtension(FileType type) {
    switch (type) {
        case FileType::PNG:
            return ".png";
        case FileType::BINARY:
        default:
            return ".bin";
    }
}

bool LogoStorage::isValidFileType(const String& fileName) {
    return fileName.endsWith(".bin") || fileName.endsWith(".png");
}

// =============================================================================
// MAPPING OPERATIONS (/logos/mappings/)
// =============================================================================

bool LogoStorage::saveProcessMapping(const String& processName, const String& fileName) {
    if (processName.isEmpty() || fileName.isEmpty()) {
        return false;
    }

    String mappingPath = getMappingPath(processName);

    JsonDocument doc;
    doc["processName"] = processName;
    doc["fileName"] = fileName;
    doc["fileType"] = (getFileType(fileName) == FileType::PNG) ? "png" : "binary";
    doc["timestamp"] = Hardware::Device::getMillis();

    String jsonContent;
    serializeJson(doc, jsonContent);

    bool success = writeJsonFile(mappingPath, jsonContent);
    if (success) {
        ESP_LOGD(TAG, "Saved process mapping: %s -> %s", processName.c_str(), fileName.c_str());
    }

    return success;
}

String LogoStorage::getProcessMapping(const String& processName) {
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

    // Support both old "binaryFile" and new "fileName" formats for backward compatibility
    String result = doc["fileName"] | "";
    if (result.isEmpty()) {
        result = doc["binaryFile"] | "";
    }

    return result;
}

bool LogoStorage::deleteProcessMapping(const String& processName) {
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

bool LogoStorage::hasProcessMapping(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String mappingPath = getMappingPath(processName);
    return Hardware::SD::fileExists(mappingPath.c_str());
}

std::vector<String> LogoStorage::listMappedProcesses() {
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

bool LogoStorage::saveMetadata(const String& processName, bool verified, bool flagged, uint64_t timestamp) {
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

bool LogoStorage::getMetadata(const String& processName, bool& verified, bool& flagged, uint64_t& timestamp) {
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

bool LogoStorage::deleteMetadata(const String& processName) {
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

bool LogoStorage::hasMetadata(const String& processName) {
    if (processName.isEmpty()) {
        return false;
    }

    String metadataPath = getMetadataPath(processName);
    return Hardware::SD::fileExists(metadataPath.c_str());
}

// =============================================================================
// PATH HELPERS
// =============================================================================

String LogoStorage::getFilePath(const String& fileName) {
    if (fileName.isEmpty()) {
        return "";
    }
    // Use LVGL filesystem format with proper path separators
    return "S:/logos/files/" + fileName;
}

String LogoStorage::getMappingPath(const String& processName) {
    if (processName.isEmpty()) {
        return "";
    }
    String sanitized = sanitizeFileName(processName);
    return String(MAPPINGS_DIR) + "/" + sanitized + ".json";
}

String LogoStorage::getMetadataPath(const String& processName) {
    if (processName.isEmpty()) {
        return "";
    }
    String sanitized = sanitizeFileName(processName);
    return String(METADATA_DIR) + "/" + sanitized + ".json";
}

// =============================================================================
// DIRECTORY MANAGEMENT
// =============================================================================

bool LogoStorage::ensureDirectoryStructure() {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    // Create all required directories
    bool success = true;
    success &= ensureDirectory(LOGOS_ROOT);
    success &= ensureDirectory(FILES_DIR);
    success &= ensureDirectory(MAPPINGS_DIR);
    success &= ensureDirectory(METADATA_DIR);

    if (success) {
        ESP_LOGD(TAG, "Directory structure verified");
    } else {
        ESP_LOGE(TAG, "Failed to create directory structure");
    }

    return success;
}

bool LogoStorage::isReady() {
    return Hardware::SD::isMounted() &&
           Hardware::SD::directoryExists(LOGOS_ROOT) &&
           Hardware::SD::directoryExists(FILES_DIR) &&
           Hardware::SD::directoryExists(MAPPINGS_DIR) &&
           Hardware::SD::directoryExists(METADATA_DIR);
}

void LogoStorage::cleanup() {
    // This could implement cleanup of orphaned files, etc.
    ESP_LOGI(TAG, "Cleanup requested - not implemented yet");
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String LogoStorage::sanitizeFileName(const String& input) {
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

bool LogoStorage::ensureDirectory(const String& path) {
    if (!Hardware::SD::directoryExists(path.c_str())) {
        ESP_LOGI(TAG, "Creating directory: %s", path.c_str());
        return Hardware::SD::createDirectory(path.c_str());
    }
    return true;
}

String LogoStorage::readJsonFile(const String& filePath) {
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

bool LogoStorage::writeJsonFile(const String& filePath, const String& content) {
    if (!ensureDirectoryStructure()) {
        return false;
    }

    auto result = Hardware::SD::writeFile(filePath.c_str(), content.c_str(), false);
    return result.success;
}

}  // namespace Logo
