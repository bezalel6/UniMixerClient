#include "LVGLSDFilesystem.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>
#include <SD.h>

static const char* TAG = "LVGLSDFilesystem";

namespace Display {
namespace LVGLSDFilesystem {

// Driver state
static bool initialized = false;
static lv_fs_drv_t sd_drv;

// Convert LVGL path (S:/path) to SD manager path (/path)
static String convertLvglPath(const char* lvgl_path) {
    if (!lvgl_path) {
        return "";
    }

    // Remove drive letter and colon if present (S: -> "")
    String path = String(lvgl_path);
    if (path.length() >= 2 && path.charAt(1) == ':') {
        path = path.substring(2);  // Remove "S:"
    }

    // Ensure path starts with /
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    return path;
}

// LVGL filesystem driver functions
static void* sd_open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    ESP_LOGD(TAG, "Opening file: %s (mode: %d)", path, mode);

    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return nullptr;
    }

    String sdPath = convertLvglPath(path);
    ESP_LOGD(TAG, "Converted path: %s", sdPath.c_str());

    // Determine file mode
    const char* fileMode = FILE_READ;
    if (mode & LV_FS_MODE_WR) {
        if (mode & LV_FS_MODE_RD) {
            fileMode = FILE_WRITE;  // Read/Write
        } else {
            fileMode = FILE_WRITE;  // Write only
        }
    }

    // Open file using SD manager
    File* file = new File();
    *file = Hardware::SD::openFile(sdPath.c_str(), fileMode);

    if (!*file) {
        ESP_LOGW(TAG, "Failed to open file: %s", sdPath.c_str());
        delete file;
        return nullptr;
    }

    ESP_LOGD(TAG, "File opened successfully: %s", sdPath.c_str());
    return file;
}

