#ifndef LOGO_BINARY_STORAGE_H
#define LOGO_BINARY_STORAGE_H

#include <Arduino.h>
#include <FS.h>

namespace Logo {

/**
 * Handles direct binary file operations for LVGL logo files
 * Manages saving/loading .bin files to/from SD card
 */
class LogoBinaryStorage {
   public:
    static LogoBinaryStorage& getInstance();

    // Core binary file operations
    bool saveBinaryFile(const char* filename, const uint8_t* data, size_t size);
    bool deleteBinaryFile(const char* filename);
    bool fileExists(const char* filename);
    size_t getFileSize(const char* filename);

    // Path and filename helpers
    String makeLogoPath(const char* processName);  // "S:/logos/process_xxx.bin"
    String makeFileName(const char* processName);  // "process_xxx.bin"
    String sanitizeProcessName(const char* processName);

    // Directory management
    bool ensureLogosDirectory();
    bool isLogosDirectoryReady();

   private:
    LogoBinaryStorage() = default;
    ~LogoBinaryStorage() = default;
    LogoBinaryStorage(const LogoBinaryStorage&) = delete;
    LogoBinaryStorage& operator=(const LogoBinaryStorage&) = delete;

    static const char* LOGOS_DIR;
    static const char* LOGOS_PREFIX;
    static const char* BIN_EXTENSION;
};

}  // namespace Logo

#endif  // LOGO_BINARY_STORAGE_H
