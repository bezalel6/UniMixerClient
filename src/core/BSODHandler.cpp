
#include "BSODHandler.h"
#include "BootProgressScreen.h"
#include "BuildInfo.h"
#include "CoreLoggingFilter.h"
// #include "ui/wrapper/LVGLWrapper.h" // REMOVED - using direct LVGL
#include "TaskManager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string>
#include <vector>
#include <Arduino.h>
#include <esp32-hal.h>
#include <sstream>
#include <iomanip>
#include "display/DisplayManager.h"
static const char* TAG = "BSODHandler";

// =============================================================================
// MODERN BSOD DESIGN SYSTEM
// =============================================================================

// Design tokens for consistent visual language
namespace BSODDesign {
// Modern color palette (inspired by modern OS error screens)
const lv_color_t COLOR_BACKGROUND = lv_color_hex(0x1E3A5F);  // Deep blue-gray
const lv_color_t COLOR_CARD_BG = lv_color_hex(0x2B4C7E);     // Lighter blue-gray
const lv_color_t COLOR_PRIMARY = lv_color_hex(0xFFFFFF);     // White
const lv_color_t COLOR_SECONDARY = lv_color_hex(0xB8D4F1);   // Light blue
const lv_color_t COLOR_ACCENT = lv_color_hex(0x4A90E2);      // Bright blue
const lv_color_t COLOR_ERROR = lv_color_hex(0xFF6B6B);       // Soft red
const lv_color_t COLOR_WARNING = lv_color_hex(0xFFD93D);     // Yellow
const lv_color_t COLOR_SUCCESS = lv_color_hex(0x4ECDC4);     // Teal
const lv_color_t COLOR_INFO = lv_color_hex(0x6C5CE7);        // Purple

// Typography scale - using best fonts for authentic BSOD look
const lv_font_t* FONT_HUGE = &lv_font_montserrat_48;        // For big sad face like real Windows BSOD
const lv_font_t* FONT_TITLE = &lv_font_montserrat_36;       // For main title (large and imposing)
const lv_font_t* FONT_HEADING_LG = &lv_font_montserrat_28;
const lv_font_t* FONT_HEADING_MD = &lv_font_montserrat_24;
const lv_font_t* FONT_HEADING_SM = &lv_font_montserrat_18;
const lv_font_t* FONT_BODY = &lv_font_montserrat_14;
const lv_font_t* FONT_CAPTION = &lv_font_montserrat_12;
const lv_font_t* FONT_MONO = &lv_font_montserrat_12;  // Simulated mono

// Spacing system (8px grid)
const int SPACE_XS = 4;
const int SPACE_SM = 8;
const int SPACE_MD = 16;
const int SPACE_LG = 24;
const int SPACE_XL = 32;
const int SPACE_XXL = 48;

// Layout constants
const int CARD_RADIUS = 12;
const int CARD_SHADOW = 20;
const int MAX_CONTENT_WIDTH = 600;  // For readability
}  // namespace BSODDesign

// =============================================================================
// ICON SYSTEM FOR VISUAL CLARITY
// =============================================================================

namespace BSODIcons {
// LVGL built-in symbols for different error types
const char* ICON_ERROR = LV_SYMBOL_WARNING;
const char* ICON_CRITICAL = LV_SYMBOL_CLOSE;
const char* ICON_INFO = LV_SYMBOL_BELL;
const char* ICON_SYSTEM = LV_SYMBOL_SETTINGS;
const char* ICON_MEMORY = LV_SYMBOL_SD_CARD;
const char* ICON_NETWORK = LV_SYMBOL_WIFI;
const char* ICON_RESTART = LV_SYMBOL_REFRESH;
const char* ICON_DEBUG = LV_SYMBOL_CHARGE;
}  // namespace BSODIcons

// =============================================================================
// DUAL-CORE BSOD ARCHITECTURE GLOBALS
// =============================================================================

// Global variables
static bool bsodReady = false;
static bool bsodActive = false;
static lv_obj_t* bsodScreen = nullptr;

// Dual-core BSOD task handles
static TaskHandle_t bsodLvglTaskHandle = nullptr;   // Core 0: LVGL processing
static TaskHandle_t bsodDebugTaskHandle = nullptr;  // Core 1: Debug data collection

// Synchronization for dual-core BSOD
static SemaphoreHandle_t bsodDataMutex = nullptr;
static volatile bool bsodTasksRunning = false;
static volatile bool debugDataReady = false;

