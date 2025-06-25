#ifndef LOGO_INDEX_H
#define LOGO_INDEX_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

namespace Logo {

/**
 * Logo entry information structure
 */
struct LogoBinaryInfo {
    String processName;  // "chrome.exe"
    String binFileName;  // "process_chrome.bin"
    String filePath;     // "S:/logos/process_chrome.bin" (for LVGL)
    size_t fileSize;     // Size in bytes

    // Simple tracking flags
    bool verified;       // User verified as correct
    bool flagged;        // User flagged as incorrect
    uint64_t timestamp;  // When received/saved

    LogoBinaryInfo() : fileSize(0), verified(false), flagged(false), timestamp(0) {}
};

/**
 * Manages JSON-based index mapping process names to logo binary files
 * Provides fast lookup and persistence of logo metadata
 */
class LogoIndex {
   public:
    static LogoIndex& getInstance();

    // Index management
    bool loadFromFile();
    bool saveToFile();
    bool rebuildFromFileSystem();

    // Entry operations
    bool addEntry(const String& processName, const String& binFileName, size_t fileSize = 0);
    bool removeEntry(const String& processName);
    bool hasEntry(const String& processName) const;

    // Lookup operations
    String findBinFile(const String& processName) const;
    String findFilePath(const String& processName) const;  // Returns LVGL format path
    LogoBinaryInfo getLogoInfo(const String& processName) const;

    // Flag management
    bool setVerified(const String& processName, bool verified);
    bool setFlagged(const String& processName, bool flagged);

    // Utility
    std::vector<String> listAllProcesses() const;
    size_t getEntryCount() const;
    void clearAll();

   private:
    LogoIndex() = default;
    ~LogoIndex() = default;
    LogoIndex(const LogoIndex&) = delete;
    LogoIndex& operator=(const LogoIndex&) = delete;

    // Internal storage
    std::map<String, LogoBinaryInfo> logoEntries;
    bool indexLoaded = false;

    // JSON operations
    bool parseJsonToIndex(const String& jsonString);
    String createJsonFromIndex() const;

    // File paths
    static const char* INDEX_FILE_PATH;
    static const int INDEX_VERSION;
};

}  // namespace Logo

#endif  // LOGO_INDEX_H
