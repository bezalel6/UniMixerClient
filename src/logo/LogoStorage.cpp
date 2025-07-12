#include "LogoStorage.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/SDManager.h"
#include <esp_log.h>

static const char *TAG = "LogoStorage";

namespace Logo {

// Single directory - brutal simplicity
const char *LogoStorage::LOGOS_DIR = "/logos/files";

LogoStorage &LogoStorage::getInstance() {
  static LogoStorage instance;
  return instance;
}

// =============================================================================
// CORE OPERATIONS - PNG ONLY
// =============================================================================

bool LogoStorage::saveLogo(const String &processName, const uint8_t *pngData,
                           size_t size) {
  if (processName.isEmpty() || !pngData || size == 0) {
    ESP_LOGW(TAG, "Invalid parameters for saveLogo");
    return false;
  }

  if (!ensureDirectoryStructure()) {
    ESP_LOGE(TAG, "Failed to ensure directory structure");
    return false;
  }

  String fileName = getFileName(processName);
  String fullPath = String(LOGOS_DIR) + "/" + fileName;

  ESP_LOGI(TAG, "Saving logo: %s -> %s (%zu bytes)", processName.c_str(),
           fullPath.c_str(), size);

  File file = Hardware::SD::openFile(fullPath.c_str(), "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", fullPath.c_str());
    return false;
  }

  size_t bytesWritten = file.write(pngData, size);
  file.close();

  if (bytesWritten != size) {
    ESP_LOGE(TAG, "Failed to write complete data. Written: %zu, Expected: %zu",
             bytesWritten, size);
    Hardware::SD::deleteFile(fullPath.c_str());
    return false;
  }

  ESP_LOGI(TAG, "Successfully saved logo: %s (%zu bytes)", fullPath.c_str(),
           bytesWritten);
  return true;
}

bool LogoStorage::deleteLogo(const String &processName) {
  if (processName.isEmpty()) {
    return false;
  }

  String fileName = getFileName(processName);
  String fullPath = String(LOGOS_DIR) + "/" + fileName;

  auto result = Hardware::SD::deleteFile(fullPath.c_str());
  if (result.success) {
    ESP_LOGI(TAG, "Deleted logo: %s", fullPath.c_str());
  } else {
    ESP_LOGW(TAG, "Failed to delete logo: %s", fullPath.c_str());
  }

  return result.success;
}

bool LogoStorage::hasLogo(const String &processName) {
  if (processName.isEmpty()) {
    return false;
  }

  String fileName = getFileName(processName);
  String fullPath = String(LOGOS_DIR) + "/" + fileName;
  return Hardware::SD::fileExists(fullPath.c_str());
}

size_t LogoStorage::getLogoSize(const String &processName) {
  if (processName.isEmpty()) {
    return 0;
  }

  String fileName = getFileName(processName);
  String fullPath = String(LOGOS_DIR) + "/" + fileName;
  return Hardware::SD::getFileSize(fullPath.c_str());
}

// =============================================================================
// PATH HELPERS
// =============================================================================

String LogoStorage::getLogoPath(const String &processName) {
  if (processName.isEmpty()) {
    return "";
  }
  // LVGL filesystem format
  String fileName = getFileName(processName);
  return "S:/logos/files/" + fileName;
}

String LogoStorage::getFileSystemPath(const String &processName) {
  if (processName.isEmpty()) {
    return "";
  }
  // Regular filesystem format for SD operations
  String fileName = getFileName(processName);
  return String(LOGOS_DIR) + "/" + fileName;
}

// =============================================================================
// LIST OPERATIONS
// =============================================================================

std::vector<String> LogoStorage::listLogos() {
  std::vector<String> processes;

  if (!isReady()) {
    return processes;
  }

  // TODO: Implement when std::function template issues are resolved
  // For now, return empty list to avoid compilation issues
  ESP_LOGW(TAG,
           "listLogos() not implemented yet - std::function template issues");

  return processes;
}

// =============================================================================
// SYSTEM OPERATIONS
// =============================================================================

bool LogoStorage::isReady() {
  return Hardware::SD::isMounted() && Hardware::SD::directoryExists(LOGOS_DIR);
}

bool LogoStorage::ensureDirectoryStructure() {
  if (!Hardware::SD::isMounted()) {
    ESP_LOGW(TAG, "SD card not mounted");
    return false;
  }

  if (!ensureDirectory(LOGOS_DIR)) {
    ESP_LOGE(TAG, "Failed to create directory: %s", LOGOS_DIR);
    return false;
  }

  ESP_LOGD(TAG, "Directory structure verified: %s", LOGOS_DIR);
  return true;
}

void LogoStorage::cleanup() {
  ESP_LOGI(TAG,
           "Cleanup requested - could implement orphaned file removal here");
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String LogoStorage::sanitizeProcessName(const String &processName) {
  if (processName.isEmpty()) {
    return "unknown";
  }

  String sanitized = processName;

  // Replace problematic characters
  sanitized.replace("/", "_");
  sanitized.replace("\\", "_");
  sanitized.replace(":", "_");
  sanitized.replace("*", "_");
  sanitized.replace("?", "_");
  sanitized.replace("\"", "_");
  sanitized.replace("<", "_");
  sanitized.replace(">", "_");
  sanitized.replace("|", "_");
  sanitized.replace(" ", "_");

  // Convert to lowercase for consistency
  sanitized.toLowerCase();

  // Limit length
  if (sanitized.length() > 100) {
    sanitized = sanitized.substring(0, 100);
  }

  return sanitized;
}

// =============================================================================
// PRIVATE HELPER METHODS
// =============================================================================

bool LogoStorage::ensureDirectory(const String &path) {
  if (!Hardware::SD::directoryExists(path.c_str())) {
    ESP_LOGI(TAG, "Creating directory: %s", path.c_str());
    return Hardware::SD::createDirectory(path.c_str());
  }
  return true;
}

String LogoStorage::getFileName(const String &processName) {
  return sanitizeProcessName(processName) + ".png";
}

} // namespace Logo
