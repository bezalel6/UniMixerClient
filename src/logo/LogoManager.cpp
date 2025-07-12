// #include "LogoManager.h"
// #include "../hardware/SDManager.h"
// #include "../hardware/DeviceManager.h"
// #include <esp_log.h>
// #include <lvgl.h>

// static const char* TAG = "LogoManager";

// namespace Logo {

// LogoManager& LogoManager::getInstance() {
//     static LogoManager instance;
//     return instance;
// }

// bool LogoManager::init() {
//     if (initialized) {
//         ESP_LOGW(TAG, "LogoManager already initialized");
//         return true;
//     }

//     ESP_LOGI(TAG, "Initializing LogoManager with organized directory
//     structure");

//     // Ensure SD card is available
//     if (!Hardware::SD::isMounted()) {
//         ESP_LOGW(TAG, "SD card not mounted - logo functionality will be
//         limited");
//         // Don't fail init, just mark as not ready until SD is available
//     }

//     // Ensure directory structure exists
//     if (!LogoStorage::getInstance().ensureDirectoryStructure()) {
//         ESP_LOGW(TAG, "Failed to create directory structure");
//     }

//     // Reset statistics
//     logosLoaded = 0;
//     logosSaved = 0;
//     logosDeleted = 0;

//     initialized = true;
//     ESP_LOGI(TAG, "LogoManager initialized successfully");
//     return true;
// }

// void LogoManager::deinit() {
//     if (!initialized) {
//         return;
//     }

//     ESP_LOGI(TAG, "Deinitializing LogoManager");

//     initialized = false;
//     ESP_LOGI(TAG, "LogoManager deinitialized");
// }

// String LogoManager::saveLogo(const char* processName, const uint8_t* data,
// size_t size, LogoStorage::FileType type) {
//     if (!ensureInitialized() || !processName || !data || size == 0) {
//         ESP_LOGW(TAG, "Invalid parameters for saveLogo");
//         return "";
//     }

//     ESP_LOGI(TAG, "Saving %s logo for process: %s (%zu bytes)",
//              (type == LogoStorage::FileType::PNG) ? "PNG" : "binary",
//              processName, size);

//     // Generate unique filename
//     String fileName = generateFileName(processName, type);

//     // Save file
//     LogoStorage& storage = LogoStorage::getInstance();
//     bool success = storage.saveFile(fileName, data, size);

//     if (success) {
//         // Create process mapping
//         bool mappingSuccess = storage.saveProcessMapping(processName,
//         fileName);

//         // Save initial metadata
//         bool metadataSuccess = storage.saveMetadata(processName, false,
//         false, Hardware::Device::getMillis());

//         if (mappingSuccess && metadataSuccess) {
//             logosSaved++;
//             logOperation("SAVE", processName, true);
//             String logoPath = storage.getFilePath(fileName);
//             ESP_LOGI(TAG, "Successfully saved logo: %s -> %s (path: %s)",
//             processName, fileName.c_str(), logoPath.c_str()); return
//             logoPath;
//         } else {
//             // Cleanup file if mapping/metadata failed
//             storage.deleteFile(fileName);
//             logOperation("SAVE", processName, false);
//         }
//     } else {
//         logOperation("SAVE", processName, false);
//     }

//     return "";
// }

// String LogoManager::saveLogoFromFile(const char* processName, const char*
// sourceFilePath) {
//     if (!ensureInitialized() || !processName || !sourceFilePath) {
//         ESP_LOGW(TAG, "Invalid parameters for saveLogoFromFile");
//         return "";
//     }

//     // Auto-detect file type from extension
//     String filePath = String(sourceFilePath);
//     LogoStorage::FileType type = LogoStorage::FileType::BINARY;
//     if (filePath.endsWith(".png")) {
//         type = LogoStorage::FileType::PNG;
//     } else if (filePath.endsWith(".bin")) {
//         type = LogoStorage::FileType::BINARY;
//     } else {
//         ESP_LOGW(TAG, "Unsupported file type for: %s", sourceFilePath);
//         return "";
//     }

//     // Read file data
//     File file = Hardware::SD::openFile(sourceFilePath, "rb");
//     if (!file) {
//         ESP_LOGE(TAG, "Failed to open source file: %s", sourceFilePath);
//         return "";
//     }

//     size_t fileSize = file.size();
//     if (fileSize == 0) {
//         file.close();
//         ESP_LOGW(TAG, "Source file is empty: %s", sourceFilePath);
//         return "";
//     }

