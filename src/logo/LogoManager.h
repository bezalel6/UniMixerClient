#ifndef LOGO_MANAGER_H
#define LOGO_MANAGER_H

#include <Arduino.h>
#include <functional>
#include "LogoBinaryStorage.h"

// Forward declaration for LVGL
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace Logo {

/**
 * Logo information structure
 */
struct LogoBinaryInfo {
    String processName;     // "chrome.exe"
    String binaryFileName;  // "chrome_v1.bin"
    String binaryPath;      // "S:/logos/binaries/chrome_v1.bin" (for LVGL)
    size_t fileSize;        // Size in bytes

    // Metadata flags
    bool verified;       // User verified as correct
    bool flagged;        // User flagged as incorrect
    uint64_t timestamp;  // When received/saved

    LogoBinaryInfo() : fileSize(0), verified(false), flagged(false), timestamp(0) {}
};

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

    // Primary logo operations
    String saveLogo(const char* processName, const uint8_t* binaryData, size_t size);  // Returns path on success, empty string on failure
    String getLogoPath(const char* processName);                                       // Returns "S:/logos/binaries/xxx.bin" for LVGL
    bool deleteLogo(const char* processName);
    bool hasLogo(const char* processName);

    // LVGL integration helpers
    bool loadLogoToImage(const char* processName, lv_obj_t* imgObj, bool useDefault = true);
    bool setDefaultLogo(lv_obj_t* imgObj);

    // Logo information and metadata
    LogoBinaryInfo getLogoInfo(const char* processName);
    size_t getLogoFileSize(const char* processName);

    // Flag and verification management
    bool flagAsIncorrect(const char* processName, bool incorrect = true);
    bool markAsVerified(const char* processName, bool verified = true);
    bool isVerified(const char* processName);
    bool isFlagged(const char* processName);

    // System operations
    std::vector<String> listAvailableLogos();
    bool rebuildMappings();
    size_t getLogoCount();
    String getSystemStatus();

    // Cleanup and maintenance
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
    String generateBinaryName(const char* processName);
};

}  // namespace Logo

#endif  // LOGO_MANAGER_H
