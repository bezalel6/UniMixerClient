#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>

// =============================================================================
// MULTITHREADED OTA ARCHITECTURE
// =============================================================================

namespace MultiOTA {

// =============================================================================
// CONSTANTS AND CONFIGURATION
// =============================================================================

// Task Configuration
static const uint32_t OTA_UI_TASK_STACK_SIZE = 8192;
static const uint32_t OTA_NETWORK_TASK_STACK_SIZE = 12288;
static const uint32_t OTA_DOWNLOAD_TASK_STACK_SIZE = 8192;
static const uint32_t OTA_MONITOR_TASK_STACK_SIZE = 4096;

static const UBaseType_t OTA_UI_TASK_PRIORITY = 10;        // Highest - UI responsiveness
static const UBaseType_t OTA_NETWORK_TASK_PRIORITY = 8;    // High - Network operations
static const UBaseType_t OTA_DOWNLOAD_TASK_PRIORITY = 7;   // Medium - Download processing
static const UBaseType_t OTA_MONITOR_TASK_PRIORITY = 5;    // Low - Background monitoring

static const BaseType_t OTA_UI_TASK_CORE = 0;        // Core 0 - UI and display
static const BaseType_t OTA_NETWORK_TASK_CORE = 1;   // Core 1 - Network operations
static const BaseType_t OTA_DOWNLOAD_TASK_CORE = 1;  // Core 1 - Download processing
static const BaseType_t OTA_MONITOR_TASK_CORE = 1;   // Core 1 - Monitoring

// Queue Sizes
static const uint32_t OTA_COMMAND_QUEUE_SIZE = 10;
static const uint32_t OTA_DOWNLOAD_QUEUE_SIZE = 4;
static const uint32_t OTA_UI_UPDATE_QUEUE_SIZE = 20;

// Timing Configuration
static const uint32_t OTA_UI_UPDATE_INTERVAL_MS = 16;      // 60 FPS
static const uint32_t OTA_PROGRESS_UPDATE_INTERVAL_MS = 100; // 10 Hz progress
static const uint32_t OTA_INPUT_CHECK_INTERVAL_MS = 50;     // 20 Hz input
static const uint32_t OTA_WATCHDOG_FEED_INTERVAL_MS = 1000; // 1 Hz watchdog

// Download Configuration
static const uint32_t OTA_DOWNLOAD_CHUNK_SIZE = 2048;      // 2KB chunks
static const uint32_t OTA_DOWNLOAD_BUFFER_SIZE = 8192;     // 8KB buffer
static const uint32_t OTA_NETWORK_TIMEOUT_MS = 30000;      // 30 second timeout
static const uint32_t OTA_DOWNLOAD_TIMEOUT_MS = 300000;    // 5 minute timeout

// =============================================================================
// ENUMERATIONS
// =============================================================================

typedef enum {
    OTA_CMD_START,
    OTA_CMD_CANCEL,
    OTA_CMD_RETRY,
    OTA_CMD_REBOOT,
    OTA_CMD_EXIT,
    OTA_CMD_DOWNLOAD,
    OTA_CMD_INSTALL,
    OTA_CMD_CLEANUP
} OTACommandType_t;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_INITIALIZING,
    OTA_STATE_CONNECTING,
    OTA_STATE_CONNECTED,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_INSTALLING,
    OTA_STATE_VERIFYING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED,
    OTA_STATE_CANCELLED,
    OTA_STATE_CLEANUP
} OTAState_t;

typedef enum {
    OTA_ERROR_NONE,
    OTA_ERROR_WIFI_TIMEOUT,
    OTA_ERROR_SERVER_UNREACHABLE,
    OTA_ERROR_DOWNLOAD_FAILED,
    OTA_ERROR_FLASH_FAILED,
    OTA_ERROR_VERIFICATION_FAILED,
    OTA_ERROR_OUT_OF_MEMORY,
    OTA_ERROR_UNKNOWN
} OTAError_t;

typedef enum {
    OTA_UI_UPDATE_PROGRESS,
    OTA_UI_UPDATE_STATE,
    OTA_UI_UPDATE_ERROR,
    OTA_UI_UPDATE_LOG,
    OTA_UI_UPDATE_STATS
} OTAUIUpdateType_t;

// =============================================================================
// DATA STRUCTURES
// =============================================================================

typedef struct {
    OTACommandType_t type;
    uint32_t parameter;
    char data[64];
    uint32_t timestamp;
} OTACommand_t;

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t offset;
    uint32_t totalSize;
    uint8_t progress;
    char message[64];
} DownloadChunk_t;

