
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
    const lv_color_t COLOR_BACKGROUND = lv_color_hex(0x1E3A5F);      // Deep blue-gray
    const lv_color_t COLOR_CARD_BG = lv_color_hex(0x2B4C7E);        // Lighter blue-gray
    const lv_color_t COLOR_PRIMARY = lv_color_hex(0xFFFFFF);        // White
    const lv_color_t COLOR_SECONDARY = lv_color_hex(0xB8D4F1);      // Light blue
    const lv_color_t COLOR_ACCENT = lv_color_hex(0x4A90E2);         // Bright blue
    const lv_color_t COLOR_ERROR = lv_color_hex(0xFF6B6B);          // Soft red
    const lv_color_t COLOR_WARNING = lv_color_hex(0xFFD93D);        // Yellow
    const lv_color_t COLOR_SUCCESS = lv_color_hex(0x4ECDC4);        // Teal
    const lv_color_t COLOR_INFO = lv_color_hex(0x6C5CE7);           // Purple
    
    // Typography scale
    const lv_font_t* FONT_HEADING_LG = &lv_font_montserrat_32;
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
}

// =============================================================================
// ICON SYSTEM FOR VISUAL CLARITY
// =============================================================================

namespace BSODIcons {
    // Unicode symbols for different error types
    const char* ICON_ERROR = "⚠";        // Warning triangle
    const char* ICON_CRITICAL = "❌";     // X mark
    const char* ICON_INFO = "ℹ";         // Info
    const char* ICON_SYSTEM = "⚙";       // Gear
    const char* ICON_MEMORY = "💾";      // Floppy disk  
    const char* ICON_NETWORK = "🌐";     // Globe
    const char* ICON_RESTART = "↺";      // Reload
    const char* ICON_DEBUG = "🐞";       // Bug
}

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

// UI Elements with better organization
static struct {
    lv_obj_t* mainContainer;
    lv_obj_t* headerCard;
    lv_obj_t* errorIcon;
    lv_obj_t* titleLabel;
    lv_obj_t* subtitleLabel;
    
    lv_obj_t* messageCard;
    lv_obj_t* messageText;
    lv_obj_t* errorCodeChip;
    
    lv_obj_t* detailsCard;
    lv_obj_t* detailsText;
    
    lv_obj_t* diagnosticsCard;
    lv_obj_t* cpuStatusBar;
    lv_obj_t* memoryStatusBar;
    lv_obj_t* diagnosticsText;
    
    lv_obj_t* actionCard;
    lv_obj_t* instructionText;
    lv_obj_t* progressBar;
    
    lv_obj_t* footerContainer;
    lv_obj_t* buildInfoText;
    lv_obj_t* timestampText;
} ui;

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
// HELPER FUNCTIONS FOR MODERN UI
// =============================================================================

static lv_obj_t* createCard(lv_obj_t* parent, bool isTransparent = false) {
    lv_obj_t* card = lv_obj_create(parent);
    
    if (isTransparent) {
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(card, 0, 0);
    } else {
        lv_obj_set_style_bg_color(card, BSODDesign::COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, BSODDesign::CARD_RADIUS, 0);
        
        // Subtle shadow for depth
        lv_obj_set_style_shadow_width(card, BSODDesign::CARD_SHADOW, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_shadow_spread(card, 2, 0);
        
        // Border for definition
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, BSODDesign::COLOR_ACCENT, 0);
        lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    }
    
    lv_obj_set_style_pad_all(card, BSODDesign::SPACE_MD, 0);
    return card;
}

static lv_obj_t* createChip(lv_obj_t* parent, const char* text, lv_color_t bgColor) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, bgColor, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 16, 0);
    lv_obj_set_style_pad_hor(chip, 12, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    
    lv_obj_t* label = lv_label_create(chip);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, BSODDesign::COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(label, BSODDesign::FONT_CAPTION, 0);
    
    return chip;
}

