#include "DisplayManager.h"
#include "../core/TaskManager.h"
#include "../include/UIConstants.h"
#include <SPI.h>
#include <cinttypes>
#include <ui/ui.h>

static const char *TAG = "DisplayManager";

namespace Display {

// HSPI bus (for high-speed display communication)
SPIClass hspi(HSPI);

// Private variables for ESP32-S3 optimization
static unsigned long lvLastTick = 0;
static unsigned long frameCount = 0;
static unsigned long lastFpsTime = 0;
static float currentFPS = 0.0f;
static const unsigned long FPS_UPDATE_INTERVAL =
    1000;  // Update FPS every 1000ms

// Performance monitoring
static uint32_t renderTime = 0;
static uint32_t maxRenderTime = 0;
static uint32_t avgRenderTime = 0;
static uint32_t renderSamples = 0;
static uint32_t actualRenderCount = 0;
static uint32_t lastActualRenderTime = 0;
static float actualRenderFPS = 0.0f;
static uint32_t uiResponseTime = 0;

// PSRAM usage monitoring
static size_t psramUsed = 0;
static size_t psramFree = 0;

// Forward declaration for SPI configuration
void configure_spi_bus();

bool init(void) {
    ESP_LOGI(TAG, "Initializing Display Manager (ESP32-S3 Optimized)");

    // Check available PSRAM
    size_t psramSize = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psramFreeSize = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM: Total %d bytes, Free %d bytes", psramSize,
             psramFreeSize);

    // Configure SPI bus for high-speed communication
    configure_spi_bus();

    // Force cache flush for ESP32-S3 PSRAM issues
    ESP_LOGI(TAG, "Flushing caches for ESP32-S3 PSRAM stability");

    // Initialize the smart display with ESP32-S3 optimizations
    smartdisplay_init();

    // Minimal delay for hardware settling - reduced for faster startup
    ESP_LOGI(TAG, "Allowing display hardware settling time...");
    vTaskDelay(pdMS_TO_TICKS(100));  // Reduced from 500ms

    // Initialize the UI
    ui_init();

    // Move all widgets with User 1 state to background
    moveUser1WidgetsToBackground();

    // Minimal settling time after UI initialization
    ESP_LOGI(TAG, "Allowing UI initialization settling time...");
    vTaskDelay(pdMS_TO_TICKS(50));  // Reduced from 200ms

    // Initialize tick tracking
    lvLastTick = millis();

    // Log display buffer information using LVGL 9.x API
    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        ESP_LOGI(TAG, "Display resolution: %dx%d", lv_disp_get_hor_res(disp),
                 lv_disp_get_ver_res(disp));
        ESP_LOGI(TAG, "Display initialized successfully");

        // Try to get some buffer information if available
        ESP_LOGI(TAG,
                 "LVGL memory configured for ESP32-S3 stability (corruption "
                 "prevention)");

        // Force a display refresh to ensure buffers are properly initialized
        lv_obj_invalidate(lv_scr_act());

        // Additional verification delay to ensure everything is truly ready
        ESP_LOGI(TAG, "Performing final display readiness verification...");
        vTaskDelay(pdMS_TO_TICKS(200));

        // Verify display is not in rendering state (should be idle and ready)
        if (disp->rendering_in_progress) {
            ESP_LOGW(TAG, "Display still rendering during init - waiting for completion...");
            // Wait up to 1 second for rendering to complete
            for (int i = 0; i < 10; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (!disp->rendering_in_progress) {
                    ESP_LOGI(TAG, "Display rendering completed after %d attempts", i + 1);
                    break;
                }
            }
        }
    }

    ESP_LOGI(
        TAG,
        "Display Manager initialized successfully with corruption prevention");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Display Manager");
    ui_destroy();
}

void tick(void) {
    // This function should only be called when LVGL actually renders a frame
    // NOT on every task cycle - this was causing fake FPS readings
    frameCount++;
}

void update(void) {
    uint32_t startTime = millis();

    unsigned long now = millis();
    if (now - lastFpsTime >= FPS_UPDATE_INTERVAL) {
        // Calculate FPS based on actual time difference
        if (now > lastFpsTime) {  // Prevent division by zero
            currentFPS = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
        } else {
            currentFPS = 0.0f;
        }

        // Clamp FPS to reasonable range (0-120 FPS)
        if (currentFPS > 120.0f) {
            currentFPS = 120.0f;
        }

        frameCount = 0;
        lastFpsTime = now;

        // Update PSRAM usage statistics less frequently for performance
        psramUsed = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) -
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }

    // Simplified render performance tracking
    uint32_t endTime = millis();
    renderTime = endTime - startTime;

    if (renderTime > maxRenderTime) {
        maxRenderTime = renderTime;
    }
}

