#ifndef LVGL_SD_FILESYSTEM_H
#define LVGL_SD_FILESYSTEM_H

#include <lvgl.h>

namespace Display {
namespace LVGLSDFilesystem {

/**
 * LVGL SD Card Filesystem Driver
 *
 * This driver registers a custom LVGL filesystem interface for the SD card
 * that bridges LVGL filesystem calls to the existing Hardware::SD::SDManager.
 *
 * The driver registers the "S:" drive letter so LVGL can access files using
 * paths like "S:/logos/binaries/file.bin"
 */

/**
 * Initialize and register the LVGL SD filesystem driver
 * Must be called after LVGL initialization and SD card mounting
 *
 * @return true if successful, false otherwise
 */
bool init(void);

/**
 * Deinitialize the LVGL SD filesystem driver
 */
void deinit(void);

/**
 * Check if the filesystem driver is initialized and ready
 *
 * @return true if ready, false otherwise
 */
bool isReady(void);

}  // namespace LVGLSDFilesystem
}  // namespace Display

#endif  // LVGL_SD_FILESYSTEM_H