static lv_fs_res_t sd_close_cb(lv_fs_drv_t* drv, void* file_p) {
    if (!file_p) {
        return LV_FS_RES_INV_PARAM;
    }

    File* file = static_cast<File*>(file_p);
    Hardware::SD::closeFile(*file);
    delete file;

    ESP_LOGD(TAG, "File closed");
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    if (!file_p || !buf || !br) {
        return LV_FS_RES_INV_PARAM;
    }

    File* file = static_cast<File*>(file_p);
    if (!*file) {
        return LV_FS_RES_NOT_EX;
    }

    *br = file->read(static_cast<uint8_t*>(buf), btr);

    ESP_LOGD(TAG, "Read %lu bytes (requested: %lu)", *br, btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_write_cb(lv_fs_drv_t* drv, void* file_p, const void* buf, uint32_t btw, uint32_t* bw) {
    if (!file_p || !buf || !bw) {
        return LV_FS_RES_INV_PARAM;
    }

    File* file = static_cast<File*>(file_p);
    if (!*file) {
        return LV_FS_RES_NOT_EX;
    }

    *bw = file->write(static_cast<const uint8_t*>(buf), btw);

    ESP_LOGD(TAG, "Wrote %lu bytes (requested: %lu)", *bw, btw);
    return (*bw == btw) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

static lv_fs_res_t sd_seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    if (!file_p) {
        return LV_FS_RES_INV_PARAM;
    }

    File* file = static_cast<File*>(file_p);
    if (!*file) {
        return LV_FS_RES_NOT_EX;
    }

    bool success = false;
    switch (whence) {
        case LV_FS_SEEK_SET:
            success = file->seek(pos);
            break;
        case LV_FS_SEEK_CUR:
            success = file->seek(file->position() + pos);
            break;
        case LV_FS_SEEK_END:
            success = file->seek(file->size() + pos);
            break;
    }

    ESP_LOGD(TAG, "Seek to position %lu (whence: %d) - %s", pos, whence, success ? "success" : "failed");
    return success ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

static lv_fs_res_t sd_tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    if (!file_p || !pos_p) {
        return LV_FS_RES_INV_PARAM;
    }

    File* file = static_cast<File*>(file_p);
    if (!*file) {
        return LV_FS_RES_NOT_EX;
    }

    *pos_p = file->position();

    ESP_LOGD(TAG, "Tell position: %lu", *pos_p);
    return LV_FS_RES_OK;
}

// Directory operations
static void* sd_dir_open_cb(lv_fs_drv_t* drv, const char* path) {
    ESP_LOGD(TAG, "Opening directory: %s", path);

    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return nullptr;
    }

    String sdPath = convertLvglPath(path);
    ESP_LOGD(TAG, "Converted directory path: %s", sdPath.c_str());

    if (!Hardware::SD::directoryExists(sdPath.c_str())) {
        ESP_LOGW(TAG, "Directory does not exist: %s", sdPath.c_str());
        return nullptr;
    }

    File* dir = new File();
    *dir = Hardware::SD::openFile(sdPath.c_str(), FILE_READ);

    if (!*dir || !dir->isDirectory()) {
        ESP_LOGW(TAG, "Failed to open directory: %s", sdPath.c_str());
        if (*dir) {
            dir->close();
        }
        delete dir;
        return nullptr;
    }

    ESP_LOGD(TAG, "Directory opened successfully: %s", sdPath.c_str());
    return dir;
}

static lv_fs_res_t sd_dir_read_cb(lv_fs_drv_t* drv, void* rddir_p, char* fn, uint32_t fn_len) {
    if (!rddir_p || !fn || fn_len == 0) {
        return LV_FS_RES_INV_PARAM;
    }

    File* dir = static_cast<File*>(rddir_p);
    if (!*dir) {
        return LV_FS_RES_NOT_EX;
    }

    File entry = dir->openNextFile();
    if (!entry) {
        // End of directory
        fn[0] = '\0';
        ESP_LOGD(TAG, "End of directory reached");
        return LV_FS_RES_OK;
    }

    const char* entryName = entry.name();
    if (!entryName) {
        fn[0] = '\0';
        entry.close();
        return LV_FS_RES_OK;
    }

    // Extract just the filename without full path
    const char* baseName = strrchr(entryName, '/');
    if (baseName) {
        baseName++;  // Skip the '/'
    } else {
        baseName = entryName;
    }

    // Copy filename, ensuring we don't overflow
    size_t nameLen = strlen(baseName);
    if (nameLen >= fn_len) {
        nameLen = fn_len - 1;
    }
    strncpy(fn, baseName, nameLen);
    fn[nameLen] = '\0';

    entry.close();

    ESP_LOGD(TAG, "Directory entry: %s", fn);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_dir_close_cb(lv_fs_drv_t* drv, void* rddir_p) {
    if (!rddir_p) {
        return LV_FS_RES_INV_PARAM;
    }

    File* dir = static_cast<File*>(rddir_p);
    dir->close();
    delete dir;

    ESP_LOGD(TAG, "Directory closed");
    return LV_FS_RES_OK;
}

// Public interface implementation
bool init(void) {
    ESP_LOGI(TAG, "Initializing LVGL SD filesystem driver");

    if (initialized) {
        ESP_LOGW(TAG, "LVGL SD filesystem driver already initialized");
        return true;
    }

    // Check if SD card is mounted
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot initialize filesystem driver");
        return false;
    }

    // Initialize the driver structure
    lv_fs_drv_init(&sd_drv);

    // Set up the driver
    sd_drv.letter = 'S';    // Drive letter S:
    sd_drv.cache_size = 0;  // No caching

    // File operations
    sd_drv.open_cb = sd_open_cb;
    sd_drv.close_cb = sd_close_cb;
    sd_drv.read_cb = sd_read_cb;
    sd_drv.write_cb = sd_write_cb;
    sd_drv.seek_cb = sd_seek_cb;
    sd_drv.tell_cb = sd_tell_cb;

    // Directory operations
    sd_drv.dir_open_cb = sd_dir_open_cb;
    sd_drv.dir_read_cb = sd_dir_read_cb;
    sd_drv.dir_close_cb = sd_dir_close_cb;

    // Register the driver
    lv_fs_drv_register(&sd_drv);

    initialized = true;
    ESP_LOGI(TAG, "LVGL SD filesystem driver registered successfully with drive letter 'S:'");
    return true;
}

void deinit(void) {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing LVGL SD filesystem driver");

    // Note: LVGL doesn't provide an unregister function, so we just mark as not initialized
    initialized = false;

    ESP_LOGI(TAG, "LVGL SD filesystem driver deinitialized");
}

bool isReady(void) {
    return initialized && Hardware::SD::isMounted();
}

}  // namespace LVGLSDFilesystem
}  // namespace Display
