#include "LogoManager.h"
#include "../hardware/SDManager.h"
#include <esp_log.h>
#include <lvgl.h>

static const char* TAG = "LogoManager";

namespace Logo {

LogoManager& LogoManager::getInstance() {
    static LogoManager instance;
    return instance;
}

bool LogoManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "LogoManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing LogoManager");

    // Ensure SD card is available
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted - logo functionality will be limited");
        // Don't fail init, just mark as not ready until SD is available
    }

    // Ensure logos directory exists
    if (!LogoBinaryStorage::getInstance().ensureLogosDirectory()) {
        ESP_LOGW(TAG, "Failed to create logos directory");
    }

    // Load index from file
    if (!LogoIndex::getInstance().loadFromFile()) {
        ESP_LOGW(TAG, "Failed to load logo index, will rebuild if needed");
    }

    // Reset statistics
    logosLoaded = 0;
    logosSaved = 0;
    logosDeleted = 0;

    initialized = true;
    ESP_LOGI(TAG, "LogoManager initialized successfully");
    return true;
}

void LogoManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing LogoManager");

    // Save any pending index changes
    LogoIndex::getInstance().saveToFile();

    initialized = false;
    ESP_LOGI(TAG, "LogoManager deinitialized");
}

bool LogoManager::saveLogo(const char* processName, const uint8_t* binaryData, size_t size) {
    if (!ensureInitialized() || !processName || !binaryData || size == 0) {
        ESP_LOGW(TAG, "Invalid parameters for saveLogo");
        return false;
    }

    // Generate filename
    String binFileName = LogoBinaryStorage::getInstance().makeFileName(processName);

    ESP_LOGI(TAG, "Saving logo for process: %s -> %s (%zu bytes)",
             processName, binFileName.c_str(), size);

    // Save binary file
    bool success = LogoBinaryStorage::getInstance().saveBinaryFile(binFileName.c_str(), binaryData, size);

    if (success) {
        // Add to index
        LogoIndex::getInstance().addEntry(processName, binFileName, size);
        logosSaved++;
        logOperation("SAVE", processName, true);
    } else {
        logOperation("SAVE", processName, false);
    }

    return success;
}

String LogoManager::getLogoPath(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return "";
    }

    String filePath = LogoIndex::getInstance().findFilePath(processName);
    if (!filePath.isEmpty()) {
        logosLoaded++;
        logOperation("GET_PATH", processName, true);
        return filePath;
    }

    logOperation("GET_PATH", processName, false);
    return "";
}

bool LogoManager::deleteLogo(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    ESP_LOGI(TAG, "Deleting logo for process: %s", processName);

    // Get binary filename from index
    String binFileName = LogoIndex::getInstance().findBinFile(processName);
    if (binFileName.isEmpty()) {
        ESP_LOGW(TAG, "No logo found for process: %s", processName);
        return false;
    }

    // Delete binary file
    bool fileDeleted = LogoBinaryStorage::getInstance().deleteBinaryFile(binFileName.c_str());

    // Remove from index (even if file deletion failed)
    bool indexRemoved = LogoIndex::getInstance().removeEntry(processName);

    bool success = fileDeleted && indexRemoved;
    if (success) {
        logosDeleted++;
        logOperation("DELETE", processName, true);
    } else {
        logOperation("DELETE", processName, false);
        ESP_LOGW(TAG, "Partial deletion: file=%s, index=%s",
                 fileDeleted ? "OK" : "FAIL", indexRemoved ? "OK" : "FAIL");
    }

    return success;
}

bool LogoManager::hasLogo(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    return LogoIndex::getInstance().hasEntry(processName);
}

bool LogoManager::loadLogoToImage(const char* processName, lv_obj_t* imgObj, bool useDefault) {
    if (!imgObj) {
        ESP_LOGW(TAG, "Invalid image object");
        return false;
    }

    String logoPath = getLogoPath(processName);

    if (!logoPath.isEmpty()) {
        // Set logo source - LVGL will load it automatically
        lv_img_set_src(imgObj, logoPath.c_str());
        ESP_LOGD(TAG, "Set logo source for %s: %s", processName, logoPath.c_str());
        return true;
    } else if (useDefault) {
        // Use default logo if available
        return setDefaultLogo(imgObj);
    }

    return false;
}

