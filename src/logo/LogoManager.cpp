#include "LogoManager.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
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

    ESP_LOGI(TAG, "Initializing LogoManager with organized directory structure");

    // Ensure SD card is available
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted - logo functionality will be limited");
        // Don't fail init, just mark as not ready until SD is available
    }

    // Ensure directory structure exists
    if (!LogoBinaryStorage::getInstance().ensureDirectoryStructure()) {
        ESP_LOGW(TAG, "Failed to create directory structure");
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

    initialized = false;
    ESP_LOGI(TAG, "LogoManager deinitialized");
}

bool LogoManager::saveLogo(const char* processName, const uint8_t* binaryData, size_t size) {
    if (!ensureInitialized() || !processName || !binaryData || size == 0) {
        ESP_LOGW(TAG, "Invalid parameters for saveLogo");
        return false;
    }

    ESP_LOGI(TAG, "Saving logo for process: %s (%zu bytes)", processName, size);

    // Generate unique binary filename
    String binaryFileName = generateBinaryName(processName);

    // Save binary file
    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();
    bool success = storage.saveBinaryFile(binaryFileName, binaryData, size);

    if (success) {
        // Create process mapping
        bool mappingSuccess = storage.saveProcessMapping(processName, binaryFileName);

        // Save initial metadata
        bool metadataSuccess = storage.saveMetadata(processName, false, false, Hardware::Device::getMillis());

        if (mappingSuccess && metadataSuccess) {
            logosSaved++;
            logOperation("SAVE", processName, true);
            ESP_LOGI(TAG, "Successfully saved logo: %s -> %s", processName, binaryFileName.c_str());
        } else {
            // Cleanup binary file if mapping/metadata failed
            storage.deleteBinaryFile(binaryFileName);
            success = false;
            logOperation("SAVE", processName, false);
        }
    } else {
        logOperation("SAVE", processName, false);
    }

    return success;
}

String LogoManager::getLogoPath(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return "";
    }

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();
    String binaryFileName = storage.getProcessMapping(processName);

    if (!binaryFileName.isEmpty()) {
        logosLoaded++;
        logOperation("GET_PATH", processName, true);
        return storage.getBinaryPath(binaryFileName);
    }

    logOperation("GET_PATH", processName, false);
    return "";
}

bool LogoManager::deleteLogo(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    ESP_LOGI(TAG, "Deleting logo for process: %s", processName);

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get binary filename from mapping
    String binaryFileName = storage.getProcessMapping(processName);
    if (binaryFileName.isEmpty()) {
        ESP_LOGW(TAG, "No logo mapping found for process: %s", processName);
        return false;
    }

    // Delete binary file
    bool binaryDeleted = storage.deleteBinaryFile(binaryFileName);

    // Delete mapping
    bool mappingDeleted = storage.deleteProcessMapping(processName);

    // Delete metadata
    bool metadataDeleted = storage.deleteMetadata(processName);

    bool success = binaryDeleted && mappingDeleted && metadataDeleted;
    if (success) {
        logosDeleted++;
        logOperation("DELETE", processName, true);
        ESP_LOGI(TAG, "Successfully deleted logo: %s", processName);
    } else {
        logOperation("DELETE", processName, false);
        ESP_LOGW(TAG, "Partial deletion for %s: binary=%s, mapping=%s, metadata=%s",
                 processName, binaryDeleted ? "OK" : "FAIL",
                 mappingDeleted ? "OK" : "FAIL", metadataDeleted ? "OK" : "FAIL");
    }

    return success;
}

bool LogoManager::hasLogo(const char* processName) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    return LogoBinaryStorage::getInstance().hasProcessMapping(processName);
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
    LogoBinaryInfo info;

    if (!ensureInitialized() || !processName) {
        return info;
    }

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get binary filename from mapping
    String binaryFileName = storage.getProcessMapping(processName);
    if (binaryFileName.isEmpty()) {
        return info;
    }

    // Fill basic info
    info.processName = processName;
    info.binaryFileName = binaryFileName;
    info.binaryPath = storage.getBinaryPath(binaryFileName);
    info.fileSize = storage.getBinaryFileSize(binaryFileName);

    // Get metadata
    bool verified, flagged;
    uint64_t timestamp;
    if (storage.getMetadata(processName, verified, flagged, timestamp)) {
        info.verified = verified;
        info.flagged = flagged;
        info.timestamp = timestamp;
    }

    return info;
}

size_t LogoManager::getLogoFileSize(const char* processName) {
    LogoBinaryInfo info = getLogoInfo(processName);
    return info.fileSize;
}