//     // Allocate buffer and read data
//     uint8_t* buffer = (uint8_t*)malloc(fileSize);
//     if (!buffer) {
//         file.close();
//         ESP_LOGE(TAG, "Failed to allocate memory for file: %s",
//         sourceFilePath); return "";
//     }

//     size_t bytesRead = file.read(buffer, fileSize);
//     file.close();

//     if (bytesRead != fileSize) {
//         free(buffer);
//         ESP_LOGE(TAG, "Failed to read complete file: %s", sourceFilePath);
//         return "";
//     }

//     // Save logo
//     String result = saveLogo(processName, buffer, fileSize, type);
//     free(buffer);

//     return result;
// }

// String LogoManager::getLogoPath(const char* processName) {
//     if (!ensureInitialized() || !processName) {
//         return "";
//     }

//     LogoStorage& storage = LogoStorage::getInstance();
//     String binaryFileName = storage.getProcessMapping(processName);

//     if (!binaryFileName.isEmpty()) {
//         logosLoaded++;
//         logOperation("GET_PATH", processName, true);
//         return storage.getFilePath(binaryFileName);
//     }

//     logOperation("GET_PATH", processName, false);
//     return "";
// }

// bool LogoManager::deleteLogo(const char* processName) {
//     if (!ensureInitialized() || !processName) {
//         return false;
//     }

//     ESP_LOGI(TAG, "Deleting logo for process: %s", processName);

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get binary filename from mapping
//     String binaryFileName = storage.getProcessMapping(processName);
//     if (binaryFileName.isEmpty()) {
//         ESP_LOGW(TAG, "No logo mapping found for process: %s", processName);
//         return false;
//     }

//     // Delete binary file
//     bool binaryDeleted = storage.deleteFile(binaryFileName);

//     // Delete mapping
//     bool mappingDeleted = storage.deleteProcessMapping(processName);

//     // Delete metadata
//     bool metadataDeleted = storage.deleteMetadata(processName);

//     bool success = binaryDeleted && mappingDeleted && metadataDeleted;
//     if (success) {
//         logosDeleted++;
//         logOperation("DELETE", processName, true);
//         ESP_LOGI(TAG, "Successfully deleted logo: %s", processName);
//     } else {
//         logOperation("DELETE", processName, false);
//         ESP_LOGW(TAG, "Partial deletion for %s: binary=%s, mapping=%s,
//         metadata=%s",
//                  processName, binaryDeleted ? "OK" : "FAIL",
//                  mappingDeleted ? "OK" : "FAIL", metadataDeleted ? "OK" :
//                  "FAIL");
//     }

//     return success;
// }

// bool LogoManager::hasLogo(const char* processName) {
//     if (!ensureInitialized() || !processName) {
//         return false;
//     }

//     return LogoStorage::getInstance().hasProcessMapping(processName);
// }

// bool LogoManager::loadLogoToImage(const char* processName, lv_obj_t* imgObj,
// bool useDefault) {
//     if (!imgObj) {
//         ESP_LOGW(TAG, "Invalid image object");
//         return false;
//     }

//     String logoPath = getLogoPath(processName);

//     if (!logoPath.isEmpty()) {
//         // Set logo source - LVGL will load it automatically
//         lv_img_set_src(imgObj, logoPath.c_str());
//         ESP_LOGD(TAG, "Set logo source for %s: %s", processName,
//         logoPath.c_str()); return true;
//     } else if (useDefault) {
//         // Use default logo if available
//         return setDefaultLogo(imgObj);
//     }

//     return false;
// }

// bool LogoManager::setDefaultLogo(lv_obj_t* imgObj) {
//     if (!imgObj) {
//         return false;
//     }

//     // You can set a default image here
//     // For now, just clear the image
//     lv_img_set_src(imgObj, nullptr);
//     ESP_LOGD(TAG, "Set default (empty) logo");
//     return true;
// }

// LogoFileInfo LogoManager::getLogoInfo(const char* processName) {
//     LogoFileInfo info = {};

//     if (!ensureInitialized() || !processName) {
//         return info;
//     }

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get filename from mapping
//     String fileName = storage.getProcessMapping(processName);
//     if (fileName.isEmpty()) {
//         return info;
//     }

//     // Fill basic info
//     info.processName = processName;
//     info.fileName = fileName;
//     info.filePath = storage.getFilePath(fileName);
//     info.fileSize = storage.getFileSize(fileName);
//     info.fileType = storage.getFileType(fileName);