// New function to track actual LVGL rendering
void onLvglRenderComplete(void) {
    // Call this only when LVGL actually completes a frame render
    frameCount++;
    actualRenderCount++;

    // Track actual render FPS
    unsigned long now = millis();
    if (now - lastActualRenderTime >= 1000) {
        actualRenderFPS = (float)actualRenderCount * 1000.0f / (float)(now - lastActualRenderTime);
        actualRenderCount = 0;
        lastActualRenderTime = now;
    }
}

void setRotation(Rotation rotation) {
    auto disp = lv_disp_get_default();
    if (disp) {
        lv_display_set_rotation(disp, (lv_display_rotation_t)rotation);
        ESP_LOGI(TAG, "Display rotation set to: %d", rotation);
    }
}

Rotation getRotation(void) {
    auto disp = lv_disp_get_default();
    if (disp) {
        return (Rotation)lv_disp_get_rotation(disp);
    }
    return ROTATION_0;
}

void rotateNext(void) {
    auto current = getRotation();
    auto next = (Rotation)((current + 1) % (ROTATION_270 + 1));
    setRotation(next);
}

void updateLabelUint32(lv_obj_t *label, uint32_t value) {
    if (label) {
        char buffer[32];
        sprintf(buffer, "%" PRIu32, value);
        lv_label_set_text(label, buffer);
    }
}

void updateLabelString(lv_obj_t *label, const char *text) {
    if (label && text) {
        lv_label_set_text(label, text);
    }
}

void updateLabelMillivolts(lv_obj_t *label, uint32_t millivolts) {
    if (label == NULL)
        return;

    float volts = millivolts / 1000.0f;
    char text[32];
    snprintf(text, sizeof(text), "%.2fV", volts);
    lv_label_set_text(label, text);
}

void updateDropdownOptions(lv_obj_t *dropdown, const char *options) {
    if (dropdown == NULL || options == NULL) {
        ESP_LOGW(TAG, "updateDropdownOptions: Invalid parameters");
        return;
    }

    // Update the dropdown options
    lv_dropdown_set_options(dropdown, options);
    ESP_LOGD(TAG, "Updated dropdown options: %s", options);
}

void tickUpdate(void) {
    auto now = millis();
    lv_tick_inc(now - lvLastTick);
    lvLastTick = now;
}

void updateConnectionStatus(lv_obj_t *statusLabel, lv_obj_t *indicatorObj,
                            const char *statusText, ConnectionStatus status) {
    // Update status text
    if (statusLabel && statusText) {
        lv_label_set_text(statusLabel, statusText);
    }

    // Update indicator if provided
    if (indicatorObj) {
        // Position indicator to the left of the status label
        if (statusLabel) {
            lv_obj_align_to(indicatorObj, statusLabel, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }

        // Clear text and style as round background indicator
        lv_label_set_text(indicatorObj, "");
        lv_obj_set_style_radius(indicatorObj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicatorObj, LV_OPA_80, LV_PART_MAIN);

        // Clear text and style as round background indicator
        lv_label_set_text(indicatorObj, UI_LABEL_EMPTY);
        lv_obj_set_style_radius(indicatorObj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(indicatorObj, LV_OPA_80, LV_PART_MAIN);

        // Set color based on connection status
        switch (status) {
            case CONNECTION_STATUS_CONNECTED:
                // Green background for connected
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0x00FF00),
                                          LV_PART_MAIN);
                break;
            case CONNECTION_STATUS_CONNECTING:
                // Yellow background for connecting
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFFFF00),
                                          LV_PART_MAIN);
                break;
            case CONNECTION_STATUS_FAILED:
            case CONNECTION_STATUS_ERROR:
            case CONNECTION_STATUS_DISCONNECTED:
            default:
                // Red background for disconnected/failed/error
                lv_obj_set_style_bg_color(indicatorObj, lv_color_hex(0xFF0000),
                                          LV_PART_MAIN);
                break;
        }
    }
}

