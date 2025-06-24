#include "SDManager.h"
#include "DeviceManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "SDManager";

namespace Hardware {
namespace SD {

// Private variables
static SDStatus currentStatus = SD_STATUS_NOT_INITIALIZED;
static unsigned long lastActivity = 0;
static unsigned long lastMountAttempt = 0;
static bool initializationComplete = false;
static SDCardInfo cardInfo = {0};

// Private function declarations
static void updateCardInfo(void);
static const char* getCardTypeString(uint8_t cardType);
static bool initializeSPI(void);
static void deinitializeSPI(void);
static SDFileResult createFileResult(bool success, size_t bytes = 0, const char* error = nullptr);
bool init(void) {
    ESP_LOGI(TAG, "Initializing SD manager for ESP32-8048S070C");

    if (initializationComplete) {
        ESP_LOGW(TAG, "SD manager already initialized");
        return currentStatus == SD_STATUS_MOUNTED;
    }

    // Initialize variables
    currentStatus = SD_STATUS_INITIALIZING;
    lastActivity = Hardware::Device::getMillis();
    lastMountAttempt = 0;

    // Clear card info
    memset(&cardInfo, 0, sizeof(cardInfo));

    // Initialize SPI interface
    if (!initializeSPI()) {
        ESP_LOGE(TAG, "Failed to initialize SPI interface");
        currentStatus = SD_STATUS_ERROR;
        return false;
    }

    // Attempt to mount the SD card
    initializationComplete = true;
    if (mount()) {
        ESP_LOGI(TAG, "SD manager initialized successfully with card mounted");
        return true;
    } else {
        ESP_LOGW(TAG, "SD manager initialized but no card mounted");
        initializationComplete = true;
        return false;
    }
}
bool isInitialized(void) {
    return initializationComplete;
}
void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing SD manager");

    if (!initializationComplete) {
        return;
    }

    // Unmount card if mounted
    if (currentStatus == SD_STATUS_MOUNTED) {
        unmount();
    }

    // Deinitialize SPI
    deinitializeSPI();

    // Reset state
    currentStatus = SD_STATUS_NOT_INITIALIZED;
    initializationComplete = false;
    memset(&cardInfo, 0, sizeof(cardInfo));

    ESP_LOGI(TAG, "SD manager deinitialized");
}

void update(void) {
    if (!initializationComplete) {
        return;
    }

    unsigned long now = Hardware::Device::getMillis();

    // Update card info periodically if mounted
    if (currentStatus == SD_STATUS_MOUNTED) {
        static unsigned long lastCardInfoUpdate = 0;
        if (now - lastCardInfoUpdate > 30000) {  // Update every 30 seconds
            updateCardInfo();
            lastCardInfoUpdate = now;
        }

        // Check if card is still present (basic check)
        if (!::SD.cardSize()) {
            ESP_LOGW(TAG, "SD card appears to have been removed");
            currentStatus = SD_STATUS_CARD_REMOVED;
        }
    }

    // Attempt remount if card was removed and some time has passed
    else if (currentStatus == SD_STATUS_CARD_REMOVED ||
             currentStatus == SD_STATUS_MOUNT_FAILED) {
        if (now - lastMountAttempt > 10000) {  // Try every 10 seconds
            ESP_LOGI(TAG, "Attempting to remount SD card");
            mount();
        }
    }
}

bool mount(void) {
    ESP_LOGI(TAG, "Attempting to mount SD card");

    if (!initializationComplete) {
        ESP_LOGE(TAG, "Cannot mount: SD manager not initialized");
        return false;
    }

    lastMountAttempt = Hardware::Device::getMillis();
    currentStatus = SD_STATUS_INITIALIZING;

    // Attempt to begin SD card communication
    for (int attempt = 0; attempt < SD_RETRY_ATTEMPTS; attempt++) {
        ESP_LOGI(TAG, "Mount attempt %d/%d", attempt + 1, SD_RETRY_ATTEMPTS);

        if (::SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQUENCY)) {
            ESP_LOGI(TAG, "SD card mounted successfully");
            currentStatus = SD_STATUS_MOUNTED;
            updateCardInfo();
            lastActivity = Hardware::Device::getMillis();
            printCardInfo();
            return true;
        }

        ESP_LOGW(TAG, "Mount attempt %d failed", attempt + 1);
        if (attempt < SD_RETRY_ATTEMPTS - 1) {
            Hardware::Device::delay(1000);  // Wait between attempts
        }
    }