//     // Get metadata
//     bool verified, flagged;
//     uint64_t timestamp;
//     if (storage.getMetadata(processName, verified, flagged, timestamp)) {
//         info.verified = verified;
//         info.flagged = flagged;
//         info.timestamp = timestamp;
//     }

//     return info;
// }

// size_t LogoManager::getLogoFileSize(const char* processName) {
//     LogoFileInfo info = getLogoInfo(processName);
//     return info.fileSize;
// }

// LogoStorage::FileType LogoManager::getLogoType(const char* processName) {
//     LogoFileInfo info = getLogoInfo(processName);
//     return info.fileType;
// }

// bool LogoManager::flagAsIncorrect(const char* processName, bool incorrect) {
//     if (!ensureInitialized() || !processName) {
//         return false;
//     }

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get current metadata
//     bool verified, flagged;
//     uint64_t timestamp;
//     if (!storage.getMetadata(processName, verified, flagged, timestamp)) {
//         // No existing metadata, create default
//         verified = false;
//         timestamp = Hardware::Device::getMillis();
//     }

//     // Update flagged status
//     bool success = storage.saveMetadata(processName, verified, incorrect,
//     timestamp);

//     if (success) {
//         ESP_LOGI(TAG, "Logo flagged as %s: %s", incorrect ? "incorrect" :
//         "correct", processName); logOperation("FLAG", processName, true);
//     } else {
//         logOperation("FLAG", processName, false);
//     }

//     return success;
// }

// bool LogoManager::markAsVerified(const char* processName, bool verified) {
//     if (!ensureInitialized() || !processName) {
//         return false;
//     }

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get current metadata
//     bool currentVerified, flagged;
//     uint64_t timestamp;
//     if (!storage.getMetadata(processName, currentVerified, flagged,
//     timestamp)) {
//         // No existing metadata, create default
//         flagged = false;
//         timestamp = Hardware::Device::getMillis();
//     }

//     // Update verified status
//     bool success = storage.saveMetadata(processName, verified, flagged,
//     timestamp);

//     if (success) {
//         ESP_LOGI(TAG, "Logo marked as %s: %s", verified ? "verified" :
//         "unverified", processName); logOperation("VERIFY", processName,
//         true);
//     } else {
//         logOperation("VERIFY", processName, false);
//     }

//     return success;
// }

// bool LogoManager::isVerified(const char* processName) {
//     LogoFileInfo info = getLogoInfo(processName);
//     return info.verified;
// }

// bool LogoManager::isFlagged(const char* processName) {
//     LogoFileInfo info = getLogoInfo(processName);
//     return info.flagged;
// }

// std::vector<String> LogoManager::listAvailableLogos() {
//     if (!ensureInitialized()) {
//         return std::vector<String>();
//     }

//     return LogoStorage::getInstance().listMappedProcesses();
// }

// std::vector<String> LogoManager::listLogosByType(LogoStorage::FileType type)
// {
//     if (!ensureInitialized()) {
//         return std::vector<String>();
//     }

//     std::vector<String> processesWithType;
//     LogoStorage& storage = LogoStorage::getInstance();

//     std::vector<String> allProcesses = storage.listMappedProcesses();
//     for (const String& processName : allProcesses) {
//         String fileName = storage.getProcessMapping(processName);
//         if (!fileName.isEmpty() && storage.getFileType(fileName) == type) {
//             processesWithType.push_back(processName);
//         }
//     }

//     return processesWithType;
// }

// bool LogoManager::rebuildMappings() {
//     if (!ensureInitialized()) {
//         return false;
//     }

//     ESP_LOGI(TAG, "Rebuilding logo mappings from existing files");

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get list of logo files
//     std::vector<String> logoFiles = storage.listFiles();

//     ESP_LOGI(TAG, "Found %zu logo files to process", logoFiles.size());

//     // For each logo file, try to create a reasonable mapping
//     for (const String& logoFile : logoFiles) {
//         // Extract base name (remove file extension)
//         String baseName = logoFile;
//         if (baseName.endsWith(".bin")) {
//             baseName = baseName.substring(0, baseName.length() - 4);
//         } else if (baseName.endsWith(".png")) {
//             baseName = baseName.substring(0, baseName.length() - 4);
//         }

