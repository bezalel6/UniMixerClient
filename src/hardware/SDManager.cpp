#include "SDManager.h"
#include "../display/LVGLSDFilesystem.h"
#include "DeviceManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "SDManager";

namespace Hardware {
namespace SD {

// Private variables - cardInfo is now the single source of truth
static SDCardInfo cardInfo = {};
static SemaphoreHandle_t sdOperationMutex = nullptr;

// Private function declarations
static void updateCardInfo(void);
static const char *getCardTypeString(uint8_t cardType);
static bool initializeSPI(void);
static void deinitializeSPI(void);
static SDFileResult createFileResult(bool success, size_t bytes = 0,
                                     const char *error = nullptr);
static bool removeDirectoryRecursive(const char *path);
static bool removeFileOrDirectory(const char *path);

bool init(void) {
  ESP_LOGI(TAG, "Initializing SD manager for ESP32-8048S070C");

  if (cardInfo.isInitialized()) {
    ESP_LOGW(TAG, "SD manager already initialized");
    return cardInfo.status == SD_STATUS_MOUNTED;
  }

  // Create mutex for thread-safe SD operations
  if (!sdOperationMutex) {
    sdOperationMutex = xSemaphoreCreateMutex();
    if (!sdOperationMutex) {
      ESP_LOGE(TAG, "Failed to create SD operation mutex");
      return false;
    }
  }

  // Initialize cardInfo as single source of truth
  RESET_CARD_INFO(cardInfo);
  cardInfo.set(cardInfo.status, SD_STATUS_INITIALIZING);
  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  cardInfo.set(cardInfo.lastMountAttempt, 0UL);

  // Initialize SPI interface
  if (!initializeSPI()) {
    ESP_LOGE(TAG, "Failed to initialize SPI interface");
    cardInfo.set(cardInfo.status, SD_STATUS_ERROR);
    return false;
  }

  // Attempt to mount the SD card
  cardInfo.setStateFlag(SD_STATE_INITIALIZED);
  if (mount()) {
    ESP_LOGI(TAG, "SD manager initialized successfully with card mounted");
    return true;
  } else {
    ESP_LOGW(TAG, "SD manager initialized but no card mounted");
    return false;
  }
}
bool isInitialized(void) { return cardInfo.isInitialized(); }
void deinit(void) {
  ESP_LOGI(TAG, "Deinitializing SD manager");

  if (!cardInfo.isInitialized()) {
    return;
  }

  // Deinitialize LVGL filesystem first
  deinitLVGLFilesystem();

  // Unmount card if mounted
  if (cardInfo.status == SD_STATUS_MOUNTED) {
    unmount();
  }

  // Deinitialize SPI
  deinitializeSPI();

  // Reset cardInfo state
  RESET_CARD_INFO(cardInfo);
  cardInfo.set(cardInfo.status, SD_STATUS_NOT_INITIALIZED);

  // Clean up mutex
  if (sdOperationMutex) {
    vSemaphoreDelete(sdOperationMutex);
    sdOperationMutex = nullptr;
  }

  ESP_LOGI(TAG, "SD manager deinitialized");
}

void update(void) {
  if (!cardInfo.isInitialized()) {
    return;
  }

  unsigned long now = Hardware::Device::getMillis();

  // Update card info periodically if mounted
  if (cardInfo.status == SD_STATUS_MOUNTED) {
    static unsigned long lastCardInfoUpdate = 0;
    if (now - lastCardInfoUpdate > 30000) { // Update every 30 seconds
      updateCardInfo();
      lastCardInfoUpdate = now;
    }
    // Check if card is still present (basic check)
    if (!::SD.cardSize()) {
      ESP_LOGW(TAG, "SD card appears to have been removed");
      cardInfo.set(cardInfo.status, SD_STATUS_CARD_REMOVED);
    }
  }

  // Attempt remount if card was removed and some time has passed
  else if (cardInfo.status == SD_STATUS_CARD_REMOVED ||
           cardInfo.status == SD_STATUS_MOUNT_FAILED) {
    if (now - cardInfo.lastMountAttempt > 10000) { // Try every 10 seconds
      ESP_LOGI(TAG, "Attempting to remount SD card");
      mount();
    }
  }

  // Update LVGL filesystem state based on SD card state changes
  updateLVGLFilesystem();
}

bool mount(void) {
  ESP_LOGI(TAG, "Attempting to mount SD card");

  if (!cardInfo.isInitialized()) {
    ESP_LOGE(TAG, "Cannot mount: SD manager not initialized");
    return false;
  }

  cardInfo.set(cardInfo.lastMountAttempt, Hardware::Device::getMillis());
  cardInfo.set(cardInfo.status, SD_STATUS_INITIALIZING);

  // Attempt to begin SD card communication
  for (int attempt = 0; attempt < SD_RETRY_ATTEMPTS; attempt++) {
    ESP_LOGI(TAG, "Mount attempt %d/%d", attempt + 1, SD_RETRY_ATTEMPTS);

    if (::SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQUENCY)) {
      ESP_LOGI(TAG, "SD card mounted successfully");
      cardInfo.set(cardInfo.status, SD_STATUS_MOUNTED);
      cardInfo.setStateFlag(SD_STATE_MOUNTED);
      updateCardInfo();
      cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
      printCardInfo();

      // Create essential directory structure
      ensureDirectory("/logos");
      ensureDirectory("/logos/files");
      ensureDirectory("/logos/mappings");
      ensureDirectory("/logos/metadata");

      // Note: LVGL filesystem initialization is deferred until LVGL is ready
      // Call initLVGLFilesystem() manually after Display::init() if needed

      return true;
    }

    ESP_LOGW(TAG, "Mount attempt %d failed", attempt + 1);
    if (attempt < SD_RETRY_ATTEMPTS - 1) {
      Hardware::Device::delay(1000); // Wait between attempts
    }
  }

