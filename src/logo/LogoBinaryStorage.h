#ifndef LOGO_BINARY_STORAGE_H
#define LOGO_BINARY_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>

namespace Logo {

/**
 * Handles organized logo file structure:
 * /logos/binaries/  - LVGL binary files
 * /logos/mappings/  - Process name to binary mappings
 * /logos/metadata/  - Logo metadata (verified, flagged, etc.)
 */
class LogoBinaryStorage {
   public:
    static LogoBinaryStorage& getInstance();

    // Core binary file operations (/logos/binaries/)
    bool saveBinaryFile(const String& binaryFileName, const uint8_t* data, size_t size);
    bool deleteBinaryFile(const String& binaryFileName);
    bool binaryFileExists(const String& binaryFileName);
    size_t getBinaryFileSize(const String& binaryFileName);
    String generateUniqueBinaryName(const String& baseName);
    std::vector<String> listBinaryFiles();

    // Mapping operations (/logos/mappings/)
    bool saveProcessMapping(const String& processName, const String& binaryFileName);
    String getProcessMapping(const String& processName);
    bool deleteProcessMapping(const String& processName);
    bool hasProcessMapping(const String& processName);
    std::vector<String> listMappedProcesses();

    // Metadata operations (/logos/metadata/)
    bool saveMetadata(const String& processName, bool verified, bool flagged, uint64_t timestamp = 0);
    bool getMetadata(const String& processName, bool& verified, bool& flagged, uint64_t& timestamp);
    bool deleteMetadata(const String& processName);
    bool hasMetadata(const String& processName);

    // Path helpers
    String getBinaryPath(const String& binaryFileName);  // "S:/logos/binaries/file.bin"
    String getMappingPath(const String& processName);    // "/logos/mappings/process.json"
    String getMetadataPath(const String& processName);   // "/logos/metadata/process.json"

    // Directory management
    bool ensureDirectoryStructure();
    bool isReady();
    void cleanup();

    // Utility
    String sanitizeFileName(const String& input);

   private:
    LogoBinaryStorage() = default;
    ~LogoBinaryStorage() = default;
    LogoBinaryStorage(const LogoBinaryStorage&) = delete;
    LogoBinaryStorage& operator=(const LogoBinaryStorage&) = delete;

    // Directory constants
    static const char* LOGOS_ROOT;
    static const char* BINARIES_DIR;
    static const char* MAPPINGS_DIR;
    static const char* METADATA_DIR;

    // Helper methods
    bool ensureDirectory(const String& path);
    String readJsonFile(const String& filePath);
    bool writeJsonFile(const String& filePath, const String& content);
};

}  // namespace Logo

#endif  // LOGO_BINARY_STORAGE_H
