#include "LogoIndex.h"
#include "LogoBinaryStorage.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>

static const char* TAG = "LogoIndex";

namespace Logo {

// Static constants
const char* LogoIndex::INDEX_FILE_PATH = "/logos/index.json";
const int LogoIndex::INDEX_VERSION = 1;

LogoIndex& LogoIndex::getInstance() {
    static LogoIndex instance;
    return instance;
}

bool LogoIndex::loadFromFile() {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot load index");
        return false;
    }

    if (!Hardware::SD::fileExists(INDEX_FILE_PATH)) {
        ESP_LOGI(TAG, "Index file does not exist, starting with empty index");
        logoEntries.clear();
        indexLoaded = true;
        return true;
    }

    ESP_LOGI(TAG, "Loading logo index from: %s", INDEX_FILE_PATH);

    // Read the JSON file
    char buffer[4096];  // Reasonable size for index file
    auto result = Hardware::SD::readFile(INDEX_FILE_PATH, buffer, sizeof(buffer));

    if (!result.success) {
        ESP_LOGE(TAG, "Failed to read index file: %s", result.errorMessage);
        return false;
    }

    String jsonString = String(buffer);
    bool success = parseJsonToIndex(jsonString);

    if (success) {
        indexLoaded = true;
        ESP_LOGI(TAG, "Successfully loaded %zu logo entries from index", logoEntries.size());
    } else {
        ESP_LOGE(TAG, "Failed to parse index JSON");
    }

    return success;
}

bool LogoIndex::saveToFile() {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot save index");
        return false;
    }

    if (!LogoBinaryStorage::getInstance().ensureLogosDirectory()) {
        ESP_LOGE(TAG, "Failed to ensure logos directory exists");
        return false;
    }

    ESP_LOGI(TAG, "Saving logo index to: %s (%zu entries)", INDEX_FILE_PATH, logoEntries.size());

    String jsonString = createJsonFromIndex();

    auto result = Hardware::SD::writeFile(INDEX_FILE_PATH, jsonString.c_str(), false);

    if (result.success) {
        ESP_LOGI(TAG, "Successfully saved logo index");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to save index file: %s", result.errorMessage);
        return false;
    }
}

bool LogoIndex::rebuildFromFileSystem() {
    ESP_LOGI(TAG, "Rebuilding logo index from file system");

    logoEntries.clear();

    if (!LogoBinaryStorage::getInstance().isLogosDirectoryReady()) {
        ESP_LOGW(TAG, "Logos directory not ready for rebuilding");
        return false;
    }

    // List all .bin files in logos directory
    bool success = Hardware::SD::listDirectory("/logos", [this](const char* name, bool isDir, size_t size) {
        if (!isDir && String(name).endsWith(".bin") && String(name).startsWith("process_")) {
            // Extract process name from filename
            String filename = String(name);
            String processName = filename.substring(8);                        // Remove "process_" prefix
            processName = processName.substring(0, processName.length() - 4);  // Remove ".bin" suffix

            // Convert back from sanitized format (basic reverse)
            processName.replace("_", ".");
            if (!processName.endsWith(".exe") && !processName.endsWith(".app")) {
                processName += ".exe";  // Default assumption
            }

            // Add entry
            LogoBinaryInfo info;
            info.processName = processName;
            info.binFileName = filename;
            info.filePath = "S:/logos/" + filename;
            info.fileSize = size;
            info.timestamp = Hardware::Device::getMillis();

            logoEntries[processName] = info;

            ESP_LOGD(TAG, "Rebuilt entry: %s -> %s", processName.c_str(), filename.c_str());
        }
    });

    if (success) {
        ESP_LOGI(TAG, "Rebuilt index with %zu entries", logoEntries.size());
        indexLoaded = true;
        saveToFile();  // Persist the rebuilt index
    }

    return success;
}

