#pragma once

#include <esp_log.h>
#include <string>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>
#include <esp_system.h>
#include <esp_task.h>

// Forward declarations removed - no longer using wrapper

// BSOD Configuration Structure
typedef struct {
    std::string title = "SYSTEM ERROR";
    std::string message = "Unknown error";
    std::string errorCode = "";
    std::string technicalDetails = "";
    std::string progressText = "System halted";
    std::string buildInfo = "";
    std::string restartInstruction = "Please restart your device";
    std::string sadFace = ":(";

    // Optional custom widgets for each section - now using direct LVGL
    lv_obj_t* customTitleWidget = nullptr;
    lv_obj_t* customMessageWidget = nullptr;
    lv_obj_t* customTechnicalWidget = nullptr;
    lv_obj_t* customProgressWidget = nullptr;
    lv_obj_t* customBuildInfoWidget = nullptr;
    lv_obj_t* customRestartWidget = nullptr;
    lv_obj_t* customSadFaceWidget = nullptr;

    // Section visibility flags
    bool showSadFace = true;
    bool showTitle = true;
    bool showErrorCode = true;
    bool showMessage = true;
    bool showTechnicalDetails = false;
    bool showProgress = true;
    bool showBuildInfo = true;
    bool showRestartInstruction = true;
    bool showCpuStatus = true;  // New CPU status section

    // Styling options (readable on dark blue background)
    lv_color_t backgroundColor = lv_color_hex(0x0078D7);  // Windows 10 blue
    lv_color_t textColor = lv_color_hex(0xFFFFFF);        // White
    lv_color_t errorCodeColor = lv_color_hex(0xFF4C4C);   // Bright red for errors
    lv_color_t cpuStatusColor = textColor;                // Light green for better contrast

    bool useGlassStyle = true;
    bool useShadow = true;
    int shadowWidth = 20;
    lv_color_t shadowColor = lv_color_hex(0x000000);
    int shadowOpacity = 60;
    int padding = 24;
} BSODConfig;

namespace BSODHandler {

// =============================================================================
// PREDEFINED BSOD TEMPLATES FOR COMMON ERROR TYPES
// =============================================================================

// Memory error template
inline BSODConfig createMemoryErrorConfig(const char* details = nullptr) {
    BSODConfig config;
    config.title = "OUT OF MEMORY";
    config.message = "The system has run out of available memory and cannot continue.";
    config.errorCode = "ERR_NO_MEMORY";
    config.showCpuStatus = true;
    config.showTechnicalDetails = true;
    config.technicalDetails = details ? details : "Heap exhausted. Consider reducing memory usage.";
    config.restartInstruction = "Please restart the device to free up memory.";
    config.backgroundColor = lv_color_hex(0x8B0000);  // Dark red
    return config;
}

// Network error template
inline BSODConfig createNetworkErrorConfig(const char* details = nullptr) {
    BSODConfig config;
    config.title = "NETWORK FAILURE";
    config.message = "Critical network communication error occurred.";
    config.errorCode = "ERR_NETWORK";
    config.showCpuStatus = false;
    config.showTechnicalDetails = true;
    config.technicalDetails = details ? details : "Network stack failure or connectivity lost.";
    config.restartInstruction = "Check network connection and restart device.";
    return config;
}

// Hardware error template
inline BSODConfig createHardwareErrorConfig(const char* component, const char* details = nullptr) {
    BSODConfig config;
    config.title = "HARDWARE FAILURE";
    config.message = component ? ("Critical hardware failure: " + std::string(component)) : "Critical hardware component failure detected.";
    config.errorCode = "ERR_HARDWARE";
    config.showCpuStatus = true;
    config.showTechnicalDetails = true;
    config.technicalDetails = details ? details : "Hardware component not responding.";
    config.restartInstruction = "Power cycle the device. If problem persists, contact support.";
    return config;
}

// Initialization error template
inline BSODConfig createInitErrorConfig(const char* component, const char* details = nullptr) {
    BSODConfig config;
    config.title = "INITIALIZATION FAILED";
    config.message = component ? ("Failed to initialize: " + std::string(component)) : "System initialization failed.";
    config.errorCode = "ERR_INIT";
    config.showCpuStatus = false;
    config.showTechnicalDetails = true;
    config.technicalDetails = details ? details : "Component failed to start properly.";
    config.restartInstruction = "Restart device. Check SD card and connections.";
    return config;
}

// Assertion failure template
inline BSODConfig createAssertionErrorConfig(const char* condition, const char* file, int line) {
    BSODConfig config;
    config.title = "ASSERTION FAILED";
    config.message = "A critical assertion check failed.";
    config.errorCode = "ERR_ASSERT";
    config.showCpuStatus = true;
    config.showTechnicalDetails = true;
    
    char techBuf[256];
    snprintf(techBuf, sizeof(techBuf), "Assertion: %s\nLocation: %s:%d", 
             condition ? condition : "Unknown", file ? file : "Unknown", line);
    config.technicalDetails = techBuf;
    
    config.restartInstruction = "This is a software bug. Please report to developers.";
    return config;
}

// Task failure template
inline BSODConfig createTaskErrorConfig(const char* taskName, const char* details = nullptr) {
    BSODConfig config;
    config.title = "TASK FAILURE";
    config.message = taskName ? ("Critical task failed: " + std::string(taskName)) : "A critical system task has failed.";
    config.errorCode = "ERR_TASK";
    config.showCpuStatus = true;
    config.showTechnicalDetails = true;
    config.technicalDetails = details ? details : "Task terminated unexpectedly.";
    config.restartInstruction = "System will restart automatically...";
    config.showProgress = true;
    return config;
}
// ===================================================================
// DUAL-CORE BSOD ARCHITECTURE
// ===================================================================
// When BSOD is triggered:
// 1. All normal tasks are suspended
// 2. Core 0: Dedicated BSOD LVGL task (UI responsiveness & touchscreen only)
// 3. Core 1: Dedicated BSOD Debug task (data collection & diagnostics UI updates)
// 4. Clean separation of concerns prevents conflicts
// ===================================================================

// Core function to display BSOD screen with configuration
void show(const BSODConfig& config);

// file and line might still be provided
void show(BSODConfig& config, const char* file, int line = 0);

// Legacy function for backward compatibility
void show(const char* message, const char* file = nullptr, int line = 0);

// Initialize BSOD capability (called early in boot)
bool init();

// Check if BSOD system is ready
bool isReady();

// Check if BSOD is currently active (screen displayed)
bool isActive();

// ===================================================================
// ADVANCED DEBUGGING FUNCTIONS (DUAL-CORE ARCHITECTURE)
// ===================================================================

// Advanced system debugging with dual-core real-time updates
void showAdvancedSystemDebug();

// Get quick system status as string (non-blocking)
std::string getQuickSystemStatus();

// Test function to demonstrate the new dual-core debugging capabilities
void testAdvancedDebugging();

// Trigger dual-core BSOD mode for testing (safe test mode)
void testDualCoreBSOD();

// Private helper functions for BSOD display
namespace {
void prepareSystemForBSOD(const BSODConfig& config);
void createBSODScreen(const BSODConfig& config);
void createBSODContainer(const BSODConfig& config);
void createSadFaceSection(const BSODConfig& config);
void createTitleSection(const BSODConfig& config);
void createErrorCodeSection(const BSODConfig& config);
void createMessageSection(const BSODConfig& config);
void createTechnicalDetailsSection(const BSODConfig& config);
void createProgressSection(const BSODConfig& config);
void createBuildInfoSection(const BSODConfig& config);
void createRestartSection(const BSODConfig& config);
void createCpuStatusSection(const BSODConfig& config);
void displayBSODLoop(const BSODConfig& config);

// Helper function to convert task state to string
const char* taskStateToString(eTaskState state);
}  // namespace
}  // namespace BSODHandler