bool LogoManager::flagAsIncorrect(const char* processName, bool incorrect) {
    if (!ensureInitialized() || !processName) {
        return false;
    }

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get current metadata
    bool verified, flagged;
    uint64_t timestamp;
    if (!storage.getMetadata(processName, verified, flagged, timestamp)) {
        // No existing metadata, create default
        verified = false;
        timestamp = Hardware::Device::getMillis();
    }

    // Update flagged status
    bool success = storage.saveMetadata(processName, verified, incorrect, timestamp);

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

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get current metadata
    bool currentVerified, flagged;
    uint64_t timestamp;
    if (!storage.getMetadata(processName, currentVerified, flagged, timestamp)) {
        // No existing metadata, create default
        flagged = false;
        timestamp = Hardware::Device::getMillis();
    }

    // Update verified status
    bool success = storage.saveMetadata(processName, verified, flagged, timestamp);

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

    return LogoBinaryStorage::getInstance().listMappedProcesses();
}

bool LogoManager::rebuildMappings() {
    if (!ensureInitialized()) {
        return false;
    }

    ESP_LOGI(TAG, "Rebuilding logo mappings from binary files");

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get list of binary files
    std::vector<String> binaryFiles = storage.listBinaryFiles();

    ESP_LOGI(TAG, "Found %zu binary files to process", binaryFiles.size());

    // For each binary file, try to create a reasonable mapping
    for (const String& binaryFile : binaryFiles) {
        // Extract base name (remove .bin extension)
        String baseName = binaryFile;
        if (baseName.endsWith(".bin")) {
            baseName = baseName.substring(0, baseName.length() - 4);
        }

        // Remove version suffixes (_v1, _v2, etc.)
        int versionPos = baseName.lastIndexOf("_v");
        if (versionPos > 0) {
            baseName = baseName.substring(0, versionPos);
        }

        // Convert to reasonable process name
        String processName = baseName + ".exe";  // Default assumption

        // Only create mapping if none exists
        if (!storage.hasProcessMapping(processName)) {
            storage.saveProcessMapping(processName, binaryFile);
            ESP_LOGD(TAG, "Created mapping: %s -> %s", processName.c_str(), binaryFile.c_str());
        }
    }

    ESP_LOGI(TAG, "Mapping rebuild complete");
    return true;
}

size_t LogoManager::getLogoCount() {
    if (!ensureInitialized()) {
        return 0;
    }

    return LogoBinaryStorage::getInstance().listMappedProcesses().size();
}

String LogoManager::getSystemStatus() {
    String status = "LogoManager Status:\n";
    status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    status += "- SD Card: " + String(Hardware::SD::isMounted() ? "Mounted" : "Not mounted") + "\n";
    status += "- Directory Structure: " + String(LogoBinaryStorage::getInstance().isReady() ? "Ready" : "Not ready") + "\n";
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

    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Get all binary files and mapped processes
    std::vector<String> binaryFiles = storage.listBinaryFiles();
    std::vector<String> mappedProcesses = storage.listMappedProcesses();

    // Find binary files that are not referenced by any mapping
    std::vector<String> orphanedBinaries;
    for (const String& binaryFile : binaryFiles) {
        bool referenced = false;

        for (const String& processName : mappedProcesses) {
            String mappedBinary = storage.getProcessMapping(processName);
            if (mappedBinary == binaryFile) {
                referenced = true;
                break;
            }
        }

        if (!referenced) {
            orphanedBinaries.push_back(binaryFile);
        }
    }

    // Delete orphaned binary files
    for (const String& orphan : orphanedBinaries) {
        ESP_LOGI(TAG, "Deleting orphaned binary: %s", orphan.c_str());
        storage.deleteBinaryFile(orphan);
    }

    ESP_LOGI(TAG, "Cleanup complete. Removed %zu orphaned files", orphanedBinaries.size());
    return true;
}

size_t LogoManager::getTotalStorageUsed() {
    if (!ensureInitialized()) {
        return 0;
    }

    size_t totalSize = 0;
    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    std::vector<String> binaryFiles = storage.listBinaryFiles();
    for (const String& binaryFile : binaryFiles) {
        totalSize += storage.getBinaryFileSize(binaryFile);
    }

    return totalSize;
}

// =============================================================================
// PRIVATE HELPER METHODS
// =============================================================================

bool LogoManager::ensureInitialized() {
    if (!initialized) {
        ESP_LOGW(TAG, "LogoManager not initialized");
        return false;
    }
    return true;
}

void LogoManager::logOperation(const String& operation, const char* processName, bool success) {
    const char* status = success ? "SUCCESS" : "FAILED";
    ESP_LOGD(TAG, "Operation %s for %s: %s", operation.c_str(), processName, status);
}

String LogoManager::generateBinaryName(const char* processName) {
    LogoBinaryStorage& storage = LogoBinaryStorage::getInstance();

    // Use the storage's sanitization and unique name generation
    String baseName = storage.sanitizeFileName(processName);

    // Remove common extensions for cleaner names
    baseName.replace(".exe", "");
    baseName.replace(".app", "");

    return storage.generateUniqueBinaryName(baseName);
}

}  // namespace Logo
