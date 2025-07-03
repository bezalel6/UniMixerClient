#include "EnhancedOTAUI.h"
#include "MultithreadedOTA.h"
#include "LVGLMessageHandler.h"
#include "BootManager.h"
#include <esp_log.h>

static const char* TAG = "EnhancedOTAUI";

namespace UI {
namespace OTA {

// =============================================================================
// STATIC UI OBJECTS
// =============================================================================

static lv_obj_t* s_otaScreen = nullptr;
static lv_obj_t* s_progressBar = nullptr;
static lv_obj_t* s_progressLabel = nullptr;
static lv_obj_t* s_statusLabel = nullptr;
static lv_obj_t* s_logArea = nullptr;
static lv_obj_t* s_speedLabel = nullptr;
static lv_obj_t* s_etaLabel = nullptr;
static lv_obj_t* s_statsLabel = nullptr;

// Control buttons
static lv_obj_t* s_exitButton = nullptr;
static lv_obj_t* s_retryButton = nullptr;
static lv_obj_t* s_rebootButton = nullptr;

// UI state
static bool s_uiCreated = false;
static uint32_t s_lastProgressUpdate = 0;
static uint32_t s_lastStatsUpdate = 0;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

static const char* getStateDisplayString(MultiOTA::OTAState_t state) {
    switch (state) {
        case MultiOTA::OTA_STATE_IDLE: return "Ready";
        case MultiOTA::OTA_STATE_INITIALIZING: return "Initializing";
        case MultiOTA::OTA_STATE_CONNECTING: return "Connecting to WiFi";
        case MultiOTA::OTA_STATE_CONNECTED: return "WiFi Connected";
        case MultiOTA::OTA_STATE_DOWNLOADING: return "Downloading Firmware";
        case MultiOTA::OTA_STATE_INSTALLING: return "Installing Update";
        case MultiOTA::OTA_STATE_VERIFYING: return "Verifying Installation";
        case MultiOTA::OTA_STATE_SUCCESS: return "Update Complete";
        case MultiOTA::OTA_STATE_FAILED: return "Update Failed";
        case MultiOTA::OTA_STATE_CANCELLED: return "Update Cancelled";
        case MultiOTA::OTA_STATE_CLEANUP: return "Cleaning Up";
        default: return "Unknown State";
    }
}

static lv_color_t getStateColor(MultiOTA::OTAState_t state) {
    switch (state) {
        case MultiOTA::OTA_STATE_SUCCESS: return lv_color_hex(0x00FF00);
        case MultiOTA::OTA_STATE_FAILED: return lv_color_hex(0xFF0000);
        case MultiOTA::OTA_STATE_CANCELLED: return lv_color_hex(0xFFAA00);
        case MultiOTA::OTA_STATE_DOWNLOADING:
        case MultiOTA::OTA_STATE_INSTALLING:
        case MultiOTA::OTA_STATE_VERIFYING: return lv_color_hex(0x00AAFF);
        default: return lv_color_hex(0x00FF88);
    }
}

static void formatBytes(uint32_t bytes, char* buffer, size_t bufferSize) {
    if (bytes >= 1024 * 1024) {
        snprintf(buffer, bufferSize, "%.1f MB", bytes / (1024.0f * 1024.0f));
    } else if (bytes >= 1024) {
        snprintf(buffer, bufferSize, "%.1f KB", bytes / 1024.0f);
    } else {
        snprintf(buffer, bufferSize, "%lu B", bytes);
    }
}

static void formatTime(uint32_t seconds, char* buffer, size_t bufferSize) {
    if (seconds >= 3600) {
        uint32_t hours = seconds / 3600;
        uint32_t mins = (seconds % 3600) / 60;
        snprintf(buffer, bufferSize, "%luh %lum", hours, mins);
    } else if (seconds >= 60) {
        uint32_t mins = seconds / 60;
        uint32_t secs = seconds % 60;
        snprintf(buffer, bufferSize, "%lum %lus", mins, secs);
    } else {
        snprintf(buffer, bufferSize, "%lus", seconds);
    }
}

// =============================================================================
// BUTTON EVENT HANDLERS
// =============================================================================

static void exitButtonEventHandler(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Exit button clicked - returning to normal mode");
        addLogMessage("User requested exit to normal mode");

        // Clear OTA request and restart normally
        Boot::BootManager::clearBootRequest();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

static void retryButtonEventHandler(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Retry button clicked - restarting OTA process");
        addLogMessage("User requested OTA retry");

        // Hide retry/reboot buttons
        if (s_retryButton) lv_obj_add_flag(s_retryButton, LV_OBJ_FLAG_HIDDEN);
        if (s_rebootButton) lv_obj_add_flag(s_rebootButton, LV_OBJ_FLAG_HIDDEN);

        MultiOTA::retryOTA();
    }
}

static void rebootButtonEventHandler(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Reboot button clicked - restarting system");
        addLogMessage("User requested system reboot");

        // Clear OTA request and restart
        Boot::BootManager::clearBootRequest();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

// =============================================================================
// UI CREATION FUNCTIONS
// =============================================================================

static void createProgressSection(lv_obj_t* parent) {
    // Progress container
    lv_obj_t* progressContainer = lv_obj_create(parent);
    lv_obj_set_size(progressContainer, 700, 120);
    lv_obj_set_align(progressContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(progressContainer, 80);
    lv_obj_set_style_bg_color(progressContainer, lv_color_hex(0x002244), LV_PART_MAIN);
    lv_obj_set_style_border_color(progressContainer, lv_color_hex(0x0066AA), LV_PART_MAIN);
    lv_obj_set_style_border_width(progressContainer, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(progressContainer, 10, LV_PART_MAIN);
    lv_obj_remove_flag(progressContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Progress bar
    s_progressBar = lv_bar_create(progressContainer);
    lv_obj_set_size(s_progressBar, 650, 25);
    lv_obj_set_align(s_progressBar, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_progressBar, 15);
    lv_bar_set_value(s_progressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progressBar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progressBar, lv_color_hex(0x00AA00), LV_PART_INDICATOR);

    // Progress percentage label
    s_progressLabel = lv_label_create(progressContainer);
    lv_label_set_text(s_progressLabel, "0% - Starting OTA update...");
    lv_obj_set_align(s_progressLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_progressLabel, 50);
    lv_obj_set_style_text_color(s_progressLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_progressLabel, &lv_font_montserrat_16, LV_PART_MAIN);

    // Speed and ETA labels
    s_speedLabel = lv_label_create(progressContainer);
    lv_label_set_text(s_speedLabel, "Speed: --");
    lv_obj_set_align(s_speedLabel, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(s_speedLabel, 20, -10);
    lv_obj_set_style_text_color(s_speedLabel, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_speedLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    s_etaLabel = lv_label_create(progressContainer);
    lv_label_set_text(s_etaLabel, "ETA: --");
    lv_obj_set_align(s_etaLabel, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_pos(s_etaLabel, -20, -10);
    lv_obj_set_style_text_color(s_etaLabel, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_etaLabel, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void createStatusSection(lv_obj_t* parent) {
    // Status label
    s_statusLabel = lv_label_create(parent);
    lv_label_set_text(s_statusLabel, "OTA MODE ACTIVE");
    lv_obj_set_align(s_statusLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_statusLabel, 220);
    lv_obj_set_style_text_color(s_statusLabel, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_18, LV_PART_MAIN);
}

static void createLogSection(lv_obj_t* parent) {
    // Log container
    lv_obj_t* logContainer = lv_obj_create(parent);
    lv_obj_set_size(logContainer, 750, 200);
    lv_obj_set_align(logContainer, LV_ALIGN_CENTER);
    lv_obj_set_y(logContainer, 40);
    lv_obj_set_style_bg_color(logContainer, lv_color_hex(0x000011), LV_PART_MAIN);
    lv_obj_set_style_border_color(logContainer, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(logContainer, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(logContainer, 5, LV_PART_MAIN);

    // Log title
    lv_obj_t* logTitle = lv_label_create(logContainer);
    lv_label_set_text(logTitle, "OTA LOG");
    lv_obj_set_align(logTitle, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(logTitle, 10, 5);
    lv_obj_set_style_text_color(logTitle, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(logTitle, &lv_font_montserrat_12, LV_PART_MAIN);

    // Log text area
    s_logArea = lv_textarea_create(logContainer);
    lv_obj_set_size(s_logArea, 730, 170);
    lv_obj_set_align(s_logArea, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(s_logArea, -5);
    lv_textarea_set_text(s_logArea, "OTA system initialized with multithreaded architecture\n");
    lv_textarea_set_cursor_click_pos(s_logArea, false);
    lv_obj_set_style_bg_color(s_logArea, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_logArea, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_logArea, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_flag(s_logArea, LV_OBJ_FLAG_SCROLLABLE);
}

static void createStatsSection(lv_obj_t* parent) {
    // Statistics label (top right corner)
    s_statsLabel = lv_label_create(parent);
    lv_label_set_text(s_statsLabel, "Tasks: 4 | Queues: 3 | Memory: OK");
    lv_obj_set_align(s_statsLabel, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(s_statsLabel, -10, 10);
    lv_obj_set_style_text_color(s_statsLabel, lv_color_hex(0xAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_statsLabel, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void createControlButtons(lv_obj_t* parent) {
    // Button container
    lv_obj_t* buttonContainer = lv_obj_create(parent);
    lv_obj_set_size(buttonContainer, 500, 60);
    lv_obj_set_align(buttonContainer, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(buttonContainer, -20);
    lv_obj_set_style_bg_opa(buttonContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(buttonContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(buttonContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Exit button (always visible)
    s_exitButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(s_exitButton, 130, 45);
    lv_obj_set_style_bg_color(s_exitButton, lv_color_hex(0xFF6600), LV_PART_MAIN);

    lv_obj_t* exitLabel = lv_label_create(s_exitButton);
    lv_label_set_text(exitLabel, "EXIT OTA");
    lv_obj_center(exitLabel);
    lv_obj_set_style_text_color(exitLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(exitLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_event_cb(s_exitButton, exitButtonEventHandler, LV_EVENT_CLICKED, NULL);

    // Retry button (hidden initially)
    s_retryButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(s_retryButton, 130, 45);
    lv_obj_set_style_bg_color(s_retryButton, lv_color_hex(0x3366FF), LV_PART_MAIN);
    lv_obj_add_flag(s_retryButton, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* retryLabel = lv_label_create(s_retryButton);
    lv_label_set_text(retryLabel, "RETRY");
    lv_obj_center(retryLabel);
    lv_obj_set_style_text_color(retryLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(retryLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_event_cb(s_retryButton, retryButtonEventHandler, LV_EVENT_CLICKED, NULL);

    // Reboot button (hidden initially)
    s_rebootButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(s_rebootButton, 130, 45);
    lv_obj_set_style_bg_color(s_rebootButton, lv_color_hex(0xFF3333), LV_PART_MAIN);
    lv_obj_add_flag(s_rebootButton, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* rebootLabel = lv_label_create(s_rebootButton);
    lv_label_set_text(rebootLabel, "REBOOT");
    lv_obj_center(rebootLabel);
    lv_obj_set_style_text_color(rebootLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(rebootLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_event_cb(s_rebootButton, rebootButtonEventHandler, LV_EVENT_CLICKED, NULL);
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool createEnhancedOTAScreen() {
    if (s_uiCreated) {
        ESP_LOGW(TAG, "Enhanced OTA screen already created");
        return true;
    }

    ESP_LOGI(TAG, "Creating enhanced multithreaded OTA screen");

    // Create full-screen container
    s_otaScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_otaScreen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_otaScreen, 0, 0);
    lv_obj_set_style_bg_color(s_otaScreen, lv_color_hex(0x001122), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_otaScreen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_otaScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* titleLabel = lv_label_create(s_otaScreen);
    lv_label_set_text(titleLabel, "MULTITHREADED OTA UPDATE");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_26, LV_PART_MAIN);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x00CCFF), LV_PART_MAIN);
    lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(titleLabel, 20);

    // Create all UI sections
    createProgressSection(s_otaScreen);
    createStatusSection(s_otaScreen);
    createLogSection(s_otaScreen);
    createStatsSection(s_otaScreen);
    createControlButtons(s_otaScreen);

    s_uiCreated = true;

    // Force immediate UI refresh
    lv_refr_now(lv_disp_get_default());

    ESP_LOGI(TAG, "Enhanced multithreaded OTA screen created successfully");
    return true;
}

void updateEnhancedOTAScreen() {
    if (!s_uiCreated) return;

    uint32_t now = millis();

    // Throttle updates to prevent UI overload (10 FPS for progress updates)
    if (now - s_lastProgressUpdate < 100) return;
    s_lastProgressUpdate = now;

    // Get current progress
    MultiOTA::DetailedProgress_t progress = MultiOTA::getProgress();

    // Update progress bar
    if (s_progressBar && lv_obj_is_valid(s_progressBar)) {
        lv_bar_set_value(s_progressBar, progress.overallProgress, LV_ANIM_ON);
    }

    // Update progress label
    if (s_progressLabel && lv_obj_is_valid(s_progressLabel)) {
        char progressText[256];
        snprintf(progressText, sizeof(progressText), "%d%% - %s",
                 progress.overallProgress, progress.detailedMessage);
        lv_label_set_text(s_progressLabel, progressText);
    }

    // Update status label
    if (s_statusLabel && lv_obj_is_valid(s_statusLabel)) {
        const char* stateStr = getStateDisplayString(progress.state);
        lv_label_set_text(s_statusLabel, stateStr);
        lv_obj_set_style_text_color(s_statusLabel, getStateColor(progress.state), LV_PART_MAIN);
    }

    // Update speed and ETA
    if (s_speedLabel && lv_obj_is_valid(s_speedLabel)) {
        char speedText[64];
        if (progress.downloadSpeed > 0) {
            char speedStr[32];
            formatBytes(progress.downloadSpeed, speedStr, sizeof(speedStr));
            snprintf(speedText, sizeof(speedText), "Speed: %s/s", speedStr);
        } else {
            snprintf(speedText, sizeof(speedText), "Speed: --");
        }
        lv_label_set_text(s_speedLabel, speedText);
    }

    if (s_etaLabel && lv_obj_is_valid(s_etaLabel)) {
        char etaText[64];
        if (progress.eta > 0 && progress.state == MultiOTA::OTA_STATE_DOWNLOADING) {
            char etaStr[32];
            formatTime(progress.eta, etaStr, sizeof(etaStr));
            snprintf(etaText, sizeof(etaText), "ETA: %s", etaStr);
        } else {
            snprintf(etaText, sizeof(etaText), "ETA: --");
        }
        lv_label_set_text(s_etaLabel, etaText);
    }

    // Update statistics (less frequently)
    if (s_statsLabel && lv_obj_is_valid(s_statsLabel) && (now - s_lastStatsUpdate >= 2000)) {
        MultiOTA::OTAStats_t stats = MultiOTA::getStats();
        char statsText[128];
        snprintf(statsText, sizeof(statsText),
                 "UI: %luB | Net: %luB | Down: %luB | Mon: %luB | Errors: %lu",
                 stats.uiTaskHighWaterMark,
                 stats.networkTaskHighWaterMark,
                 stats.downloadTaskHighWaterMark,
                 stats.monitorTaskHighWaterMark,
                 stats.errorCount);
        lv_label_set_text(s_statsLabel, statsText);
        s_lastStatsUpdate = now;
    }

    // Show/hide control buttons based on state
    if (progress.state == MultiOTA::OTA_STATE_FAILED) {
        if (s_retryButton && progress.canRetry) {
            lv_obj_remove_flag(s_retryButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_rebootButton) {
            lv_obj_remove_flag(s_rebootButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void addLogMessage(const char* message) {
    if (!s_uiCreated || !message) return;

    if (s_logArea && lv_obj_is_valid(s_logArea)) {
        // Add timestamp and message
        char timestampedMessage[300];
        uint32_t seconds = millis() / 1000;
        uint32_t minutes = seconds / 60;
        seconds = seconds % 60;

        snprintf(timestampedMessage, sizeof(timestampedMessage),
                 "[%02lu:%02lu] %s\n", minutes, seconds, message);

        lv_textarea_add_text(s_logArea, timestampedMessage);

        // Auto-scroll to bottom
        lv_textarea_set_cursor_pos(s_logArea, LV_TEXTAREA_CURSOR_LAST);
    }
}

void destroyEnhancedOTAScreen() {
    if (!s_uiCreated) return;

    ESP_LOGI(TAG, "Destroying enhanced OTA screen");

    if (s_otaScreen && lv_obj_is_valid(s_otaScreen)) {
        // Remove all event callbacks first
        lv_obj_remove_event_cb(s_otaScreen, nullptr);

        // Clear all child event callbacks
        uint32_t child_count = lv_obj_get_child_count(s_otaScreen);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* child = lv_obj_get_child(s_otaScreen, i);
            if (child && lv_obj_is_valid(child)) {
                lv_obj_remove_event_cb(child, nullptr);
            }
        }

        // Give LVGL time to process event callback removals
        vTaskDelay(pdMS_TO_TICKS(10));

        // Delete the screen
        lv_obj_del(s_otaScreen);

        // Give LVGL time to process deletion
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Reset all object pointers
    s_otaScreen = nullptr;
    s_progressBar = nullptr;
    s_progressLabel = nullptr;
    s_statusLabel = nullptr;
    s_logArea = nullptr;
    s_speedLabel = nullptr;
    s_etaLabel = nullptr;
    s_statsLabel = nullptr;
    s_exitButton = nullptr;
    s_retryButton = nullptr;
    s_rebootButton = nullptr;

    s_uiCreated = false;

    ESP_LOGI(TAG, "Enhanced OTA screen destroyed");
}

bool isEnhancedOTAScreenCreated() {
    return s_uiCreated;
}

} // namespace OTA
} // namespace UI