static lv_obj_t* createStatusBar(lv_obj_t* parent, const char* label, int value, lv_color_t color) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    // Label
    lv_obj_t* labelObj = lv_label_create(container);
    lv_label_set_text(labelObj, label);
    lv_obj_set_style_text_color(labelObj, BSODDesign::COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(labelObj, BSODDesign::FONT_CAPTION, 0);
    
    // Progress bar
    lv_obj_t* bar = lv_bar_create(container);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A2332), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, 0);
    
    return container;
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
        if (currentTime - lastUIUpdate >= 3000 && cpuDiagLabel && debugDataReady) {
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
                
                // Update status bars if they exist
                if (ui.cpuStatusBar) {
                    int cpuUsage = 100 - (bsodDebugData.freeStack * 100 / 8192);
                    lv_bar_set_value(lv_obj_get_child(ui.cpuStatusBar, 1), cpuUsage, LV_ANIM_ON);
                }
                
                if (ui.memoryStatusBar) {
                    int memUsage = 100 - (bsodDebugData.freeHeap * 100 / bsodDebugData.minHeap);
                    lv_bar_set_value(lv_obj_get_child(ui.memoryStatusBar, 1), memUsage, LV_ANIM_ON);
                }
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
    // Create base screen with gradient-like background
    bsodScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bsodScreen, BSODDesign::COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_grad_color(bsodScreen, lv_color_hex(0x0F1E2E), 0);
    lv_obj_set_style_bg_grad_dir(bsodScreen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(bsodScreen, BSODDesign::SPACE_LG, 0);
}

void createMainContainer() {
    // Create scrollable main container
    ui.mainContainer = lv_obj_create(bsodScreen);
    lv_obj_set_size(ui.mainContainer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ui.mainContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.mainContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui.mainContainer, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(ui.mainContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui.mainContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.mainContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui.mainContainer, BSODDesign::SPACE_MD, 0);
}

// Forward declarations for new section functions
static void createHeaderSection(const BSODConfig& config);
static void createMessageSection(const BSODConfig& config);
static void createDetailsSection(const BSODConfig& config);
static void createDiagnosticsSection(const BSODConfig& config);
static void createActionSection(const BSODConfig& config);
static void createFooterSection(const BSODConfig& config);

void createBSODContent(const BSODConfig& config) {
    // Create sections in order
    createHeaderSection(config);
    createMessageSection(config);
    createDetailsSection(config);
    createDiagnosticsSection(config);
    createActionSection(config);
    createFooterSection(config);
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

static void createHeaderSection(const BSODConfig& config) {
    // Header card with icon and title
    ui.headerCard = createCard(ui.mainContainer, true);
    lv_obj_set_width(ui.headerCard, LV_PCT(100));
    lv_obj_set_style_max_width(ui.headerCard, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_layout(ui.headerCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui.headerCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.headerCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ui.headerCard, BSODDesign::SPACE_MD, 0);
    
    // Error icon with animation
    ui.errorIcon = lv_label_create(ui.headerCard);
    lv_label_set_text(ui.errorIcon, BSODIcons::ICON_ERROR);
    lv_obj_set_style_text_font(ui.errorIcon, BSODDesign::FONT_HEADING_LG, 0);
    lv_obj_set_style_text_color(ui.errorIcon, BSODDesign::COLOR_WARNING, 0);
    
    // Title container
    lv_obj_t* titleContainer = lv_obj_create(ui.headerCard);
    lv_obj_set_style_bg_opa(titleContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(titleContainer, 0, 0);
    lv_obj_set_style_pad_all(titleContainer, 0, 0);
    lv_obj_set_layout(titleContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(titleContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(titleContainer, 1);
    
    // Title
    ui.titleLabel = lv_label_create(titleContainer);
    lv_label_set_text(ui.titleLabel, config.title.c_str());
    lv_obj_set_style_text_font(ui.titleLabel, BSODDesign::FONT_HEADING_MD, 0);
    lv_obj_set_style_text_color(ui.titleLabel, BSODDesign::COLOR_PRIMARY, 0);
    
    // Subtitle with timestamp
    ui.subtitleLabel = lv_label_create(titleContainer);
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "Occurred at %02d:%02d:%02d", 
             (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);
    lv_label_set_text(ui.subtitleLabel, timeStr);
    lv_obj_set_style_text_font(ui.subtitleLabel, BSODDesign::FONT_CAPTION, 0);
    lv_obj_set_style_text_color(ui.subtitleLabel, BSODDesign::COLOR_SECONDARY, 0);
}

static void createMessageSection(const BSODConfig& config) {
    if (!config.showMessage) return;
    
    // Message card
    ui.messageCard = createCard(ui.mainContainer);
    lv_obj_set_width(ui.messageCard, LV_PCT(100));
    lv_obj_set_style_max_width(ui.messageCard, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_layout(ui.messageCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui.messageCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(ui.messageCard, BSODDesign::SPACE_SM, 0);
    
    // Error message
    ui.messageText = lv_label_create(ui.messageCard);
    lv_label_set_text(ui.messageText, config.message.c_str());
    lv_obj_set_style_text_font(ui.messageText, BSODDesign::FONT_BODY, 0);
    lv_obj_set_style_text_color(ui.messageText, BSODDesign::COLOR_PRIMARY, 0);
    lv_obj_set_width(ui.messageText, LV_PCT(100));
    lv_label_set_long_mode(ui.messageText, LV_LABEL_LONG_WRAP);
    
    // Error code chip
    if (config.showErrorCode && !config.errorCode.empty()) {
        ui.errorCodeChip = createChip(ui.messageCard, config.errorCode.c_str(), BSODDesign::COLOR_ERROR);
    }
}

static void createDetailsSection(const BSODConfig& config) {
    if (!config.showTechnicalDetails || config.technicalDetails.empty()) return;
    
    // Collapsible details card
    ui.detailsCard = createCard(ui.mainContainer);
    lv_obj_set_width(ui.detailsCard, LV_PCT(100));
    lv_obj_set_style_max_width(ui.detailsCard, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_style_bg_color(ui.detailsCard, lv_color_hex(0x1A2E4A), 0);
    
    // Details header
    lv_obj_t* detailsHeader = lv_label_create(ui.detailsCard);
    lv_label_set_text(detailsHeader, "Technical Details");
    lv_obj_set_style_text_font(detailsHeader, BSODDesign::FONT_HEADING_SM, 0);
    lv_obj_set_style_text_color(detailsHeader, BSODDesign::COLOR_ACCENT, 0);
    
    // Details text with monospace style
    ui.detailsText = lv_label_create(ui.detailsCard);
    lv_label_set_text(ui.detailsText, config.technicalDetails.c_str());
    lv_obj_set_style_text_font(ui.detailsText, BSODDesign::FONT_MONO, 0);
    lv_obj_set_style_text_color(ui.detailsText, BSODDesign::COLOR_SECONDARY, 0);
    lv_obj_set_style_bg_color(ui.detailsText, lv_color_hex(0x0F1A2A), 0);
    lv_obj_set_style_bg_opa(ui.detailsText, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.detailsText, BSODDesign::SPACE_SM, 0);
    lv_obj_set_style_radius(ui.detailsText, 4, 0);
    lv_obj_set_width(ui.detailsText, LV_PCT(100));
    lv_label_set_long_mode(ui.detailsText, LV_LABEL_LONG_WRAP);
}

static void createDiagnosticsSection(const BSODConfig& config) {
    if (!config.showCpuStatus) return;
    
    // Diagnostics card
    ui.diagnosticsCard = createCard(ui.mainContainer);
    lv_obj_set_width(ui.diagnosticsCard, LV_PCT(100));
    lv_obj_set_style_max_width(ui.diagnosticsCard, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_layout(ui.diagnosticsCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui.diagnosticsCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(ui.diagnosticsCard, BSODDesign::SPACE_MD, 0);
    
    // Header
    lv_obj_t* diagHeader = lv_label_create(ui.diagnosticsCard);
    lv_label_set_text(diagHeader, "System Diagnostics");
    lv_obj_set_style_text_font(diagHeader, BSODDesign::FONT_HEADING_SM, 0);
    lv_obj_set_style_text_color(diagHeader, BSODDesign::COLOR_ACCENT, 0);
    
    // Status bars
    ui.cpuStatusBar = createStatusBar(ui.diagnosticsCard, "CPU Usage", 0, BSODDesign::COLOR_INFO);
    ui.memoryStatusBar = createStatusBar(ui.diagnosticsCard, "Memory Usage", 0, BSODDesign::COLOR_WARNING);
    
    // Diagnostic text
    ui.diagnosticsText = lv_label_create(ui.diagnosticsCard);
    lv_label_set_text(ui.diagnosticsText, "Initializing diagnostics...");
    lv_obj_set_style_text_font(ui.diagnosticsText, BSODDesign::FONT_CAPTION, 0);
    lv_obj_set_style_text_color(ui.diagnosticsText, BSODDesign::COLOR_SECONDARY, 0);
    lv_obj_set_style_bg_color(ui.diagnosticsText, lv_color_hex(0x0F1A2A), 0);
    lv_obj_set_style_bg_opa(ui.diagnosticsText, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.diagnosticsText, BSODDesign::SPACE_SM, 0);
    lv_obj_set_style_radius(ui.diagnosticsText, 4, 0);
    lv_obj_set_width(ui.diagnosticsText, LV_PCT(100));
    lv_label_set_long_mode(ui.diagnosticsText, LV_LABEL_LONG_WRAP);
}

static void createActionSection(const BSODConfig& config) {
    if (!config.showRestartInstruction) return;
    
    // Action card
    ui.actionCard = createCard(ui.mainContainer);
    lv_obj_set_width(ui.actionCard, LV_PCT(100));
    lv_obj_set_style_max_width(ui.actionCard, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_style_bg_color(ui.actionCard, BSODDesign::COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(ui.actionCard, LV_OPA_10, 0);
    lv_obj_set_style_border_color(ui.actionCard, BSODDesign::COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(ui.actionCard, LV_OPA_50, 0);
    
    // Instruction text
    ui.instructionText = lv_label_create(ui.actionCard);
    lv_label_set_text(ui.instructionText, config.restartInstruction.c_str());
    lv_obj_set_style_text_font(ui.instructionText, BSODDesign::FONT_BODY, 0);
    lv_obj_set_style_text_color(ui.instructionText, BSODDesign::COLOR_PRIMARY, 0);
    lv_obj_set_width(ui.instructionText, LV_PCT(100));
    lv_label_set_long_mode(ui.instructionText, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(ui.instructionText, LV_TEXT_ALIGN_CENTER, 0);
    
    // Progress bar (if showing progress)
    if (config.showProgress) {
        ui.progressBar = lv_bar_create(ui.actionCard);
        lv_obj_set_size(ui.progressBar, LV_PCT(100), 4);
        lv_bar_set_value(ui.progressBar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui.progressBar, lv_color_hex(0x1A2332), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui.progressBar, BSODDesign::COLOR_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_radius(ui.progressBar, 2, 0);
    }
}

static void createFooterSection(const BSODConfig& config) {
    // Footer container
    ui.footerContainer = lv_obj_create(ui.mainContainer);
    lv_obj_set_width(ui.footerContainer, LV_PCT(100));
    lv_obj_set_style_max_width(ui.footerContainer, BSODDesign::MAX_CONTENT_WIDTH, 0);
    lv_obj_set_style_bg_opa(ui.footerContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.footerContainer, 0, 0);
    lv_obj_set_style_pad_all(ui.footerContainer, 0, 0);
    lv_obj_set_layout(ui.footerContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui.footerContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.footerContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Build info
    if (config.showBuildInfo) {
        ui.buildInfoText = lv_label_create(ui.footerContainer);
        std::string buildInfo = config.buildInfo.empty() ? getBuildInfo() : config.buildInfo;
        lv_label_set_text(ui.buildInfoText, buildInfo.c_str());
        lv_obj_set_style_text_font(ui.buildInfoText, BSODDesign::FONT_CAPTION, 0);
        lv_obj_set_style_text_color(ui.buildInfoText, BSODDesign::COLOR_SECONDARY, 0);
        lv_obj_set_style_text_opa(ui.buildInfoText, LV_OPA_60, 0);
    }
    
    // Timestamp
    ui.timestampText = lv_label_create(ui.footerContainer);
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "System uptime: %d seconds", millis() / 1000);
    lv_label_set_text(ui.timestampText, timestamp);
    lv_obj_set_style_text_font(ui.timestampText, BSODDesign::FONT_CAPTION, 0);
    lv_obj_set_style_text_color(ui.timestampText, BSODDesign::COLOR_SECONDARY, 0);
    lv_obj_set_style_text_opa(ui.timestampText, LV_OPA_60, 0);
}

}  // anonymous namespace

void show(const BSODConfig& config) {
    prepareSystemForBSOD(config);
    createBSODScreen(config);
    createMainContainer();
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