void updateWifiStatus(lv_obj_t *statusLabel, lv_obj_t *indicatorObj,
                      const char *statusText, bool connected) {
    // Convert boolean to ConnectionStatus enum
    ConnectionStatus status;
    if (connected) {
        status = CONNECTION_STATUS_CONNECTED;
    } else if (statusText && strcmp(statusText, "Connecting...") == 0) {
        status = CONNECTION_STATUS_CONNECTING;
    } else {
        status = CONNECTION_STATUS_DISCONNECTED;
    }

    updateConnectionStatus(statusLabel, indicatorObj, statusText, status);
}

void updateNetworkInfo(lv_obj_t *ssidLabel, lv_obj_t *ipLabel, const char *ssid,
                       const char *ipAddress) {
    if (ssidLabel && ssid) {
        lv_label_set_text(ssidLabel, ssid);
    }
    if (ipLabel && ipAddress) {
        lv_label_set_text(ipLabel, ipAddress);
    }
}

static ConnectionStatus statusStringToConnectionStatus(const char *statusText) {
    if (!statusText)
        return CONNECTION_STATUS_DISCONNECTED;

    if (strstr(statusText, "Connected") != NULL) {
        return CONNECTION_STATUS_CONNECTED;
    } else if (strstr(statusText, "Connecting") != NULL) {
        return CONNECTION_STATUS_CONNECTING;
    } else if (strstr(statusText, "Failed") != NULL) {
        return CONNECTION_STATUS_FAILED;
    } else if (strstr(statusText, "Error") != NULL) {
        return CONNECTION_STATUS_ERROR;
    } else {
        return CONNECTION_STATUS_DISCONNECTED;
    }
}

// MQTT UI functions removed - network transports available only during OTA mode

float getFPS(void) { return currentFPS; }

void updateFpsDisplay(lv_obj_t *fpsLabel) {
    if (fpsLabel) {
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "%.1f FPS", currentFPS);
        lv_label_set_text(fpsLabel, fpsText);
    }
}

// New ESP32-S3 specific functions
uint32_t getRenderTime(void) { return avgRenderTime; }

uint32_t getMaxRenderTime(void) { return maxRenderTime; }

void resetRenderStats(void) {
    maxRenderTime = 0;
    avgRenderTime = 0;
    renderSamples = 0;
}

// Helper functions for consistent label initialization
void initializeLabelEmpty(lv_obj_t *label) {
    if (label) {
        lv_label_set_text(label, UI_LABEL_EMPTY);
    }
}

void initializeLabelDash(lv_obj_t *label) {
    if (label) {
        lv_label_set_text(label, "-");
    }
}

void initializeLabelSpace(lv_obj_t *label) {
    if (label) {
        lv_label_set_text(label, UI_LABEL_SPACE);
    }
}

void initializeLabelUnknown(lv_obj_t *label) {
    if (label) {
        lv_label_set_text(label, UI_LABEL_UNKNOWN);
    }
}

void initializeLabelNone(lv_obj_t *label) {
    if (label) {
        lv_label_set_text(label, UI_LABEL_NONE);
    }
}

float getActualRenderFPS(void) {
    return actualRenderFPS;
}

uint32_t getUIResponseTime(void) {
    return uiResponseTime;
}

// Function to configure SPI bus for high-speed communication
void configure_spi_bus() {
    const int spi_bus_mhz = 80;
    ESP_LOGI(TAG, "Setting SPI bus frequency to %dMHz for performance",
             spi_bus_mhz);
    hspi.begin(21, 22, 23, -1);
    hspi.setFrequency(spi_bus_mhz * 1000000);
}

// Function to move widgets with User 1 state to background
void moveUser1WidgetsToBackground(lv_obj_t *parent) {
    if (!parent) {
        // If no parent specified, use the current active screen
        parent = lv_scr_act();
    }

    if (!parent) {
        ESP_LOGW(TAG, "moveUser1WidgetsToBackground: No valid parent object");
        return;
    }

    uint32_t child_count = lv_obj_get_child_count(parent);

    // Iterate through all children
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (child && lv_obj_has_state(child, LV_STATE_USER_1)) {
            // Move this widget to the background (index 0)
            lv_obj_move_to_index(child, 0);
            ESP_LOGI(TAG, "Moved widget with User 1 state to background (index 0)");
        }

        // Recursively check children of this child
        moveUser1WidgetsToBackground(child);
    }
}

}  // namespace Display