//         // Remove version suffixes (_v1, _v2, etc.)
//         int versionPos = baseName.lastIndexOf("_v");
//         if (versionPos > 0) {
//             baseName = baseName.substring(0, versionPos);
//         }

//         // Convert to reasonable process name
//         String processName = baseName + ".exe";  // Default assumption

//         // Only create mapping if none exists
//         if (!storage.hasProcessMapping(processName)) {
//             storage.saveProcessMapping(processName, logoFile);
//             ESP_LOGD(TAG, "Created mapping: %s -> %s", processName.c_str(),
//             logoFile.c_str());
//         }
//     }

//     ESP_LOGI(TAG, "Mapping rebuild complete");
//     return true;
// }

// size_t LogoManager::getLogoCount() {
//     if (!ensureInitialized()) {
//         return 0;
//     }

//     return LogoStorage::getInstance().listMappedProcesses().size();
// }

// String LogoManager::getSystemStatus() {
//     String status = "LogoManager Status:\n";
//     status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
//     status += "- SD Card: " + String(Hardware::SD::isMounted() ? "Mounted" :
//     "Not mounted") + "\n"; status += "- Directory Structure: " +
//     String(LogoStorage::getInstance().isReady() ? "Ready" : "Not ready") +
//     "\n"; status += "- Logo Count: " + String(getLogoCount()) + "\n"; status
//     += "- Total Storage Used: " + String(getTotalStorageUsed()) + " bytes\n";
//     status += "- Logos Saved: " + String(logosSaved) + "\n";
//     status += "- Logos Loaded: " + String(logosLoaded) + "\n";
//     status += "- Logos Deleted: " + String(logosDeleted) + "\n";

//     return status;
// }

// bool LogoManager::cleanupOrphanedFiles() {
//     if (!ensureInitialized()) {
//         return false;
//     }

//     ESP_LOGI(TAG, "Cleaning up orphaned logo files");

//     LogoStorage& storage = LogoStorage::getInstance();

//     // Get all logo files and mapped processes
//     std::vector<String> logoFiles = storage.listFiles();
//     std::vector<String> mappedProcesses = storage.listMappedProcesses();

//     // Find logo files that are not referenced by any mapping
//     std::vector<String> orphanedFiles;
//     for (const String& logoFile : logoFiles) {
//         bool referenced = false;

//         for (const String& processName : mappedProcesses) {
//             String mappedFile = storage.getProcessMapping(processName);
//             if (mappedFile == logoFile) {
//                 referenced = true;
//                 break;
//             }
//         }

//         if (!referenced) {
//             orphanedFiles.push_back(logoFile);
//         }
//     }

//     // Delete orphaned logo files
//     for (const String& orphan : orphanedFiles) {
//         ESP_LOGI(TAG, "Deleting orphaned logo file: %s", orphan.c_str());
//         storage.deleteFile(orphan);
//     }

//     ESP_LOGI(TAG, "Cleanup complete. Removed %zu orphaned files",
//     orphanedFiles.size()); return true;
// }

// size_t LogoManager::getTotalStorageUsed() {
//     if (!ensureInitialized()) {
//         return 0;
//     }

//     size_t totalSize = 0;
//     LogoStorage& storage = LogoStorage::getInstance();

//     std::vector<String> logoFiles = storage.listFiles();
//     for (const String& logoFile : logoFiles) {
//         totalSize += storage.getFileSize(logoFile);
//     }

//     return totalSize;
// }

// //
// =============================================================================
// // PRIVATE HELPER METHODS
// //
// =============================================================================

// bool LogoManager::ensureInitialized() {
//     if (!initialized) {
//         ESP_LOGW(TAG, "LogoManager not initialized");
//         return false;
//     }
//     return true;
// }

// void LogoManager::logOperation(const String& operation, const char*
// processName, bool success) {
//     const char* status = success ? "SUCCESS" : "FAILED";
//     ESP_LOGD(TAG, "Operation %s for %s: %s", operation.c_str(), processName,
//     status);
// }

// String LogoManager::generateFileName(const char* processName,
// LogoStorage::FileType type) {
//     LogoStorage& storage = LogoStorage::getInstance();

//     // Use the storage's sanitization and unique name generation
//     String baseName = storage.sanitizeFileName(processName);

//     // Remove common extensions for cleaner names
//     baseName.replace(".exe", "");
//     baseName.replace(".app", "");

//     return storage.generateUniqueFileName(baseName, type);
// }

// }  // namespace Logo
