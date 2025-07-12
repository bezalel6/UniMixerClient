#ifndef LOGO_STORAGE_H
#define LOGO_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <vector>

namespace Logo {

/**
 * BRUTAL SIMPLIFICATION: Direct PNG storage by process name
 * /logos/files/chrome.png, /logos/files/firefox.png, etc.
 * No mappings, no metadata, no JSON, no complexity.
 */
class LogoStorage {
public:
  static LogoStorage &getInstance();

  // Core operations - PNG logos only
  bool saveLogo(const String &processName, const uint8_t *pngData, size_t size);
  bool deleteLogo(const String &processName);
  bool hasLogo(const String &processName);
  size_t getLogoSize(const String &processName);

  // Path helpers
  String getLogoPath(
      const String &processName); // "S:/logos/files/chrome.png" for LVGL
  String getFileSystemPath(
      const String &processName); // "/logos/files/chrome.png" for SD operations

  // List operations
  std::vector<String> listLogos(); // Returns process names, not filenames

  // System operations
  bool isReady();
  bool ensureDirectoryStructure();
  void cleanup();

  // Utility
  String sanitizeProcessName(const String &processName);

private:
  LogoStorage() = default;
  ~LogoStorage() = default;
  LogoStorage(const LogoStorage &) = delete;
  LogoStorage &operator=(const LogoStorage &) = delete;

  // Single directory - brutal simplicity
  static const char *LOGOS_DIR;

  // Helper methods
  bool ensureDirectory(const String &path);
  String getFileName(const String &processName); // "chrome.png"
};

} // namespace Logo

#endif // LOGO_STORAGE_H