bool LogoIndex::addEntry(const String& processName, const String& binFileName, size_t fileSize) {
    if (processName.isEmpty() || binFileName.isEmpty()) {
        return false;
    }

    LogoBinaryInfo info;
    info.processName = processName;
    info.binFileName = binFileName;
    info.filePath = "S:/logos/" + binFileName;
    info.fileSize = fileSize;
    info.timestamp = Hardware::Device::getMillis();

    logoEntries[processName] = info;

    ESP_LOGD(TAG, "Added index entry: %s -> %s", processName.c_str(), binFileName.c_str());

    // Auto-save after adding entry
    saveToFile();

    return true;
}

bool LogoIndex::removeEntry(const String& processName) {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        ESP_LOGD(TAG, "Removed index entry: %s", processName.c_str());
        logoEntries.erase(it);
        saveToFile();  // Auto-save after removal
        return true;
    }
    return false;
}

bool LogoIndex::hasEntry(const String& processName) const {
    return logoEntries.find(processName) != logoEntries.end();
}

String LogoIndex::findBinFile(const String& processName) const {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        return it->second.binFileName;
    }
    return "";
}

String LogoIndex::findFilePath(const String& processName) const {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        return it->second.filePath;
    }
    return "";
}

LogoBinaryInfo LogoIndex::getLogoInfo(const String& processName) const {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        return it->second;
    }
    return LogoBinaryInfo();  // Return empty info
}

bool LogoIndex::setVerified(const String& processName, bool verified) {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        it->second.verified = verified;
        saveToFile();
        return true;
    }
    return false;
}

bool LogoIndex::setFlagged(const String& processName, bool flagged) {
    auto it = logoEntries.find(processName);
    if (it != logoEntries.end()) {
        it->second.flagged = flagged;
        saveToFile();
        return true;
    }
    return false;
}

std::vector<String> LogoIndex::listAllProcesses() const {
    std::vector<String> processes;
    for (const auto& entry : logoEntries) {
        processes.push_back(entry.first);
    }
    return processes;
}

size_t LogoIndex::getEntryCount() const {
    return logoEntries.size();
}

void LogoIndex::clearAll() {
    logoEntries.clear();
    saveToFile();
}

bool LogoIndex::parseJsonToIndex(const String& jsonString) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", error.c_str());
        return false;
    }

    // Check version
    int version = doc["version"] | 0;
    if (version != INDEX_VERSION) {
        ESP_LOGW(TAG, "Index version mismatch: %d (expected %d)", version, INDEX_VERSION);
    }

    // Parse logos object
    JsonObject logos = doc["logos"];
    if (!logos) {
        ESP_LOGW(TAG, "No logos object found in JSON");
        return true;  // Empty index is valid
    }

    logoEntries.clear();

    for (JsonPair entry : logos) {
        String processName = entry.key().c_str();
        JsonObject logoObj = entry.value();

        LogoBinaryInfo info;
        info.processName = processName;
        info.binFileName = logoObj["binFile"] | "";
        info.filePath = "S:/logos/" + info.binFileName;
        info.fileSize = logoObj["size"] | 0;
        info.verified = logoObj["verified"] | false;
        info.flagged = logoObj["flagged"] | false;
        info.timestamp = logoObj["timestamp"] | 0;

        if (!info.binFileName.isEmpty()) {
            logoEntries[processName] = info;
        }
    }

    return true;
}

String LogoIndex::createJsonFromIndex() const {
    JsonDocument doc;

    doc["version"] = INDEX_VERSION;
    JsonObject logos = doc["logos"].to<JsonObject>();

    for (const auto& entry : logoEntries) {
        const String& processName = entry.first;
        const LogoBinaryInfo& info = entry.second;

        JsonObject logoObj = logos[processName].to<JsonObject>();
        logoObj["binFile"] = info.binFileName;
        logoObj["size"] = info.fileSize;
        logoObj["verified"] = info.verified;
        logoObj["flagged"] = info.flagged;
        logoObj["timestamp"] = info.timestamp;
    }

    String jsonString;
    serializeJsonPretty(doc, jsonString);
    return jsonString;
}

}  // namespace Logo