typedef struct {
    uint8_t overallProgress;      // 0-100
    uint8_t networkProgress;      // WiFi connection sub-progress
    uint8_t downloadProgress;     // Download sub-progress  
    uint8_t installProgress;      // Installation sub-progress
    uint32_t downloadSpeed;       // Bytes per second
    uint32_t eta;                // Estimated time remaining (seconds)
    uint32_t bytesDownloaded;
    uint32_t totalBytes;
    OTAState_t state;
    OTAError_t lastError;
    char detailedMessage[128];
    uint32_t timestamp;
    bool canCancel;
    bool canRetry;
} DetailedProgress_t;

typedef struct {
    OTAUIUpdateType_t type;
    DetailedProgress_t progress;
    char logMessage[256];
    uint32_t timestamp;
} OTAUIUpdate_t;

typedef struct {
    uint32_t uiTaskHighWaterMark;
    uint32_t networkTaskHighWaterMark;
    uint32_t downloadTaskHighWaterMark;
    uint32_t monitorTaskHighWaterMark;
    uint32_t uiUpdateCount;
    uint32_t downloadChunkCount;
    uint32_t commandCount;
    uint32_t errorCount;
    float averageDownloadSpeed;
    uint32_t totalDownloadTime;
} OTAStats_t;

// =============================================================================
// GLOBAL HANDLES AND STATE
// =============================================================================

extern TaskHandle_t g_otaUITaskHandle;
extern TaskHandle_t g_otaNetworkTaskHandle;
extern TaskHandle_t g_otaDownloadTaskHandle;
extern TaskHandle_t g_otaMonitorTaskHandle;

extern QueueHandle_t g_otaCommandQueue;
extern QueueHandle_t g_otaDownloadQueue;
extern QueueHandle_t g_otaUIUpdateQueue;

extern SemaphoreHandle_t g_otaProgressMutex;
extern SemaphoreHandle_t g_otaStateMutex;
extern SemaphoreHandle_t g_otaStatsMutex;

extern DetailedProgress_t g_otaProgress;
extern OTAStats_t g_otaStats;
extern bool g_otaRunning;

// =============================================================================
// CORE API FUNCTIONS
// =============================================================================

// Initialization and Cleanup
bool init();
void deinit();
bool isRunning();

// OTA Control
bool startOTA();
bool cancelOTA();
bool retryOTA();
void exitOTA();

// Progress and Status
DetailedProgress_t getProgress();
OTAStats_t getStats();
const char* getStateString(OTAState_t state);
const char* getErrorString(OTAError_t error);

// =============================================================================
// TASK FUNCTIONS
// =============================================================================

// Core 0 Tasks
void otaUITask(void* parameter);
void otaInputMonitorTask(void* parameter);

// Core 1 Tasks  
void otaNetworkTask(void* parameter);
void otaDownloadTask(void* parameter);
void otaMonitorTask(void* parameter);

// =============================================================================
// INTERNAL FUNCTIONS
// =============================================================================

// Command System
bool sendOTACommand(OTACommandType_t type, uint32_t parameter = 0, const char* data = "");
bool receiveOTACommand(OTACommand_t* command, uint32_t timeoutMs = 1000);

// Progress Management
void updateProgressAtomic(const DetailedProgress_t* progress);
void updateProgressField(uint8_t overallProgress, const char* message);
void updateNetworkProgress(uint8_t progress, const char* message);
void updateDownloadProgress(uint8_t progress, uint32_t bytesDownloaded, uint32_t totalBytes);
void updateInstallProgress(uint8_t progress, const char* message);

// UI Updates
bool sendUIUpdate(OTAUIUpdateType_t type, const DetailedProgress_t* progress = nullptr, const char* logMessage = nullptr);
void processUIUpdates();

// State Management
void setState(OTAState_t newState);
OTAState_t getState();
void setError(OTAError_t error, const char* message);

// Network Operations
bool connectWiFi();
bool downloadFirmware();
bool installFirmware();
bool verifyFirmware();

// Utility Functions
void feedTaskWatchdog(const char* taskName);
uint32_t calculateDownloadSpeed(uint32_t bytesDownloaded, uint32_t timeMs);
uint32_t calculateETA(uint32_t bytesDownloaded, uint32_t totalBytes, uint32_t speed);
void addLogMessage(const char* message);

// Error Handling
void handleNetworkError(OTAError_t error);
void handleDownloadError(OTAError_t error);
void handleInstallError(OTAError_t error);
bool shouldRetryError(OTAError_t error);

// Statistics
void updateStats();
void resetStats();

} // namespace MultiOTA