bool LogoManager::setDefaultLogo(lv_obj_t* imgObj) {
    if (!imgObj) {
        return false;
    }

    // You can set a default image here
    // For now, just clear the image
    lv_img_set_src(imgObj, nullptr);
    ESP_LOGD(TAG, "Set default (empty) logo");
    return true;
}

LogoBinaryInfo LogoManager::getLogoInfo(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return LogoBinaryInfo();
    }

    return LogoIndex::getInstance().getLogoInfo(processName);
}

size_t LogoManager::getLogoFileSize(const char* processName) {
    LogoBinaryInfo info = getLogoInfo(processName);
    return info.fileSize;
}

bool LogoManager::flagAsIncorrect(const char* processName, bool incorrect) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    bool success = LogoIndex::getInstance().setFlagged(processName, incorrect);
    if (success) {
        ESP_LOGI(TAG, "Logo flagged as %s: %s", incorrect ? "incorrect" : "correct", processName);
        logOperation("FLAG", processName, true);
    } else {
        logOperation("FLAG", processName, false);
    }

    return success;
}

bool LogoManager::markAsVerified(const char* processName, bool verified) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    bool success = LogoIndex::getInstance().setVerified(processName, verified);
    if (success) {
        ESP_LOGI(TAG, "Logo marked as %s: %s", verified ? "verified" : "unverified", processName);
        logOperation("VERIFY", processName, true);
    } else {
        logOperation("VERIFY", processName, false);
    }

    return success;
}

bool LogoManager::isVerified(const char* processName) {
    LogoBinaryInfo info = getLogoInfo(processName);
    return info.verified;
}

bool LogoManager::isFlagged(const char* processName) {
    LogoBinaryInfo info = getLogoInfo(processName);
    return info.flagged;
}

std::vector<String> LogoManager::listAvailableLogos() {
    if (!ensureInitialized()) {
        return std::vector<String>();
    }

    return LogoIndex::getInstance().listAllProcesses();
}

bool LogoManager::rebuildIndex() {
    if (!ensureInitialized()) {
        return false;
    }

    ESP_LOGI(TAG, "Rebuilding logo index from file system");
    return LogoIndex::getInstance().rebuildFromFileSystem();
}

size_t LogoManager::getLogoCount() {
    if (!ensureInitialized()) {
        return 0;
    }

    return LogoIndex::getInstance().getEntryCount();
}

String LogoManager::getSystemStatus() {
    String status = "LogoManager Status:\n";
    status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    status += "- SD Card: " + String(Hardware::SD::isMounted() ? "Mounted" : "Not mounted") + "\n";
    status += "- Logos Directory: " + String(LogoBinaryStorage::getInstance().isLogosDirectoryReady() ? "Ready" : "Not ready") + "\n";
    status += "- Logo Count: " + String(getLogoCount()) + "\n";
    status += "- Total Storage Used: " + String(getTotalStorageUsed()) + " bytes\n";
    status += "- Logos Saved: " + String(logosSaved) + "\n";
    status += "- Logos Loaded: " + String(logosLoaded) + "\n";
    status += "- Logos Deleted: " + String(logosDeleted) + "\n";

    return status;
}

bool LogoManager::cleanupOrphanedFiles() {
    if (!ensureInitialized()) {
        return false;
    }

    ESP_LOGI(TAG, "Cleaning up orphaned logo files");

    // This could be implemented to scan the filesystem and remove
    // .bin files that don't have corresponding index entries
    // For now, just rebuild the index which is safer
    return rebuildIndex();
}

size_t LogoManager::getTotalStorageUsed() {
    if (!ensureInitialized()) {
        return 0;
    }

    size_t totalSize = 0;
    auto processes = listAvailableLogos();

    for (const String& processName : processes) {
        LogoBinaryInfo info = getLogoInfo(processName.c_str());
        totalSize += info.fileSize;
    }

    return totalSize;
}

bool LogoManager::ensureInitialized() {
    if (!initialized) {
        ESP_LOGW(TAG, "LogoManager not initialized");
        return false;
    }

    return true;
}

void LogoManager::logOperation(const char* operation, const char* processName, bool success) {
    ESP_LOGD(TAG, "Operation %s for %s: %s", operation, processName, success ? "SUCCESS" : "FAILED");
}

}  // namespace Logo