    ESP_LOGE(TAG, "Failed to mount SD card after %d attempts", SD_RETRY_ATTEMPTS);
    currentStatus = SD_STATUS_MOUNT_FAILED;
    return false;
}

void unmount(void) {
    ESP_LOGI(TAG, "Unmounting SD card");

    if (currentStatus == SD_STATUS_MOUNTED) {
        ::SD.end();
        currentStatus = SD_STATUS_NOT_INITIALIZED;
        memset(&cardInfo, 0, sizeof(cardInfo));
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

bool remount(void) {
    ESP_LOGI(TAG, "Remounting SD card");
    unmount();
    Hardware::Device::delay(500);
    return mount();
}

SDStatus getStatus(void) {
    return currentStatus;
}

const char* getStatusString(void) {
    switch (currentStatus) {
        case SD_STATUS_NOT_INITIALIZED:
            return "Not Initialized";
        case SD_STATUS_INITIALIZING:
            return "Initializing...";
        case SD_STATUS_MOUNTED:
            return "Mounted";
        case SD_STATUS_MOUNT_FAILED:
            return "Mount Failed";
        case SD_STATUS_CARD_REMOVED:
            return "Card Removed";
        case SD_STATUS_ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

bool isMounted(void) {
    return currentStatus == SD_STATUS_MOUNTED;
}

bool isCardPresent(void) {
    if (!isMounted()) {
        return false;
    }

    // Simple check - try to get card size
    return ::SD.cardSize() > 0;
}

SDCardInfo getCardInfo(void) {
    return cardInfo;
}

unsigned long getLastActivity(void) {
    return lastActivity;
}

bool createDirectory(const char* path) {
    if (!isMounted() || !path) {
        return false;
    }

    ESP_LOGI(TAG, "Creating directory: %s", path);

    bool result = ::SD.mkdir(path);
    if (result) {
        ESP_LOGI(TAG, "Directory created successfully: %s", path);
        lastActivity = Hardware::Device::getMillis();
    } else {
        ESP_LOGW(TAG, "Failed to create directory: %s", path);
    }

    return result;
}

bool removeDirectory(const char* path) {
    if (!isMounted() || !path) {
        return false;
    }

    ESP_LOGI(TAG, "Removing directory: %s", path);

    bool result = ::SD.rmdir(path);
    if (result) {
        ESP_LOGI(TAG, "Directory removed successfully: %s", path);
        lastActivity = Hardware::Device::getMillis();
    } else {
        ESP_LOGW(TAG, "Failed to remove directory: %s", path);
    }

    return result;
}

bool directoryExists(const char* path) {
    if (!isMounted() || !path) {
        return false;
    }

    File dir = ::SD.open(path);
    if (!dir) {
        return false;
    }

    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
}

bool listDirectory(const char* path, void (*callback)(const char* name, bool isDir, size_t size)) {
    if (!isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }
    if (!path) {
        ESP_LOGW(TAG, "Path is null");
        return false;
    }
    if (!callback) {
        ESP_LOGW(TAG, "Callback is null");
        return false;
    }

    ESP_LOGI(TAG, "Listing directory: %s", path);

    File root = ::SD.open(path);
    if (!root) {
        ESP_LOGW(TAG, "Failed to open directory: %s", path);
        return false;
    }

    if (!root.isDirectory()) {
        ESP_LOGW(TAG, "Path is not a directory: %s", path);
        root.close();
        return false;
    }

    // Safer file iteration with explicit validation
    int fileCount = 0;
    const int MAX_FILES = 1000;  // Prevent infinite loops

    File file = root.openNextFile();
    while (file && fileCount < MAX_FILES) {
        // Reset watchdog timer every 10 files to prevent timeout
        if (fileCount % 10 == 0) {
            esp_task_wdt_reset();
        }

        // Validate file object before using it
        if (file.available() >= 0) {  // This checks if the file object is valid
            const char* fileName = file.name();
            if (fileName && strlen(fileName) > 0 && strlen(fileName) < 256) {
                // Additional safety check - ensure we can call the methods safely
                try {
                    bool isDir = file.isDirectory();
                    size_t size = isDir ? 0 : file.size();
                    callback(fileName, isDir, size);
                } catch (...) {
                    ESP_LOGW(TAG, "Exception while processing file: %s", fileName ? fileName : "unknown");
                }
            } else {
                ESP_LOGW(TAG, "Skipping file with invalid name");
            }
        } else {
            ESP_LOGW(TAG, "Invalid file object encountered, stopping iteration");
            break;
        }

        // Explicitly close current file before getting next one
        file.close();
        file = root.openNextFile();
        fileCount++;
    }

    if (fileCount >= MAX_FILES) {
        ESP_LOGW(TAG, "Reached maximum file limit (%d), stopping iteration", MAX_FILES);
    }

    root.close();
    lastActivity = Hardware::Device::getMillis();
    ESP_LOGI(TAG, "Listed %d items from directory: %s", fileCount, path);
    return true;
}

SDFileResult readFile(const char* path, char* buffer, size_t maxLength) {
    if (!isMounted() || !path || !buffer || maxLength == 0) {
        return createFileResult(false, 0, "Invalid parameters");
    }

    ESP_LOGI(TAG, "Reading file: %s", path);

    File file = ::SD.open(path, FILE_READ);
    if (!file) {
        return createFileResult(false, 0, "Failed to open file");
    }

    size_t bytesToRead = min((size_t)file.size(), maxLength - 1);  // Reserve space for null terminator
    size_t bytesRead = file.readBytes(buffer, bytesToRead);
    buffer[bytesRead] = '\0';  // Null terminate

    file.close();
    lastActivity = Hardware::Device::getMillis();

    ESP_LOGI(TAG, "File read successfully: %zu bytes", bytesRead);
    return createFileResult(true, bytesRead, nullptr);
}

SDFileResult writeFile(const char* path, const char* data, bool append) {
    if (!isMounted() || !path || !data) {
        return createFileResult(false, 0, "Invalid parameters");
    }

    ESP_LOGI(TAG, "Writing file: %s (append: %s)", path, append ? "true" : "false");

    File file = ::SD.open(path, append ? FILE_APPEND : FILE_WRITE);
    if (!file) {
        return createFileResult(false, 0, "Failed to open file for writing");
    }

    size_t dataLength = strlen(data);
    size_t bytesWritten = file.write((const uint8_t*)data, dataLength);

    file.close();
    lastActivity = Hardware::Device::getMillis();

    if (bytesWritten == dataLength) {
        ESP_LOGI(TAG, "File written successfully: %zu bytes", bytesWritten);
        return createFileResult(true, bytesWritten, nullptr);
    } else {
        ESP_LOGW(TAG, "Incomplete write: %zu/%zu bytes", bytesWritten, dataLength);
        return createFileResult(false, bytesWritten, "Incomplete write");
    }
}

SDFileResult deleteFile(const char* path) {
    if (!isMounted() || !path) {
        return createFileResult(false, 0, "Invalid parameters");
    }

    ESP_LOGI(TAG, "Deleting file: %s", path);

    bool result = ::SD.remove(path);
    lastActivity = Hardware::Device::getMillis();

    if (result) {
        ESP_LOGI(TAG, "File deleted successfully: %s", path);
        return createFileResult(true, 0, nullptr);
    } else {
        ESP_LOGW(TAG, "Failed to delete file: %s", path);
        return createFileResult(false, 0, "Delete failed");
    }
}

bool fileExists(const char* path) {
    if (!isMounted() || !path) {
        return false;
    }

    return ::SD.exists(path);
}

size_t getFileSize(const char* path) {
    if (!isMounted() || !path) {
        return 0;
    }

    File file = ::SD.open(path, FILE_READ);
    if (!file) {
        return 0;
    }

    size_t size = file.size();
    file.close();
    return size;
}

bool renameFile(const char* oldPath, const char* newPath) {
    if (!isMounted() || !oldPath || !newPath) {
        return false;
    }

    ESP_LOGI(TAG, "Renaming file: %s -> %s", oldPath, newPath);

    bool result = ::SD.rename(oldPath, newPath);
    if (result) {
        ESP_LOGI(TAG, "File renamed successfully");
        lastActivity = Hardware::Device::getMillis();
    } else {
        ESP_LOGW(TAG, "Failed to rename file");
    }

    return result;
}

File openFile(const char* path, const char* mode) {
    if (!isMounted() || !path) {
        return File();
    }

    lastActivity = Hardware::Device::getMillis();
    return ::SD.open(path, mode);
}

void closeFile(File& file) {
    if (file) {
        file.close();
        lastActivity = Hardware::Device::getMillis();
    }
}

bool copyFile(const char* sourcePath, const char* destPath) {
    if (!isMounted() || !sourcePath || !destPath) {
        return false;
    }

    ESP_LOGI(TAG, "Copying file: %s -> %s", sourcePath, destPath);

    File sourceFile = ::SD.open(sourcePath, FILE_READ);
    if (!sourceFile) {
        ESP_LOGW(TAG, "Failed to open source file: %s", sourcePath);
        return false;
    }

    File destFile = ::SD.open(destPath, FILE_WRITE);
    if (!destFile) {
        ESP_LOGW(TAG, "Failed to open destination file: %s", destPath);
        sourceFile.close();
        return false;
    }

    const size_t bufferSize = 512;
    uint8_t buffer[bufferSize];
    size_t totalCopied = 0;

    while (sourceFile.available()) {
        size_t bytesRead = sourceFile.read(buffer, bufferSize);
        size_t bytesWritten = destFile.write(buffer, bytesRead);

        if (bytesWritten != bytesRead) {
            ESP_LOGW(TAG, "Copy failed at offset %zu", totalCopied);
            sourceFile.close();
            destFile.close();
            return false;
        }

        totalCopied += bytesWritten;
    }

    sourceFile.close();
    destFile.close();
    lastActivity = Hardware::Device::getMillis();

    ESP_LOGI(TAG, "File copied successfully: %zu bytes", totalCopied);
    return true;
}

bool format(void) {
    if (!isMounted()) {
        ESP_LOGW(TAG, "Cannot format: SD card not mounted");
        return false;
    }

    ESP_LOGW(TAG, "Starting SD card format operation - this will erase all data!");

    // Store card info before formatting
    uint64_t cardSize = ::SD.cardSize();
    uint8_t cardType = ::SD.cardType();

    // Unmount the card first
    unmount();
    Hardware::Device::delay(1000);  // Give time for unmount

    // Try to remount in raw mode for formatting
    // Note: ESP32 SD library doesn't provide direct format support
    // This is a workaround that recreates the file system

    bool formatSuccess = false;
    int attempts = 0;
    const int MAX_ATTEMPTS = 3;

    while (!formatSuccess && attempts < MAX_ATTEMPTS) {
        attempts++;
        ESP_LOGI(TAG, "Format attempt %d/%d", attempts, MAX_ATTEMPTS);

        // Try to mount again
        if (mount()) {
            ESP_LOGI(TAG, "Card remounted, attempting to clear root directory");

            // Delete all files and directories in root
            bool clearSuccess = true;
            File root = ::SD.open("/");
            if (root && root.isDirectory()) {
                File file = root.openNextFile();
                while (file) {
                    String fileName = file.name();
                    bool isDir = file.isDirectory();
                    file.close();

                    if (isDir) {
                        if (!removeDirectory(fileName.c_str())) {
                            ESP_LOGW(TAG, "Failed to remove directory: %s", fileName.c_str());
                            clearSuccess = false;
                        }
                    } else {
                        if (!::SD.remove(fileName.c_str())) {
                            ESP_LOGW(TAG, "Failed to remove file: %s", fileName.c_str());
                            clearSuccess = false;
                        }
                    }

                    file = root.openNextFile();
                }
                root.close();
            }

            if (clearSuccess) {
                ESP_LOGI(TAG, "SD card formatted successfully (all content cleared)");
                formatSuccess = true;
                // Update card info after format
                updateCardInfo();
                lastActivity = Hardware::Device::getMillis();
            } else {
                ESP_LOGW(TAG, "Failed to clear all content during format");
                unmount();
                Hardware::Device::delay(1000);
            }
        } else {
            ESP_LOGW(TAG, "Failed to remount card for format attempt %d", attempts);
            Hardware::Device::delay(2000);
        }
    }

    if (!formatSuccess) {
        ESP_LOGE(TAG, "SD card format failed after %d attempts", MAX_ATTEMPTS);
        // Try to remount normally
        mount();
    }

    return formatSuccess;
}

void printCardInfo(void) {
    if (!isMounted()) {
        ESP_LOGI(TAG, "SD Card: Not mounted");
        return;
    }

    ESP_LOGI(TAG, "=== SD Card Information ===");
    ESP_LOGI(TAG, "Card Type: %s", getCardTypeString(cardInfo.cardType));
    ESP_LOGI(TAG, "Card Size: %.2f MB", cardInfo.cardSize / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "Total Space: %.2f MB", cardInfo.totalBytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "Used Space: %.2f MB", cardInfo.usedBytes / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "Free Space: %.2f MB", (cardInfo.totalBytes - cardInfo.usedBytes) / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "===========================");
}

void cleanup(void) {
    ESP_LOGI(TAG, "Performing SD card cleanup");

    if (isMounted()) {
        // Close any open files (this is automatically handled by the File destructor)
        lastActivity = Hardware::Device::getMillis();
    }
}

// Private function implementations
static void updateCardInfo(void) {
    if (!isMounted()) {
        memset(&cardInfo, 0, sizeof(cardInfo));
        return;
    }

    cardInfo.cardType = ::SD.cardType();
    cardInfo.cardSize = ::SD.cardSize();
    cardInfo.totalBytes = ::SD.totalBytes();
    cardInfo.usedBytes = ::SD.usedBytes();
    cardInfo.mounted = true;
    cardInfo.lastActivity = lastActivity;
}

static const char* getCardTypeString(uint8_t cardType) {
    switch (cardType) {
        case CARD_NONE:
            return "None";
        case CARD_MMC:
            return "MMC";
        case CARD_SD:
            return "SDSC";
        case CARD_SDHC:
            return "SDHC";
        default:
            return "Unknown";
    }
}

/**
 * Initialize SPI interface for SD card communication
 * Based on ESP32-8048S070C board configuration
 *
 * Pin assignments:
 * - CS (Chip Select): GPIO 10
 * - MOSI (Master Out Slave In): GPIO 11
 * - SCLK (Serial Clock): GPIO 12
 * - MISO (Master In Slave Out): GPIO 13
 *
 * @return true if SPI initialization successful, false otherwise
 */
static bool initializeSPI() {
    // Define pin assignments from board configuration

    // SPI frequency for SD card (typically 1-25 MHz)
    // Start conservatively due to potential display interference
    const uint32_t SPI_FREQUENCY = 4000000;  // 1 MHz - very conservative for multi-peripheral board

    try {
        // Initialize SPI with custom pins
        SPI.begin(TF_SPI_SCLK, TF_SPI_MISO, TF_SPI_MOSI, TF_CS);

        // Configure CS pin as output and set high (inactive)
        pinMode(TF_CS, OUTPUT);
        digitalWrite(TF_CS, HIGH);

        // Test SPI communication by attempting to read SD card
        // This is a basic connectivity test
        digitalWrite(TF_CS, LOW);

        // Send CMD0 (GO_IDLE_STATE) to test SPI communication
        SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));

        // CMD0: 0x40, 0x00, 0x00, 0x00, 0x00, 0x95
        uint8_t cmd0[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
        for (int i = 0; i < 6; i++) {
            SPI.transfer(cmd0[i]);
        }

        // Read response (should be 0x01 for idle state)
        uint8_t response = 0xFF;
        for (int i = 0; i < 8; i++) {
            response = SPI.transfer(0xFF);
            if (response != 0xFF) break;
        }

        SPI.endTransaction();
        digitalWrite(TF_CS, HIGH);

        // Check if we got a valid response
        if (response == 0x01 || response == 0x00) {
            Serial.println("SPI initialization successful - SD card detected");
            return true;
        } else {
            Serial.printf("SPI initialized but unexpected SD response: 0x%02X\n", response);
            return true;  // SPI is working, SD card might not be inserted
        }

    } catch (...) {
        Serial.println("SPI initialization failed - exception occurred");
        return false;
    }
}

static void deinitializeSPI(void) {
    // Deinitialize SPI
    SPI.end();
}

static SDFileResult createFileResult(bool success, size_t bytes, const char* error) {
    SDFileResult result;
    result.success = success;
    result.bytesProcessed = bytes;

    if (error) {
        strncpy(result.errorMessage, error, sizeof(result.errorMessage) - 1);
        result.errorMessage[sizeof(result.errorMessage) - 1] = '\0';
    } else {
        result.errorMessage[0] = '\0';
    }

    return result;
}

}  // namespace SD
}  // namespace Hardware
