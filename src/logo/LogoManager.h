#ifndef LOGO_MANAGER_H
#define LOGO_MANAGER_H

#include <Arduino.h>
#include <functional>
#include "LogoIndex.h"
#include "LogoBinaryStorage.h"

// Forward declaration for LVGL
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace Logo {

/**
 * Main coordinator for LVGL logo management system
 * Provides high-level interface for saving, loading, and managing logo binary files
 */
class LogoManager {
   public:
    static LogoManager& getInstance();

    // Initialization and lifecycle
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // Primary logo operations
    bool saveLogo(const char* processName, const uint8_t* binaryData, size_t size);
    String getLogoPath(const char* processName);  // Returns "S:/logos/process_xxx.bin" for LVGL
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
    bool rebuildIndex();
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

    bool initialized = false;

    // Internal helpers
    bool ensureInitialized();
    void logOperation(const char* operation, const char* processName, bool success);

    // Statistics tracking
    unsigned long logosLoaded = 0;
    unsigned long logosSaved = 0;
    unsigned long logosDeleted = 0;
};

}  // namespace Logo

#endif  // LOGO_MANAGER_H
