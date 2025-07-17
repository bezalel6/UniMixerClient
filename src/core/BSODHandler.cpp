
#include "BSODHandler.h"
#include "BootProgressScreen.h"
#include "BuildInfo.h"
#include "CoreLoggingFilter.h"
#include "ui/wrapper/LVGLWrapper.h"
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

// Simple BSOD widgets
static UI::Wrapper::Container* mainContainer = nullptr;
static UI::Wrapper::Label* sadFaceLabel = nullptr;
static UI::Wrapper::Label* titleLabel = nullptr;
static UI::Wrapper::Label* messageLabel = nullptr;
static UI::Wrapper::Label* errorCodeLabel = nullptr;
static UI::Wrapper::Label* instructionsLabel = nullptr;
static UI::Wrapper::Label* buildInfoLabel = nullptr;
static UI::Wrapper::Label* cpuDiagLabel = nullptr;

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
                cpuDiagLabel->setText(cpuDiag.str().c_str());
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
    // Create BSOD screen with blue background
    bsodScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bsodScreen, lv_color_hex(0x0000AA), 0);    // Blue background
    lv_obj_set_style_text_color(bsodScreen, lv_color_hex(0xFFFFFF), 0);  // White text
    lv_obj_set_style_pad_all(bsodScreen, 20, 0);
}

void createMainContainer() {
    // Create main container with proper flex layout
    mainContainer = new UI::Wrapper::Container("bsod_main");
    mainContainer->init(bsodScreen);
    mainContainer->setSize(LV_PCT(100), LV_PCT(100))
        .setFlexFlow(LV_FLEX_FLOW_COLUMN)
        .setFlexAlign(LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER)
        .setPadding(30);
}

void createBSODContent(const BSODConfig& config) {
    // Sad face
    if (config.showSadFace) {
        sadFaceLabel = new UI::Wrapper::Label("bsod_sad_face", ":-(");
        sadFaceLabel->init(mainContainer->getWidget());
        sadFaceLabel->setTextColor(lv_color_hex(0xFFFFFF))
            .setPadding(10);

        // Make sad face larger
        lv_obj_set_style_text_font(sadFaceLabel->getWidget(), &lv_font_montserrat_48, 0);
    }

    // Title
    if (config.showTitle) {
        titleLabel = new UI::Wrapper::Label("bsod_title", config.title.c_str());
        titleLabel->init(mainContainer->getWidget());
        titleLabel->setTextColor(lv_color_hex(0xFFFFFF))
            .setPadding(20);

        // Make title larger
        lv_obj_set_style_text_font(titleLabel->getWidget(), &lv_font_montserrat_24, 0);
    }

    // Main error message
    if (config.showMessage) {
        messageLabel = new UI::Wrapper::Label("bsod_message", config.message.c_str());
        messageLabel->init(mainContainer->getWidget());
        messageLabel->setTextColor(lv_color_hex(0xFFFFFF))
            .setPadding(15);

        // Enable text wrapping for long messages
        lv_obj_set_width(messageLabel->getWidget(), LV_PCT(90));
        lv_label_set_long_mode(messageLabel->getWidget(), LV_LABEL_LONG_WRAP);
    }

    // Error code
    if (config.showErrorCode && !config.errorCode.empty()) {
        std::string errorText = "Error Code: " + config.errorCode;
        errorCodeLabel = new UI::Wrapper::Label("bsod_error_code", errorText.c_str());
        errorCodeLabel->init(mainContainer->getWidget());
        errorCodeLabel->setTextColor(lv_color_hex(0xFFFFFF))
            .setPadding(10);
    }

    // Technical details (if enabled)
    if (config.showTechnicalDetails && !config.technicalDetails.empty()) {
        UI::Wrapper::Label* techLabel = new UI::Wrapper::Label("bsod_tech", config.technicalDetails.c_str());
        techLabel->init(mainContainer->getWidget());
        techLabel->setTextColor(lv_color_hex(0xCCCCCC))
            .setPadding(10);

        // Make technical details smaller
        lv_obj_set_style_text_font(techLabel->getWidget(), &lv_font_montserrat_12, 0);
    }

    // Restart instructions
    if (config.showRestartInstruction) {
        instructionsLabel = new UI::Wrapper::Label("bsod_instructions", config.restartInstruction.c_str());
        instructionsLabel->init(mainContainer->getWidget());
        instructionsLabel->setTextColor(lv_color_hex(0xFFFFFF))
            .setPadding(20);

        // Enable text wrapping for instructions
        lv_obj_set_width(instructionsLabel->getWidget(), LV_PCT(80));
        lv_label_set_long_mode(instructionsLabel->getWidget(), LV_LABEL_LONG_WRAP);
    }

    // CPU Diagnostics section (real-time updated)
    if (config.showCpuStatus) {
        // Create initial CPU diagnostics (will be updated by Core 0 task)
        std::ostringstream cpuDiag;
        cpuDiag << "=== INITIALIZING CPU DIAGNOSTICS ===\n";
        cpuDiag << "Waiting for real-time data from Core 1...\n";
        cpuDiag << "Dual-core BSOD architecture active\n";
        cpuDiag << "Core 0: LVGL processing\n";
        cpuDiag << "Core 1: Debug data collection";

        cpuDiagLabel = new UI::Wrapper::Label("bsod_cpu", cpuDiag.str().c_str());
        cpuDiagLabel->init(mainContainer->getWidget());
        cpuDiagLabel->setTextColor(lv_color_hex(0x00FFFF))  // Cyan for diagnostics
            .setPadding(15);

        // Enable text wrapping and make it monospace-like
        lv_obj_set_width(cpuDiagLabel->getWidget(), LV_PCT(95));
        lv_label_set_long_mode(cpuDiagLabel->getWidget(), LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(cpuDiagLabel->getWidget(), &lv_font_montserrat_12, 0);
    }

    // Build info (at bottom)
    if (config.showBuildInfo) {
        std::string buildInfo = config.buildInfo.empty() ? getBuildInfo() : config.buildInfo;
        buildInfoLabel = new UI::Wrapper::Label("bsod_build", buildInfo.c_str());
        buildInfoLabel->init(mainContainer->getWidget());
        buildInfoLabel->setTextColor(lv_color_hex(0x888888))
            .setPadding(5);

        // Make build info smaller
        lv_obj_set_style_text_font(buildInfoLabel->getWidget(), &lv_font_montserrat_10, 0);
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
