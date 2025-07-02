#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace Boot {

/**
 * Boot Mode Types
 */
enum class BootMode {
  NORMAL,     // Standard operation mode
  OTA_UPDATE, // OTA update mode
  FACTORY,    // Factory reset mode
  RECOVERY    // Recovery mode
};

/**
 * Boot Manager - Handles separate boot modes
 */
class BootManager {
public:
  /**
   * Initialize boot manager and determine current boot mode
   */
  static bool init();

  /**
   * Get current boot mode
   */
  static BootMode getCurrentMode();

  /**
   * Request boot into OTA mode (sets flag and restarts)
   */
  static void requestOTAMode();

  /**
   * Request boot into normal mode
   */
  static void requestNormalMode();

  /**
   * Check if OTA mode was requested
   */
  static bool isOTAModeRequested();

  /**
   * Clear boot mode request
   */
  static void clearBootRequest();

  /**
   * Get boot reason string for debugging
   */
  static const char *getBootReasonString();

private:
  static BootMode currentMode;
  static bool initialized;

  // NVS keys for boot mode persistence
  static const char *NVS_NAMESPACE;
  static const char *NVS_BOOT_MODE_KEY;
  static const char *NVS_OTA_REQUEST_KEY;

  /**
   * Read boot mode from NVS
   */
  static BootMode readBootModeFromNVS();

  /**
   * Write boot mode to NVS
   */
  static bool writeBootModeToNVS(BootMode mode);

  /**
   * Determine boot mode based on reset reason and NVS flags
   */
  static BootMode determineBootMode();
};

} // namespace Boot

#endif // BOOT_MANAGER_H
