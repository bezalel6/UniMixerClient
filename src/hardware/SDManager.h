#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <functional>

namespace Hardware {
namespace SD {

// SD card configuration for ESP32-8048S070C (ESP32-S3)
#define SD_CS_PIN TF_CS
#define SD_MOSI_PIN TF_SPI_MOSI
#define SD_SCLK_PIN TF_SPI_SCLK
#define SD_MISO_PIN TF_SPI_MISO
#define SD_SPI_FREQUENCY 10000000  // Reduce to 10MHz for stability
#define SD_RETRY_ATTEMPTS 3
#define SD_MOUNT_TIMEOUT_MS 5000
// SD card status enum
typedef enum {
    SD_STATUS_NOT_INITIALIZED = 0,
    SD_STATUS_INITIALIZING,
    SD_STATUS_MOUNTED,
    SD_STATUS_MOUNT_FAILED,
    SD_STATUS_CARD_REMOVED,
    SD_STATUS_ERROR
} SDStatus;

// SD card information structure
typedef struct {
    uint8_t cardType;
    uint64_t cardSize;
    uint64_t totalBytes;
    uint64_t usedBytes;
    bool mounted;
    unsigned long lastActivity;
} SDCardInfo;

// File operation result structure
typedef struct {
    bool success;
    size_t bytesProcessed;
    char errorMessage[64];
} SDFileResult;

// SD manager initialization and control
bool init(void);
void deinit(void);
void update(void);

// Mount/unmount operations
bool mount(void);
void unmount(void);
bool remount(void);

// Status query functions
SDStatus getStatus(void);
const char* getStatusString(void);
bool isMounted(void);
bool isInitialized(void);
bool isCardPresent(void);
SDCardInfo getCardInfo(void);
unsigned long getLastActivity(void);

// Directory operations
bool createDirectory(const char* path);
bool removeDirectory(const char* path);
bool directoryExists(const char* path);
bool listDirectory(const char* path, std::function<void(const char* name, bool isDir, size_t size)> callback);

// File operations
SDFileResult readFile(const char* path, char* buffer, size_t maxLength);
SDFileResult writeFile(const char* path, const char* data, bool append = false);
SDFileResult deleteFile(const char* path);
bool fileExists(const char* path);
size_t getFileSize(const char* path);
bool renameFile(const char* oldPath, const char* newPath);

// Advanced file operations
File openFile(const char* path, const char* mode = "r");
void closeFile(File& file);
bool copyFile(const char* sourcePath, const char* destPath);

// Utility functions
bool format(void);
void printCardInfo(void);
void cleanup(void);

// File system constants
static const char* ROOT_PATH = "/";
static const size_t MAX_PATH_LENGTH = 256;
static const size_t MAX_FILENAME_LENGTH = 64;

}  // namespace SD
}  // namespace Hardware

#endif  // SD_MANAGER_H
