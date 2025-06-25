#include "LogoBinaryStorage.h"
#include "../hardware/SDManager.h"
#include <esp_log.h>

static const char* TAG = "LogoBinaryStorage";

namespace Logo {

// Static constants
const char* LogoBinaryStorage::LOGOS_DIR = "/logos";
const char* LogoBinaryStorage::LOGOS_PREFIX = "process_";
const char* LogoBinaryStorage::BIN_EXTENSION = ".bin";

LogoBinaryStorage& LogoBinaryStorage::getInstance() {
    static LogoBinaryStorage instance;
    return instance;
}

bool LogoBinaryStorage::saveBinaryFile(const char* filename, const uint8_t* data, size_t size) {
    if (!filename || !data || size == 0) {
        ESP_LOGW(TAG, "Invalid parameters for saveBinaryFile");
        return false;
    }

    if (!ensureLogosDirectory()) {
        ESP_LOGE(TAG, "Failed to ensure logos directory exists");
        return false;
    }

    // Construct full path
    String fullPath = String(LOGOS_DIR) + "/" + String(filename);

    ESP_LOGI(TAG, "Saving binary file: %s (%zu bytes)", fullPath.c_str(), size);

    // Open file for binary writing
    File file = Hardware::SD::openFile(fullPath.c_str(), "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", fullPath.c_str());
        return false;
    }

    // Write binary data
    size_t bytesWritten = file.write(data, size);
    file.close();

    if (bytesWritten != size) {
        ESP_LOGE(TAG, "Failed to write complete data. Written: %zu, Expected: %zu",
                 bytesWritten, size);
        // Clean up partial file
        Hardware::SD::deleteFile(fullPath.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Successfully saved binary file: %s (%zu bytes)",
             fullPath.c_str(), bytesWritten);
    return true;
}

bool LogoBinaryStorage::deleteBinaryFile(const char* filename) {
    if (!filename) {
        return false;
    }

    String fullPath = String(LOGOS_DIR) + "/" + String(filename);
    ESP_LOGI(TAG, "Deleting binary file: %s", fullPath.c_str());

    auto result = Hardware::SD::deleteFile(fullPath.c_str());
    return result.success;
}

bool LogoBinaryStorage::fileExists(const char* filename) {
    if (!filename) {
        return false;
    }

    String fullPath = String(LOGOS_DIR) + "/" + String(filename);
    return Hardware::SD::fileExists(fullPath.c_str());
}

size_t LogoBinaryStorage::getFileSize(const char* filename) {
    if (!filename) {
        return 0;
    }

    String fullPath = String(LOGOS_DIR) + "/" + String(filename);
    return Hardware::SD::getFileSize(fullPath.c_str());
}

String LogoBinaryStorage::makeLogoPath(const char* processName) {
    if (!processName) {
        return "";
    }

    String filename = makeFileName(processName);
    return "S:" + String(LOGOS_DIR) + "/" + filename;  // LVGL format path
}

String LogoBinaryStorage::makeFileName(const char* processName) {
    if (!processName) {
        return "";
    }

    String sanitized = sanitizeProcessName(processName);
    return String(LOGOS_PREFIX) + sanitized + String(BIN_EXTENSION);
}

String LogoBinaryStorage::sanitizeProcessName(const char* processName) {
    if (!processName) {
        return "";
    }

    String sanitized = String(processName);

    // Remove common executable extensions
    sanitized.replace(".exe", "");
    sanitized.replace(".app", "");

    // Replace problematic characters with underscores
    sanitized.replace(".", "_");
    sanitized.replace(" ", "_");
    sanitized.replace("/", "_");
    sanitized.replace("\\", "_");
    sanitized.replace(":", "_");
    sanitized.replace("*", "_");
    sanitized.replace("?", "_");
    sanitized.replace("\"", "_");
    sanitized.replace("<", "_");
    sanitized.replace(">", "_");
    sanitized.replace("|", "_");

    // Convert to lowercase for consistency
    sanitized.toLowerCase();

    // Limit length to reasonable size
    if (sanitized.length() > 50) {
        sanitized = sanitized.substring(0, 50);
    }

    return sanitized;
}

bool LogoBinaryStorage::ensureLogosDirectory() {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    if (!Hardware::SD::directoryExists(LOGOS_DIR)) {
        ESP_LOGI(TAG, "Creating logos directory: %s", LOGOS_DIR);
        if (!Hardware::SD::createDirectory(LOGOS_DIR)) {
            ESP_LOGE(TAG, "Failed to create logos directory");
            return false;
        }
    }

    return true;
}

bool LogoBinaryStorage::isLogosDirectoryReady() {
    return Hardware::SD::isMounted() && Hardware::SD::directoryExists(LOGOS_DIR);
}

}  // namespace Logo
