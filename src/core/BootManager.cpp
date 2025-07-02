#include "BootManager.h"
#include <esp_log.h>
#include <esp_system.h>

static const char* TAG = "BootManager";

namespace Boot {

// Static member definitions
BootMode BootManager::currentMode = BootMode::NORMAL;
bool BootManager::initialized = false;
const char* BootManager::NVS_NAMESPACE = "boot_mgr";
const char* BootManager::NVS_BOOT_MODE_KEY = "boot_mode";
const char* BootManager::NVS_OTA_REQUEST_KEY = "ota_request";

bool BootManager::init() {
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing Boot Manager...");

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Determine current boot mode
    currentMode = determineBootMode();

    ESP_LOGI(TAG, "Boot Manager initialized - Mode: %s", getBootReasonString());
    initialized = true;

    return true;
}

BootMode BootManager::getCurrentMode() {
    return currentMode;
}

void BootManager::requestOTAMode() {
    ESP_LOGI(TAG, "OTA mode requested - system will restart into OTA mode");

    // Set OTA request flag in NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t ota_request = 1;
        nvs_set_u8(nvs_handle, NVS_OTA_REQUEST_KEY, ota_request);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        ESP_LOGI(TAG, "OTA request flag set, restarting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for OTA request: %s", esp_err_to_name(err));
    }
}

void BootManager::requestNormalMode() {
    ESP_LOGI(TAG, "Normal mode requested");
    writeBootModeToNVS(BootMode::NORMAL);
    clearBootRequest();
}

bool BootManager::isOTAModeRequested() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t ota_request = 0;
    size_t required_size = sizeof(ota_request);
    err = nvs_get_u8(nvs_handle, NVS_OTA_REQUEST_KEY, &ota_request);
    nvs_close(nvs_handle);

    return (err == ESP_OK && ota_request == 1);
}

void BootManager::clearBootRequest() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, NVS_OTA_REQUEST_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

const char* BootManager::getBootReasonString() {
    switch (currentMode) {
        case BootMode::NORMAL:
            return "NORMAL";
        case BootMode::OTA_UPDATE:
            return "OTA_UPDATE";
        case BootMode::FACTORY:
            return "FACTORY";
        case BootMode::RECOVERY:
            return "RECOVERY";
        default:
            return "UNKNOWN";
    }
}

BootMode BootManager::readBootModeFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return BootMode::NORMAL;
    }

    uint8_t mode_value = 0;
    err = nvs_get_u8(nvs_handle, NVS_BOOT_MODE_KEY, &mode_value);
    nvs_close(nvs_handle);

    if (err == ESP_OK && mode_value <= static_cast<uint8_t>(BootMode::RECOVERY)) {
        return static_cast<BootMode>(mode_value);
    }

    return BootMode::NORMAL;
}

bool BootManager::writeBootModeToNVS(BootMode mode) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t mode_value = static_cast<uint8_t>(mode);
    err = nvs_set_u8(nvs_handle, NVS_BOOT_MODE_KEY, mode_value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    return (err == ESP_OK);
}

BootMode BootManager::determineBootMode() {
    // Check if OTA mode was explicitly requested
    if (isOTAModeRequested()) {
        ESP_LOGI(TAG, "OTA mode requested via NVS flag");
        return BootMode::OTA_UPDATE;
    }

    // Check reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    switch (reset_reason) {
        case ESP_RST_POWERON:
        case ESP_RST_SW:
        case ESP_RST_DEEPSLEEP:
            // Normal boot reasons
            break;

        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            ESP_LOGW(TAG, "System recovered from error - reset reason: %d", reset_reason);
            break;

        case ESP_RST_BROWNOUT:
            ESP_LOGW(TAG, "Brownout reset detected");
            break;

        default:
            ESP_LOGW(TAG, "Unknown reset reason: %d", reset_reason);
            break;
    }

    // Read persisted boot mode
    BootMode persistedMode = readBootModeFromNVS();

    // Default to normal mode
    return (persistedMode != BootMode::NORMAL) ? persistedMode : BootMode::NORMAL;
}

}  // namespace Boot