  ESP_LOGE(TAG, "Failed to mount SD card after %d attempts", SD_RETRY_ATTEMPTS);
  cardInfo.set(cardInfo.status, SD_STATUS_MOUNT_FAILED);
  return false;
}

void unmount(void) {
  ESP_LOGI(TAG, "Unmounting SD card");

  if (cardInfo.status == SD_STATUS_MOUNTED) {
    // Deinitialize LVGL filesystem before unmounting SD card
    deinitLVGLFilesystem();

    ::SD.end();
    RESET_CARD_INFO(cardInfo);
    cardInfo.set(cardInfo.status, SD_STATUS_NOT_INITIALIZED);
    // Keep the initialized flag since the manager itself is still initialized
    cardInfo.setStateFlag(SD_STATE_INITIALIZED);
    ESP_LOGI(TAG, "SD card unmounted");
  }
}

bool remount(void) {
  ESP_LOGI(TAG, "Remounting SD card");
  unmount();
  Hardware::Device::delay(500);
  return mount();
}

SDStatus getStatus(void) { return cardInfo.status; }

const char *getStatusString(void) {
  switch (cardInfo.status) {
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

bool isMounted(void) { return cardInfo.status == SD_STATUS_MOUNTED; }

bool isCardPresent(void) {
  if (!isMounted()) {
    return false;
  }

  // Simple check - try to get card size
  return ::SD.cardSize() > 0;
}

SDCardInfo getCardInfo(void) { return cardInfo; }

unsigned long getLastActivity(void) { return cardInfo.lastActivity; }

bool createDirectory(const char *path) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGW(TAG, "Failed to acquire SD mutex for directory creation");
    return false;
  }

  bool result = false;

  if (!isMounted() || !path) {
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  ESP_LOGI(TAG, "Creating directory: %s", path);

  result = ::SD.mkdir(path);
  if (result) {
    ESP_LOGI(TAG, "Directory created successfully: %s", path);
    cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  } else {
    ESP_LOGW(TAG, "Failed to create directory: %s", path);
  }

  xSemaphoreGive(sdOperationMutex);
  return result;
}

bool removeDirectory(const char *path) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGW(TAG, "Failed to acquire SD mutex for directory removal");
    return false;
  }

  bool result = false;