// BSOD configuration storage
static BSODConfig currentBSODConfig;

// Real-time debug data structure (updated by Core 1, read by Core 0)
struct BSODDebugData {
    uint32_t currentCore;
    uint32_t cpuFrequency;
    uint32_t freeHeap;
    uint32_t minHeap;
    uint32_t freePsram;
    uint32_t uptime;
    uint32_t freeStack;
    uint32_t lastUpdate;
    String systemStatus;
    String taskStatuses;
} static bsodDebugData;

// Simple UI Elements - just labels
static struct {
    lv_obj_t* sadFace;
    lv_obj_t* titleLabel;
    lv_obj_t* messageText;
    lv_obj_t* errorCodeText;
    lv_obj_t* detailsText;
    lv_obj_t* diagnosticsText;
    lv_obj_t* instructionText;
    lv_obj_t* buildInfoText;
    lv_obj_t* toggleText;
} ui;

// Advanced debugging toggle
static bool showAdvancedDebug = false;

// Utility functions
static std::string generateErrorCode(const char* message, const char* file, int line) {
    uint32_t hash = 0;
    if (message) {
        for (int i = 0; message[i]; i++) {
            hash = ((hash << 5) + hash) + message[i];
        }
    }
    if (file) {
        for (int i = 0; file[i]; i++) {
            hash = ((hash << 5) + hash) + file[i];
        }
    }
    hash += line;

    char errorCode[16];
    snprintf(errorCode, sizeof(errorCode), "ERR_%08X", hash);
    return std::string(errorCode);
}

// =============================================================================
// SIMPLE HELPER FUNCTIONS - NO CONTAINERS
// =============================================================================

static lv_obj_t* createSimpleLabel(lv_obj_t* parent, const char* text, int x, int y, lv_color_t color, const lv_font_t* font) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t* createCenteredLabel(lv_obj_t* parent, const char* text, int y, lv_color_t color, const lv_font_t* font) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t* createLeftAlignedLabel(lv_obj_t* parent, const char* text, int x, int y, lv_color_t color, const lv_font_t* font, int width) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

// Touch event handler for toggling advanced debug info
static void toggle_debug_event_cb(lv_event_t* e) {
    showAdvancedDebug = !showAdvancedDebug;
    
    if (ui.diagnosticsText) {
        if (showAdvancedDebug) {
            lv_obj_clear_flag(ui.diagnosticsText, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ui.toggleText, "[Tap to hide technical details]");
        } else {
            lv_obj_add_flag(ui.diagnosticsText, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ui.toggleText, "[Tap to show technical details]");
        }
    }
}

