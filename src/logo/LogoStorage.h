#ifndef LOGO_STORAGE_H
#define LOGO_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>

namespace Logo {

/**
 * Handles organized logo file structure:
 * /logos/files/     - Logo files (binary .bin and PNG .png)
 * /logos/mappings/  - Process name to file mappings
 * /logos/metadata/  - Logo metadata (verified, flagged, etc.)
 */
class LogoStorage {
   public:
    enum class FileType {
        BINARY,  // LVGL binary format
        PNG      // PNG image format
    };

    static LogoStorage& getInstance();

    // Core file operations (/logos/files/)
    bool saveFile(const String& fileName, const uint8_t* data, size_t size);
    bool deleteFile(const String& fileName);
    bool fileExists(const String& fileName);
    size_t getFileSize(const String& fileName);
    String generateUniqueFileName(const String& baseName, FileType type);
    std::vector<String> listFiles();
    std::vector<String> listFilesByType(FileType type);

    // File type utilities
    FileType getFileType(const String& fileName);
    String getFileExtension(FileType type);
    bool isValidFileType(const String& fileName);

    // Mapping operations (/logos/mappings/)
    bool saveProcessMapping(const String& processName, const String& fileName);
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
    String getFilePath(const String& fileName);         // "S:/logos/files/file.bin" or "S:/logos/files/file.png"
    String getMappingPath(const String& processName);   // "/logos/mappings/process.json"
    String getMetadataPath(const String& processName);  // "/logos/metadata/process.json"

    // Directory management
    bool ensureDirectoryStructure();
    bool isReady();
    void cleanup();

    // Utility
    String sanitizeFileName(const String& input);

   private:
    LogoStorage() = default;
    ~LogoStorage() = default;
    LogoStorage(const LogoStorage&) = delete;
    LogoStorage& operator=(const LogoStorage&) = delete;

    // Directory constants
    static const char* LOGOS_ROOT;
    static const char* FILES_DIR;
    static const char* MAPPINGS_DIR;
    static const char* METADATA_DIR;

    // Helper methods
    bool ensureDirectory(const String& path);
    String readJsonFile(const String& filePath);
    bool writeJsonFile(const String& filePath, const String& content);
};

}  // namespace Logo

#endif  // LOGO_STORAGE_H