  if (!isMounted() || !path) {
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  ESP_LOGI(TAG, "Removing directory: %s", path);

  result = ::SD.rmdir(path);
  if (result) {
    ESP_LOGI(TAG, "Directory removed successfully: %s", path);
    cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  } else {
    ESP_LOGW(TAG, "Failed to remove directory: %s", path);
  }

  xSemaphoreGive(sdOperationMutex);
  return result;
}

bool directoryExists(const char *path) {
  // OPTIMIZED: Reduced timeout from 5000ms to 500ms to prevent system blocking
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGD(TAG,
             "Could not acquire SD mutex for directoryExists (non-critical)");
    return false;
  }

  bool result = false;

  if (!isMounted() || !path) {
    ESP_LOGD(TAG, "Invalid parameters - mounted: %s, path: %s",
             isMounted() ? "YES" : "NO", path ? "valid" : "NULL");
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  File dir;
  try {
    dir = ::SD.open(path);
  } catch (...) {
    ESP_LOGD(TAG, "Exception during SD.open() for path: %s", path);
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  if (!dir) {
    ESP_LOGV(TAG, "Directory does not exist: %s", path);
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  try {
    bool isDir = dir.isDirectory();
    result = isDir;
  } catch (...) {
    ESP_LOGD(TAG, "Exception during dir.isDirectory() for path: %s", path);
    result = false;
  }

  try {
    dir.close();
  } catch (...) {
    ESP_LOGD(TAG, "Exception during dir.close() for path: %s", path);
  }

  xSemaphoreGive(sdOperationMutex);
  return result;
}

bool listDirectory(
    const char *path,
    std::function<void(const char *name, bool isDir, size_t size)> callback) {
  // OPTIMIZED: Reduced timeout from 5000ms to 1000ms and improved error
  // handling
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGW(TAG,
             "Could not acquire SD mutex for directory listing within timeout");
    return false;
  }

  bool result = false;

  if (!isMounted()) {
    ESP_LOGD(TAG, "SD card not mounted");
    xSemaphoreGive(sdOperationMutex);
    return false;
  }
  if (!path) {
    ESP_LOGW(TAG, "Path is null");
    xSemaphoreGive(sdOperationMutex);
    return false;
  }
  if (!callback) {
    ESP_LOGW(TAG, "Callback is null");
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  ESP_LOGI(TAG, "Listing directory: %s", path);

  File root = ::SD.open(path);

  if (!root) {
    ESP_LOGW(TAG, "Could not open directory: %s", path);
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  if (!root.isDirectory()) {
    ESP_LOGW(TAG, "Path is not a directory: %s", path);
    root.close();
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  // OPTIMIZED: Reduced max files and added time limiting
  int fileCount = 0;
  const int MAX_FILES = 500;                 // Reduced from 1000
  const uint32_t MAX_PROCESSING_TIME = 2000; // 2 second max processing time
  uint32_t start_time = millis();

  File file;
  try {
    file = root.openNextFile();
  } catch (...) {
    ESP_LOGW(TAG, "Exception during root.openNextFile()");
    try {
      root.close();
    } catch (...) {
      ESP_LOGW(TAG, "Exception during root.close() in error handler");
    }
    result = true; // Empty directory is still successful
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  // Special handling for empty directories
  if (!file) {
    ESP_LOGD(TAG, "Directory is empty: %s", path);
    try {
      root.close();
    } catch (...) {
    }
    result = true;
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  while (file && fileCount < MAX_FILES &&
         (millis() - start_time) < MAX_PROCESSING_TIME) {
    // Reset watchdog timer every 5 files (reduced from 10)
    if (fileCount % 5 == 0) {
      esp_task_wdt_reset();
    }

    // OPTIMIZED: Increased memory threshold and added timeout check
    if (ESP.getFreeHeap() < 8192) { // Increased from 4096
      ESP_LOGW(TAG,
               "Low memory detected (%u bytes), stopping directory iteration",
               ESP.getFreeHeap());
      break;
    }

    // More robust file object validation
    bool fileValid = false;
    const char *fileName = nullptr;

    try {
      fileName = file.name();
      if (fileName && strlen(fileName) > 0 && strlen(fileName) < 256) {
        fileValid = true;
      } else {
        ESP_LOGD(TAG, "Filename is invalid - null or bad length");
      }
    } catch (...) {
      ESP_LOGD(TAG, "Exception while getting file name");
      fileValid = false;
    }

    if (fileValid) {
      try {
        bool isDir = file.isDirectory();
        size_t size = isDir ? 0 : file.size();

        // Extract just the filename without path
        const char *baseName = strrchr(fileName, '/');
        if (baseName) {
          baseName++; // Skip the '/'
        } else {
          baseName = fileName;
        }

        callback(baseName, isDir, size);
      } catch (...) {
        ESP_LOGD(TAG, "Exception while processing file: %s",
                 fileName ? fileName : "unknown");
      }
    }

    // Safely close current file before getting next one
    try {
      file.close();
    } catch (...) {
      ESP_LOGD(TAG, "Exception while closing file");
    }

    try {
      file = root.openNextFile();
    } catch (...) {
      ESP_LOGD(TAG, "Exception while getting next file, stopping iteration");
      break;
    }
    fileCount++;
  }

  // OPTIMIZED: Log timeout condition
  if ((millis() - start_time) >= MAX_PROCESSING_TIME) {
    ESP_LOGW(TAG, "Directory listing timed out after %ums, processed %d files",
             MAX_PROCESSING_TIME, fileCount);
  } else if (fileCount >= MAX_FILES) {
    ESP_LOGW(TAG, "Reached maximum file limit (%d), stopping iteration",
             MAX_FILES);
  }

  try {
    root.close();
  } catch (...) {
    ESP_LOGD(TAG, "Exception while closing root directory");
  }

  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  ESP_LOGI(TAG, "Listed %d items from directory: %s in %ums", fileCount, path,
           millis() - start_time);
  result = true;

  xSemaphoreGive(sdOperationMutex);
  return result;
}
bool ensureDirectory(const char *path) {
  if (!directoryExists(path)) {
    ESP_LOGI(TAG, "Creating directory: %s", path);
    return createDirectory(path);
  }
  return true;
}
SDFileResult readFile(const char *path, char *buffer, size_t maxLength) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return createFileResult(false, 0, "Failed to acquire SD mutex");
  }

  SDFileResult result;

  if (!isMounted() || !path || !buffer || maxLength == 0) {
    result = createFileResult(false, 0, "Invalid parameters");
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  ESP_LOGI(TAG, "Reading file: %s", path);

  File file = ::SD.open(path, FILE_READ);
  if (!file) {
    result = createFileResult(false, 0, "Failed to open file");
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  size_t bytesToRead = min((size_t)file.size(),
                           maxLength - 1); // Reserve space for null terminator
  size_t bytesRead = file.readBytes(buffer, bytesToRead);
  buffer[bytesRead] = '\0'; // Null terminate

  file.close();
  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());

  ESP_LOGI(TAG, "File read successfully: %zu bytes", bytesRead);
  result = createFileResult(true, bytesRead, nullptr);

  xSemaphoreGive(sdOperationMutex);
  return result;
}

SDFileResult writeFile(const char *path, const char *data, bool append) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return createFileResult(false, 0, "Failed to acquire SD mutex");
  }

  SDFileResult result;

  if (!isMounted() || !path || !data) {
    result = createFileResult(false, 0, "Invalid parameters");
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  ESP_LOGI(TAG, "Writing file: %s (append: %s)", path,
           append ? "true" : "false");

  File file = ::SD.open(path, append ? FILE_APPEND : FILE_WRITE);
  if (!file) {
    result = createFileResult(false, 0, "Failed to open file for writing");
    xSemaphoreGive(sdOperationMutex);
    return result;
  }

  size_t dataLength = strlen(data);
  size_t bytesWritten = file.write((const uint8_t *)data, dataLength);

  file.close();
  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());

  if (bytesWritten == dataLength) {
    ESP_LOGI(TAG, "File written successfully: %zu bytes", bytesWritten);
    result = createFileResult(true, bytesWritten, nullptr);
  } else {
    ESP_LOGW(TAG, "Incomplete write: %zu/%zu bytes", bytesWritten, dataLength);
    result = createFileResult(false, bytesWritten, "Incomplete write");
  }

  xSemaphoreGive(sdOperationMutex);
  return result;
}

SDFileResult deleteFile(const char *path) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return createFileResult(false, 0, "Failed to acquire SD mutex");
  }

  SDFileResult fileResult;

  if (!isMounted() || !path) {
    fileResult = createFileResult(false, 0, "Invalid parameters");
    xSemaphoreGive(sdOperationMutex);
    return fileResult;
  }

  ESP_LOGI(TAG, "Deleting file: %s", path);

  bool result = ::SD.remove(path);
  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());

  if (result) {
    ESP_LOGI(TAG, "File deleted successfully: %s", path);
    fileResult = createFileResult(true, 0, nullptr);
  } else {
    ESP_LOGW(TAG, "Failed to delete file: %s", path);
    fileResult = createFileResult(false, 0, "Delete failed");
  }

  xSemaphoreGive(sdOperationMutex);
  return fileResult;
}

bool fileExists(const char *path) {
  if (!isMounted() || !path) {
    return false;
  }

  return ::SD.exists(path);
}

size_t getFileSize(const char *path) {
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

bool renameFile(const char *oldPath, const char *newPath) {
  if (!isMounted() || !oldPath || !newPath) {
    return false;
  }

  ESP_LOGI(TAG, "Renaming file: %s -> %s", oldPath, newPath);

  bool result = ::SD.rename(oldPath, newPath);
  if (result) {
    ESP_LOGI(TAG, "File renamed successfully");
    cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  } else {
    ESP_LOGW(TAG, "Failed to rename file");
  }

  return result;
}

File openFile(const char *path, const char *mode) {
  if (!isMounted() || !path) {
    return File();
  }

  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  return ::SD.open(path, mode);
}

void closeFile(File &file) {
  if (file) {
    file.close();
    cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  }
}

bool copyFile(const char *sourcePath, const char *destPath) {
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
  cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());

  ESP_LOGI(TAG, "File copied successfully: %zu bytes", totalCopied);
  return true;
}

bool format(void) {
  if (!sdOperationMutex ||
      xSemaphoreTake(sdOperationMutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to acquire SD mutex for format operation");
    return false;
  }

  bool result = false;

  if (!isMounted()) {
    ESP_LOGW(TAG, "Cannot format: SD card not mounted");
    xSemaphoreGive(sdOperationMutex);
    return false;
  }

  ESP_LOGW(TAG,
           "Starting SD card format operation - this will erase all data!");

  // Store card info before formatting
  uint64_t cardSize = ::SD.cardSize();
  uint8_t cardType = ::SD.cardType();

  // Unmount the card first
  unmount();
  Hardware::Device::delay(1000); // Give time for unmount

  bool formatSuccess = false;
  int attempts = 0;
  const int MAX_ATTEMPTS = 3;

  while (!formatSuccess && attempts < MAX_ATTEMPTS) {
    attempts++;
    ESP_LOGI(TAG, "Format attempt %d/%d", attempts, MAX_ATTEMPTS);

    // Try to mount again
    if (mount()) {
      ESP_LOGI(TAG, "Card remounted, attempting to clear root directory");

      // Delete all files and directories in root using recursive approach
      formatSuccess = removeDirectoryRecursive("/");

      if (formatSuccess) {
        ESP_LOGI(TAG, "SD card formatted successfully (all content cleared)");
        // Update card info after format
        updateCardInfo();
        cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
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

  result = formatSuccess;
  xSemaphoreGive(sdOperationMutex);
  return result;
}

void printCardInfo(void) {
  if (!isMounted()) {
    ESP_LOGI(TAG, "SD Card: Not mounted (Status: %s)", getStatusString());
    return;
  }

  ESP_LOGI(TAG, "=== SD Card Information ===");
  ESP_LOGI(TAG, "Status: %s", getStatusString());
  ESP_LOGI(TAG, "Card Type: %s", getCardTypeString(cardInfo.cardType));
  ESP_LOGI(TAG, "Card Size: %.2f MB", cardInfo.cardSize / (1024.0 * 1024.0));
  ESP_LOGI(TAG, "Total Space: %.2f MB",
           cardInfo.totalBytes / (1024.0 * 1024.0));
  ESP_LOGI(TAG, "Used Space: %.2f MB", cardInfo.usedBytes / (1024.0 * 1024.0));
  ESP_LOGI(TAG, "Free Space: %.2f MB",
           (cardInfo.totalBytes - cardInfo.usedBytes) / (1024.0 * 1024.0));
  ESP_LOGI(TAG, "===========================");
}

void cleanup(void) {
  ESP_LOGI(TAG, "Performing SD card cleanup");

  if (isMounted()) {
    // Close any open files (this is automatically handled by the File
    // destructor)
    cardInfo.set(cardInfo.lastActivity, Hardware::Device::getMillis());
  }
}

// LVGL SD filesystem management
bool initLVGLFilesystem(void) {
  ESP_LOGI(TAG, "Initializing LVGL SD filesystem driver");

  if (cardInfo.isLVGLReady()) {
    ESP_LOGW(TAG, "LVGL filesystem already initialized");
    return true;
  }

  if (!isMounted()) {
    ESP_LOGW(TAG, "Cannot initialize LVGL filesystem: SD card not mounted");
    return false;
  }

  if (Display::LVGLSDFilesystem::init()) {
    cardInfo.setStateFlag(SD_STATE_LVGL_FILESYSTEM_READY);
    cardInfo.setStateFlag(SD_STATE_LAST_SD_MOUNTED);
    ESP_LOGI(TAG, "LVGL SD filesystem driver initialized successfully");
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to initialize LVGL SD filesystem driver");
    return false;
  }
}

void deinitLVGLFilesystem(void) {
  if (cardInfo.isLVGLReady()) {
    ESP_LOGI(TAG, "Deinitializing LVGL SD filesystem driver");
    Display::LVGLSDFilesystem::deinit();
    cardInfo.clearStateFlag(SD_STATE_LVGL_FILESYSTEM_READY);
    cardInfo.clearStateFlag(SD_STATE_LAST_SD_MOUNTED);
  }
}

bool isLVGLFilesystemReady(void) {
  return cardInfo.isLVGLReady() && isMounted();
}

void updateLVGLFilesystem(void) {
  bool currentSDMounted = isMounted();

  // Check if SD card state has changed
  if (currentSDMounted != cardInfo.wasLastSDMounted()) {
    ESP_LOGI(
        TAG,
        "SD card state changed: mounted=%s, LVGL filesystem initialized=%s",
        currentSDMounted ? "YES" : "NO", cardInfo.isLVGLReady() ? "YES" : "NO");

    if (currentSDMounted) {
      // SD card was mounted - initialize LVGL filesystem if not already done
      if (!cardInfo.isLVGLReady()) {
        // Check if LVGL is ready before trying to initialize filesystem
        lv_disp_t *disp = lv_disp_get_default();
        if (disp != NULL) {
          ESP_LOGI(
              TAG,
              "SD card mounted and LVGL ready, initializing LVGL filesystem");
          initLVGLFilesystem();
        } else {
          ESP_LOGD(TAG, "SD card mounted but LVGL not ready yet, deferring "
                        "LVGL filesystem initialization");
        }
      }
    } else {
      // SD card was unmounted - deinitialize LVGL filesystem
      if (cardInfo.isLVGLReady()) {
        ESP_LOGI(TAG, "SD card unmounted, deinitializing LVGL filesystem");
        deinitLVGLFilesystem();
      }
    }

    cardInfo.setStateFlag(SD_STATE_LAST_SD_MOUNTED, currentSDMounted);
  }
}

// Private function implementations
static void updateCardInfo(void) {
  if (!isMounted()) {
    // Preserve the current state flags when not mounted
    bool initComplete = cardInfo.isInitialized();
    bool lvglInit = cardInfo.isLVGLReady();
    bool lastSDState = cardInfo.wasLastSDMounted();
    unsigned long lastMount = cardInfo.lastMountAttempt;
    unsigned long lastAct = cardInfo.lastActivity;
    SDStatus currentStat = cardInfo.status;

    RESET_CARD_INFO(cardInfo);
    cardInfo.set(cardInfo.status, currentStat);
    cardInfo.setStateFlag(SD_STATE_INITIALIZED, initComplete);
    cardInfo.setStateFlag(SD_STATE_LVGL_FILESYSTEM_READY, lvglInit);
    cardInfo.setStateFlag(SD_STATE_LAST_SD_MOUNTED, lastSDState);
    cardInfo.set(cardInfo.lastMountAttempt, lastMount);
    cardInfo.set(cardInfo.lastActivity, lastAct);
    return;
  }

  cardInfo.set(cardInfo.cardType, ::SD.cardType());
  cardInfo.set(cardInfo.cardSize, ::SD.cardSize());
  cardInfo.set(cardInfo.totalBytes, ::SD.totalBytes());
  cardInfo.set(cardInfo.usedBytes, ::SD.usedBytes());
  cardInfo.setStateFlag(SD_STATE_MOUNTED);
}

static const char *getCardTypeString(uint8_t cardType) {
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
  const uint32_t SPI_FREQUENCY =
      4000000; // 1 MHz - very conservative for multi-peripheral board

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
      if (response != 0xFF)
        break;
    }

    SPI.endTransaction();
    digitalWrite(TF_CS, HIGH);

    // Check if we got a valid response
    if (response == 0x01 || response == 0x00) {
      ESP_LOGI(TAG, "SPI initialization successful - SD card detected");
      return true;
    } else {
      ESP_LOGW(TAG, "SPI initialized but unexpected SD response: 0x%02X",
               response);
      return true; // SPI is working, SD card might not be inserted
    }

  } catch (...) {
    ESP_LOGE(TAG, "SPI initialization failed - exception occurred");
    return false;
  }
}

static void deinitializeSPI(void) {
  // Deinitialize SPI
  SPI.end();
}

static SDFileResult createFileResult(bool success, size_t bytes,
                                     const char *error) {
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

static bool removeDirectoryRecursive(const char *path) {
  if (!path)
    return false;

  ESP_LOGI(TAG, "Recursively removing directory contents: %s", path);

  File root = ::SD.open(path);
  if (!root || !root.isDirectory()) {
    if (root)
      root.close();
    return false;
  }

  bool success = true;
  File file = root.openNextFile();

  while (file) {
    String fullPath = String(path);
    if (!fullPath.endsWith("/")) {
      fullPath += "/";
    }

    // Get the base filename without path
    const char *fileName = file.name();
    const char *baseName = strrchr(fileName, '/');
    if (baseName) {
      baseName++; // Skip the '/'
    } else {
      baseName = fileName;
    }

    fullPath += baseName;
    bool isDir = file.isDirectory();
    file.close();

    ESP_LOGI(TAG, "Processing: %s (isDirectory: %s)", fullPath.c_str(),
             isDir ? "true" : "false");

    if (isDir) {
      // Recursively remove subdirectory contents first
      if (!removeDirectoryRecursive(fullPath.c_str())) {
        ESP_LOGW(TAG, "Failed to recursively remove directory: %s",
                 fullPath.c_str());
        success = false;
      }
      // Then remove the empty directory
      if (!::SD.rmdir(fullPath.c_str())) {
        ESP_LOGW(TAG, "Failed to remove empty directory: %s", fullPath.c_str());
        success = false;
      } else {
        ESP_LOGI(TAG, "Successfully removed directory: %s", fullPath.c_str());
      }
    } else {
      // Remove file
      if (!::SD.remove(fullPath.c_str())) {
        ESP_LOGW(TAG, "Failed to remove file: %s", fullPath.c_str());
        success = false;
      } else {
        ESP_LOGI(TAG, "Successfully removed file: %s", fullPath.c_str());
      }
    }

    file = root.openNextFile();
  }

  root.close();

  // Don't remove the root directory itself if it's "/"
  if (strcmp(path, "/") != 0) {
    if (!::SD.rmdir(path)) {
      ESP_LOGW(TAG, "Failed to remove directory: %s", path);
      success = false;
    }
  }

  return success;
}

static bool removeFileOrDirectory(const char *path) {
  if (!path)
    return false;

  File item = ::SD.open(path);
  if (!item) {
    ESP_LOGW(TAG, "Cannot open item for removal: %s", path);
    return false;
  }

  bool isDir = item.isDirectory();
  item.close();

  if (isDir) {
    return removeDirectoryRecursive(path);
  } else {
    return ::SD.remove(path);
  }
}

} // namespace SD
} // namespace Hardware