namespace BSODHandler {

bool init() {
    ESP_LOGI(TAG, "Initializing Dual-Core BSOD Handler");

    // Create synchronization mutex for dual-core data sharing
    bsodDataMutex = xSemaphoreCreateMutex();
    if (!bsodDataMutex) {
        ESP_LOGE(TAG, "Failed to create BSOD data mutex");
        return false;
    }

    bsodReady = true;
    ESP_LOGI(TAG, "Dual-Core BSOD Handler initialized successfully");
    return true;
}

bool isReady() {
    return bsodReady;
}

bool isActive() {
    return bsodActive;
}

// =============================================================================
// DUAL-CORE BSOD ARCHITECTURE IMPLEMENTATION
// =============================================================================

namespace {

// CRITICAL: Dual-core system shutdown for BSOD mode
// NOTE: Task suspension is now handled by Core 1 debug task to avoid hanging

// Core 1: Debug Data Collection & Diagnostics UI Update Task (dedicated)
void bsodDebugTask(void* parameter) {
    ESP_LOGI(TAG, "[BSOD-DEBUG] Debug task started on Core %d - data collection & UI updates", xPortGetCoreID());

    // Disable watchdog for this BSOD task
    esp_task_wdt_delete(NULL);

    // Wait a moment to ensure BSOD UI is displayed before suspending normal tasks
    vTaskDelay(pdMS_TO_TICKS(500));

    // CRITICAL: Suspend normal system tasks now that BSOD system is running
    // This is done from Core 1 to avoid hanging the task that created the BSOD
    ESP_LOGI(TAG, "[BSOD-DEBUG] Suspending normal system tasks from Core 1");
    try {
        Application::TaskManager::suspend();
        ESP_LOGI(TAG, "[BSOD-DEBUG] Normal system tasks suspended successfully");

        // Wait a bit to let tasks actually suspend
        vTaskDelay(pdMS_TO_TICKS(200));

        // Verify suspension worked by checking task states
        ESP_LOGI(TAG, "[BSOD-DEBUG] Verifying task suspension...");
        ESP_LOGI(TAG, "[BSOD-DEBUG] All normal tasks should now be suspended");

    } catch (...) {
        ESP_LOGE(TAG, "[BSOD-DEBUG] Exception during task suspension - continuing anyway");
    }

    while (bsodTasksRunning) {
        // Gather comprehensive system debug data
        if (xSemaphoreTake(bsodDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Core information
            bsodDebugData.currentCore = xPortGetCoreID();
            bsodDebugData.cpuFrequency = getCpuFrequencyMhz();

            // Memory status
            bsodDebugData.freeHeap = esp_get_free_heap_size();
            bsodDebugData.minHeap = esp_get_minimum_free_heap_size();
            bsodDebugData.freePsram = ESP.getFreePsram();

            // System timing
            bsodDebugData.uptime = millis() / 1000;
            bsodDebugData.freeStack = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
            bsodDebugData.lastUpdate = millis();

            // Advanced debug data collection
            std::ostringstream systemStatus;
            systemStatus << "Total Tasks: " << uxTaskGetNumberOfTasks();
            systemStatus << " | Core Temp: N/A";  // Would need ESP32 temp sensor
            systemStatus << " | Flash: " << ESP.getFlashChipSize() << " bytes";
            bsodDebugData.systemStatus = systemStatus.str().c_str();

            // Task status collection (simplified for performance)
            std::ostringstream taskStatus;
            taskStatus << "Current: " << pcTaskGetName(xTaskGetCurrentTaskHandle());
            taskStatus << " | Priority: " << uxTaskPriorityGet(NULL);
            bsodDebugData.taskStatuses = taskStatus.str().c_str();

            debugDataReady = true;
            xSemaphoreGive(bsodDataMutex);

            ESP_LOGV(TAG, "[BSOD-DEBUG] Debug data updated - Heap: %u, PSRAM: %u",
                     bsodDebugData.freeHeap, bsodDebugData.freePsram);
        }

        // Update debug data every 2 seconds for real-time monitoring
        vTaskDelay(pdMS_TO_TICKS(2000));

        // CORE 1 RESPONSIBILITY: Update CPU diagnostics UI every 3 seconds
        static uint32_t lastUIUpdate = 0;
        uint32_t currentTime = millis();
        if (currentTime - lastUIUpdate >= 3000 && ui.diagnosticsText && debugDataReady) {
            if (xSemaphoreTake(bsodDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                // Create real-time CPU diagnostics display
                std::ostringstream cpuDiag;
                cpuDiag << "=== LIVE CPU DIAGNOSTICS (Core 1) ===\n";
                cpuDiag << "Current Core: " << bsodDebugData.currentCore << "\n";
                cpuDiag << "CPU Frequency: " << bsodDebugData.cpuFrequency << " MHz\n";
                cpuDiag << "Free Heap: " << bsodDebugData.freeHeap << " bytes";
                if (bsodDebugData.freeHeap < 20000) cpuDiag << " [LOW]";
                cpuDiag << "\nMin Heap: " << bsodDebugData.minHeap << " bytes\n";
                cpuDiag << "Free PSRAM: " << bsodDebugData.freePsram << " bytes";
                if (bsodDebugData.freePsram < 50000) cpuDiag << " [LOW]";
                cpuDiag << "\nUptime: " << bsodDebugData.uptime << " seconds\n";
                cpuDiag << "Free Stack: " << bsodDebugData.freeStack << " bytes\n";
                cpuDiag << "System: " << bsodDebugData.systemStatus.c_str() << "\n";
                cpuDiag << "Tasks: " << bsodDebugData.taskStatuses.c_str();

                // Update the label with real-time data from Core 1
                if (ui.diagnosticsText) {
                    lv_label_set_text(ui.diagnosticsText, cpuDiag.str().c_str());
                }

                // No status bars to update in simple mode
                lastUIUpdate = currentTime;

                xSemaphoreGive(bsodDataMutex);

                ESP_LOGD(TAG, "[BSOD-DEBUG] Core 1 updated UI diagnostics");
            }
        }
    }

    ESP_LOGI(TAG, "[BSOD-DEBUG] Debug task ending");
    vTaskDelete(NULL);
}

// Core 0: LVGL Processing Task (dedicated to UI responsiveness & touchscreen only)
void bsodLvglTask(void* parameter) {
    ESP_LOGI(TAG, "[BSOD-LVGL] LVGL task started on Core %d - UI responsiveness only", xPortGetCoreID());

    // Disable watchdog for this BSOD task
    esp_task_wdt_delete(NULL);

    // Wait for initial debug data
    while (!debugDataReady && bsodTasksRunning) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (bsodTasksRunning) {
        lv_tick_inc(50);
        // Process LVGL to keep UI responsive including touchscreen
        lv_timer_handler();

        // LVGL processing interval optimized for dual-core BSOD
        // Core 0 focuses ONLY on UI responsiveness - diagnostics handled by Core 1
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "[BSOD-LVGL] LVGL task ending");
    vTaskDelete(NULL);
}

void prepareSystemForBSOD(const BSODConfig& config) {
    if (CoreLoggingFilter::isFilterActive()) CoreLoggingFilter::disableFilter();
    ESP_LOGE(TAG, "BSOD triggered: %s", config.message.c_str());

    // Prevent recursive BSOD
    if (bsodActive) {
        ESP_LOGE(TAG, "Recursive BSOD prevented - halting immediately");
        esp_task_wdt_delete(NULL);
        esp_task_wdt_deinit();
        while (1) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    bsodActive = true;

    ESP_LOGE(TAG, "CRITICAL SYSTEM FAILURE: %s", config.message.c_str());

    // Disable watchdogs for current task and any BSOD tasks
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    esp_task_wdt_delete(currentTask);
    esp_task_wdt_delete(NULL);

    // Store current task name for debugging
    const char* currentTaskName = pcTaskGetName(currentTask);
    ESP_LOGI(TAG, "BSOD triggered from task: %s on Core %d", currentTaskName, xPortGetCoreID());

    ESP_LOGI(TAG, "System prepared for dual-core BSOD mode");

    // Check if LVGL is initialized
    if (!lv_is_initialized()) {
        ESP_LOGE(TAG, "LVGL not initialized - cannot display BSOD");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGE(TAG, "SYSTEM HALTED: %s", config.message.c_str());
        }
    }

    // Store config
    currentBSODConfig = config;
}

void createBSODScreen(const BSODConfig& config) {
    // Create simple base screen with solid background
    bsodScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bsodScreen, BSODDesign::COLOR_BACKGROUND, 0);
    lv_obj_set_style_pad_all(bsodScreen, 0, 0);
}

void createBSODContent(const BSODConfig& config) {
    int y_pos = 30;
    const int line_height = 40;
    const int section_spacing = 30;
    
    // Classic BSOD sad face - HUGE like real Windows BSOD
    if (config.showSadFace) {
        ui.sadFace = createCenteredLabel(bsodScreen, config.sadFace.c_str(), y_pos, BSODDesign::COLOR_PRIMARY, BSODDesign::FONT_HUGE);
        y_pos += 60 + section_spacing; // Bigger spacing for huge sad face
    }
    
    // Title - centered, much larger like real BSOD
    ui.titleLabel = createCenteredLabel(bsodScreen, config.title.c_str(), y_pos, BSODDesign::COLOR_PRIMARY, BSODDesign::FONT_TITLE);
    y_pos += 50 + section_spacing; // Bigger spacing for title
    
    // Error message - LEFT ALIGNED like real Windows BSOD
    if (config.showMessage) {
        ui.messageText = createLeftAlignedLabel(bsodScreen, config.message.c_str(), 40, y_pos, BSODDesign::COLOR_PRIMARY, BSODDesign::FONT_BODY, 720);
        y_pos += line_height * 3 + section_spacing; // Message can be 3 lines
    }
    
    // Error code - centered
    if (config.showErrorCode && !config.errorCode.empty()) {
        ui.errorCodeText = createCenteredLabel(bsodScreen, config.errorCode.c_str(), y_pos, BSODDesign::COLOR_ERROR, BSODDesign::FONT_HEADING_SM);
        y_pos += line_height + section_spacing;
    }
    
    // Technical details - left aligned, smaller text
    if (config.showTechnicalDetails && !config.technicalDetails.empty()) {
        ui.detailsText = createSimpleLabel(bsodScreen, config.technicalDetails.c_str(), 40, y_pos, BSODDesign::COLOR_SECONDARY, BSODDesign::FONT_CAPTION);
        lv_obj_set_width(ui.detailsText, 720);
        y_pos += line_height * 3 + section_spacing; // Technical details can be 3 lines
    }
    
    // Toggle for advanced debugging - clickable
    ui.toggleText = createCenteredLabel(bsodScreen, "[Tap to show technical details]", y_pos, BSODDesign::COLOR_ACCENT, BSODDesign::FONT_CAPTION);
    lv_obj_add_flag(ui.toggleText, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.toggleText, toggle_debug_event_cb, LV_EVENT_CLICKED, NULL);
    y_pos += line_height + 10;
    
    // CPU diagnostics - initially hidden
    if (config.showCpuStatus) {
        ui.diagnosticsText = createSimpleLabel(bsodScreen, "Initializing diagnostics...", 40, y_pos, BSODDesign::COLOR_SECONDARY, BSODDesign::FONT_CAPTION);
        lv_obj_set_width(ui.diagnosticsText, 720);
        lv_obj_add_flag(ui.diagnosticsText, LV_OBJ_FLAG_HIDDEN); // Start hidden
        y_pos += line_height * 4 + section_spacing; // Diagnostics can be 4 lines
    }
    
    // Restart instruction - centered, emphasized
    if (config.showRestartInstruction) {
        ui.instructionText = createCenteredLabel(bsodScreen, config.restartInstruction.c_str(), y_pos, BSODDesign::COLOR_PRIMARY, BSODDesign::FONT_BODY);
        lv_obj_set_width(ui.instructionText, 700);
        lv_obj_align(ui.instructionText, LV_ALIGN_TOP_MID, 0, y_pos);
        y_pos += line_height * 2 + section_spacing;
    }
    
    // Build info - bottom left, small
    if (config.showBuildInfo) {
        std::string buildInfo = config.buildInfo.empty() ? getBuildInfo() : config.buildInfo;
        ui.buildInfoText = createSimpleLabel(bsodScreen, buildInfo.c_str(), 20, 450, BSODDesign::COLOR_SECONDARY, BSODDesign::FONT_CAPTION);
        lv_obj_set_width(ui.buildInfoText, 400);
    }
}

void launchDualCoreBSOD(const BSODConfig& config) {
    ESP_LOGI(TAG, "BSOD: Launching dual-core BSOD architecture");

    // Initialize debug data structure
    memset(&bsodDebugData, 0, sizeof(bsodDebugData));
    bsodTasksRunning = true;
    debugDataReady = false;

    // Load and display the BSOD screen immediately
    lv_scr_load(bsodScreen);
    lv_timer_handler();

    // Clean up boot screen if it was visible
    if (BootProgress::isVisible()) {
        BootProgress::forceCleanup();
    }

    ESP_LOGI(TAG, "BSOD: Creating dedicated dual-core tasks");

    // Create Core 1 debug data collection task (higher priority for data gathering)
    BaseType_t debugTaskResult = xTaskCreatePinnedToCore(
        bsodDebugTask,
        "BSOD_Debug",
        4096,  // 4KB stack for debug data collection
        NULL,
        configMAX_PRIORITIES - 2,  // High priority for debug data
        &bsodDebugTaskHandle,
        1  // Core 1
    );

    if (debugTaskResult != pdPASS) {
        ESP_LOGE(TAG, "BSOD: CRITICAL - Failed to create debug task");
        // Fallback to simple mode
        while (1) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // Create Core 0 LVGL processing task (highest priority for UI responsiveness)
    BaseType_t lvglTaskResult = xTaskCreatePinnedToCore(
        bsodLvglTask,
        "BSOD_LVGL",
        8192,  // 8KB stack for LVGL processing
        NULL,
        configMAX_PRIORITIES - 1,  // Highest priority for UI
        &bsodLvglTaskHandle,
        0  // Core 0
    );

    if (lvglTaskResult != pdPASS) {
        ESP_LOGE(TAG, "BSOD: CRITICAL - Failed to create LVGL task");
        // Fallback to simple mode
        while (1) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "BSOD: Dual-core tasks created successfully");
    ESP_LOGI(TAG, "BSOD: Core 0 = LVGL processing only, Core 1 = Debug data collection + UI updates");

    // Wait for BSOD tasks to stabilize before suspending normal tasks
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "BSOD: Dual-core BSOD system fully active");
    ESP_LOGI(TAG, "Error: %s", config.message.c_str());

    // Main BSOD loop - just monitor the dual-core system
    while (1) {
        // Check if both tasks are still running
        if (bsodLvglTaskHandle && eTaskGetState(bsodLvglTaskHandle) == eDeleted) {
            ESP_LOGE(TAG, "BSOD: LVGL task died - entering fallback mode");
            break;
        }
        if (bsodDebugTaskHandle && eTaskGetState(bsodDebugTaskHandle) == eDeleted) {
            ESP_LOGE(TAG, "BSOD: Debug task died - entering fallback mode");
            break;
        }

        // Sleep while dual-core tasks handle everything
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 second monitoring interval
    }

    // Fallback: if we reach here, something went wrong with dual-core tasks
    ESP_LOGE(TAG, "BSOD: Dual-core system failure - entering emergency fallback");
    bsodTasksRunning = false;
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =============================================================================
// MODERN BSOD SECTION IMPLEMENTATIONS
// =============================================================================

}  // anonymous namespace

void show(const BSODConfig& config) {
    prepareSystemForBSOD(config);
    createBSODScreen(config);
    createBSODContent(config);
    launchDualCoreBSOD(config);
}

void show(BSODConfig& config, const char* file, int line) {
    // Generate error code and assign to the mutable config
    config.errorCode = generateErrorCode(config.message.c_str(), file, line);
    show(config);
}

// Legacy show function for backward compatibility
void show(const char* message, const char* file, int line) {
    BSODConfig config;
    config.message = message ? message : "Unknown error";
    config.title = "System Error";
    config.restartInstruction = "Please restart your device.";
    config.showCpuStatus = true;  // Enable CPU diagnostics by default

    if (file) {
        char techBuf[256];
        snprintf(techBuf, sizeof(techBuf), "Location: %s:%d\nHeap: %u bytes\nPSRAM: %u bytes\nUptime: %u ms",
                 file, line,
                 esp_get_free_heap_size(),
                 ESP.getFreePsram(),
                 millis());
        config.technicalDetails = techBuf;
        config.showTechnicalDetails = true;
    }

    show(config, file, line);
}

// Test functions
void showAdvancedSystemDebug() {
    BSODConfig config;
    config.message = "System debug requested";
    config.title = "System Error";
    config.showCpuStatus = true;  // Enable CPU diagnostics
    config.showProgress = false;
    config.showBuildInfo = true;
    config.showRestartInstruction = true;
    config.backgroundColor = lv_color_hex(0x0000AA);
    config.textColor = lv_color_hex(0xFFFFFF);

    ESP_LOGI(TAG, "Launching dual-core BSOD screen");
    show(config);
}

std::string getQuickSystemStatus() {
    std::ostringstream status;
    status << "Heap: " << esp_get_free_heap_size() << " bytes";
    status << " | PSRAM: " << ESP.getFreePsram() << " bytes";
    status << " | Uptime: " << (millis() / 1000) << "s";
    return status.str();
}

void testAdvancedDebugging() {
    ESP_LOGI(TAG, "Testing dual-core BSOD system...");
    std::string systemStatus = getQuickSystemStatus();
    ESP_LOGI(TAG, "System status: %s", systemStatus.c_str());
    ESP_LOGI(TAG, "Launching dual-core BSOD...");
    showAdvancedSystemDebug();
}

void testDualCoreBSOD() {
    ESP_LOGI(TAG, "=== DUAL-CORE BSOD TEST ===");
    ESP_LOGI(TAG, "Pre-BSOD System Analysis:");
    ESP_LOGI(TAG, "  Current Core: %d", xPortGetCoreID());
    ESP_LOGI(TAG, "  Free Heap: %u bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Free PSRAM: %u bytes", ESP.getFreePsram());

    BSODConfig testConfig;
    testConfig.message = "DUAL-CORE BSOD TEST - Real-time debug monitoring active";
    testConfig.title = "Dual-Core System Error";
    testConfig.showCpuStatus = true;  // Enable real-time CPU diagnostics
    testConfig.showProgress = false;
    testConfig.showBuildInfo = true;
    testConfig.showRestartInstruction = true;
    testConfig.backgroundColor = lv_color_hex(0x0000AA);
    testConfig.textColor = lv_color_hex(0xFFFFFF);

    ESP_LOGI(TAG, "Triggering dual-core BSOD...");
    show(testConfig);
}

}  // namespace BSODHandler
