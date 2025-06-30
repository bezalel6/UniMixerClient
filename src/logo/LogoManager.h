#ifndef LOGO_MANAGER_H
#define LOGO_MANAGER_H

#include <Arduino.h>
#include <functional>
#include "LogoStorage.h"

// Forward declaration for LVGL
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace Logo {

/**
 * Logo information structure (generic for both binary and PNG)
 */
struct LogoFileInfo {
    String processName;
    String fileName;  // Was binaryFileName
    String filePath;  // Was binaryPath
    size_t fileSize;
    bool verified;
    bool flagged;
    uint64_t timestamp;
    LogoStorage::FileType fileType;
};

// Keep old name for backward compatibility
using LogoBinaryInfo = LogoFileInfo;

/**
 * Main coordinator for LVGL logo management system
 * Provides high-level interface for saving, loading, and managing logo binary files
 * Uses organized directory structure: /logos/binaries/, /logos/mappings/, /logos/metadata/
 */
class LogoManager {
   public:
    static LogoManager& getInstance();

    // Initialization and lifecycle
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // Core logo operations (supports both binary and PNG)
    String saveLogo(const char* processName, const uint8_t* data, size_t size, LogoStorage::FileType type = LogoStorage::FileType::PNG);
    String saveLogoFromFile(const char* processName, const char* sourceFilePath);  // Auto-detect type from extension
    String getLogoPath(const char* processName);
    bool deleteLogo(const char* processName);
    bool hasLogo(const char* processName);

    // LVGL integration
    bool loadLogoToImage(const char* processName, lv_obj_t* imgObj, bool useDefault = true);
    bool setDefaultLogo(lv_obj_t* imgObj);

    // Logo information and metadata
    LogoFileInfo getLogoInfo(const char* processName);
    size_t getLogoFileSize(const char* processName);
    LogoStorage::FileType getLogoType(const char* processName);

    // Logo verification and flagging
    bool flagAsIncorrect(const char* processName, bool incorrect = true);
    bool markAsVerified(const char* processName, bool verified = true);
    bool isVerified(const char* processName);
    bool isFlagged(const char* processName);

    // Logo management
    std::vector<String> listAvailableLogos();
    std::vector<String> listLogosByType(LogoStorage::FileType type);
    bool rebuildMappings();
    size_t getLogoCount();

    // System management
    String getSystemStatus();
    bool cleanupOrphanedFiles();
    size_t getTotalStorageUsed();

   private:
    LogoManager() = default;
    ~LogoManager() = default;
    LogoManager(const LogoManager&) = delete;
    LogoManager& operator=(const LogoManager&) = delete;

    // Internal state
    bool initialized = false;

    // Statistics
    size_t logosLoaded = 0;
    size_t logosSaved = 0;
    size_t logosDeleted = 0;

    // Helper methods
    bool ensureInitialized();
    void logOperation(const String& operation, const char* processName, bool success);
    String generateFileName(const char* processName, LogoStorage::FileType type);
};

}  // namespace Logo

#endif  // LOGO_MANAGER_H