// Generic critical failure macros with modern templates
#define CRITICAL_FAILURE(message)                           \
    do {                                                    \
        ESP_LOGE("CRITICAL", "Critical failure triggered"); \
        BSODConfig config;                                  \
        config.title = "CRITICAL FAILURE";                  \
        config.message = message;                           \
        BSODHandler::show(config, __FILE__, __LINE__);      \
    } while (0)

#define ASSERT_CRITICAL(condition, message)                                     \
    do {                                                                        \
        if (!(condition)) {                                                     \
            ESP_LOGE("CRITICAL", "Assertion failed: %s", #condition);           \
            auto config = BSODHandler::createAssertionErrorConfig(#condition, __FILE__, __LINE__); \
            config.message = message;                                           \
            BSODHandler::show(config);                                          \
        }                                                                       \
    } while (0)

#define INIT_CRITICAL(expr, failure_msg)                                    \
    do {                                                                    \
        ESP_LOGI("BOOT", "Critical init: %s", #expr);                       \
        if (!(expr)) {                                                      \
            ESP_LOGE("CRITICAL", "Init failed: %s", #expr);                 \
            auto config = BSODHandler::createInitErrorConfig(#expr, failure_msg); \
            BSODHandler::show(config, __FILE__, __LINE__);                  \
        }                                                                   \
    } while (0)

// Specialized failure macros for common scenarios
#define MEMORY_CRITICAL(details)                                            \
    do {                                                                    \
        ESP_LOGE("CRITICAL", "Memory exhausted");                           \
        auto config = BSODHandler::createMemoryErrorConfig(details);        \
        BSODHandler::show(config, __FILE__, __LINE__);                      \
    } while (0)

#define HARDWARE_CRITICAL(component, details)                               \
    do {                                                                    \
        ESP_LOGE("CRITICAL", "Hardware failure: %s", component);            \
        auto config = BSODHandler::createHardwareErrorConfig(component, details); \
        BSODHandler::show(config, __FILE__, __LINE__);                      \
    } while (0)

#define TASK_CRITICAL(taskName, details)                                    \
    do {                                                                    \
        ESP_LOGE("CRITICAL", "Task failure: %s", taskName);                 \
        auto config = BSODHandler::createTaskErrorConfig(taskName, details); \
        BSODHandler::show(config, __FILE__, __LINE__);                      \
    } while (0)

#define INIT_OPTIONAL(expr, component_name)                                                       \
    do {                                                                                          \
        ESP_LOGI("BOOT", "Optional init: %s", component_name);                                    \
        if (!(expr)) {                                                                            \
            ESP_LOGW("BOOT", "%s initialization failed - continuing without it", component_name); \
        } else {                                                                                  \
            ESP_LOGI("BOOT", "%s initialized successfully", component_name);                      \
        }                                                                                         \
    } while (0)

// DUAL-CORE BSOD TEST MACRO
#define TEST_DUAL_CORE_BSOD()                                         \
    do {                                                              \
        ESP_LOGI("BSOD_TEST", "Testing dual-core BSOD architecture"); \
        BSODHandler::testDualCoreBSOD();                              \
    } while (0)
