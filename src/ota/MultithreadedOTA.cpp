#include "MultithreadedOTA.h"
#include "OTAConfig.h"
#include "BootManager.h"
#include "../application/ui/LVGLMessageHandler.h"
#include "../core/TaskManager.h"
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char* TAG = "MultiOTA";

namespace MultiOTA {

// =============================================================================
// GLOBAL VARIABLES AND STATE
// =============================================================================

// Task Handles
TaskHandle_t g_otaUITaskHandle = NULL;
TaskHandle_t g_otaNetworkTaskHandle = NULL;
TaskHandle_t g_otaDownloadTaskHandle = NULL;
TaskHandle_t g_otaMonitorTaskHandle = NULL;

// Communication Queues
QueueHandle_t g_otaCommandQueue = NULL;
QueueHandle_t g_otaDownloadQueue = NULL;
QueueHandle_t g_otaUIUpdateQueue = NULL;

// Synchronization Objects
SemaphoreHandle_t g_otaProgressMutex = NULL;
SemaphoreHandle_t g_otaStateMutex = NULL;
SemaphoreHandle_t g_otaStatsMutex = NULL;

// Shared State
DetailedProgress_t g_otaProgress = {0};
OTAStats_t g_otaStats = {0};
bool g_otaRunning = false;

// Internal State
static esp_ota_handle_t s_otaHandle = 0;
static const esp_partition_t* s_updatePartition = NULL;
static uint32_t s_downloadStartTime = 0;
static uint32_t s_lastProgressUpdate = 0;
static uint8_t* s_downloadBuffer = NULL;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

const char* getStateString(OTAState_t state) {
    switch (state) {
        case OTA_STATE_IDLE: return "IDLE";
        case OTA_STATE_INITIALIZING: return "INITIALIZING";
        case OTA_STATE_CONNECTING: return "CONNECTING";
        case OTA_STATE_CONNECTED: return "CONNECTED";
        case OTA_STATE_DOWNLOADING: return "DOWNLOADING";
        case OTA_STATE_INSTALLING: return "INSTALLING";
        case OTA_STATE_VERIFYING: return "VERIFYING";
        case OTA_STATE_SUCCESS: return "SUCCESS";
        case OTA_STATE_FAILED: return "FAILED";
        case OTA_STATE_CANCELLED: return "CANCELLED";
        case OTA_STATE_CLEANUP: return "CLEANUP";
        default: return "UNKNOWN";
    }
}

const char* getErrorString(OTAError_t error) {
    switch (error) {
        case OTA_ERROR_NONE: return "No Error";
        case OTA_ERROR_WIFI_TIMEOUT: return "WiFi Connection Timeout";
        case OTA_ERROR_SERVER_UNREACHABLE: return "Server Unreachable";
        case OTA_ERROR_DOWNLOAD_FAILED: return "Download Failed";
        case OTA_ERROR_FLASH_FAILED: return "Flash Write Failed";
        case OTA_ERROR_VERIFICATION_FAILED: return "Verification Failed";
        case OTA_ERROR_OUT_OF_MEMORY: return "Out of Memory";
        case OTA_ERROR_UNKNOWN: return "Unknown Error";
        default: return "Invalid Error";
    }
}

void feedTaskWatchdog(const char* taskName) {
    static uint32_t lastFeed = 0;
    uint32_t now = millis();
    
    if (now - lastFeed >= OTA_WATCHDOG_FEED_INTERVAL_MS) {
        esp_task_wdt_reset();
        ESP_LOGV(TAG, "Watchdog fed by %s", taskName);
        lastFeed = now;
    }
}

uint32_t calculateDownloadSpeed(uint32_t bytesDownloaded, uint32_t timeMs) {
    if (timeMs == 0) return 0;
    return (bytesDownloaded * 1000) / timeMs; // bytes per second
}

uint32_t calculateETA(uint32_t bytesDownloaded, uint32_t totalBytes, uint32_t speed) {
    if (speed == 0 || bytesDownloaded >= totalBytes) return 0;
    uint32_t remainingBytes = totalBytes - bytesDownloaded;
    return remainingBytes / speed; // seconds
}

// =============================================================================
// PROGRESS AND STATE MANAGEMENT
// =============================================================================

void updateProgressAtomic(const DetailedProgress_t* progress) {
    if (!progress) return;
    
    if (xSemaphoreTake(g_otaProgressMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&g_otaProgress, progress, sizeof(DetailedProgress_t));
        g_otaProgress.timestamp = millis();
        xSemaphoreGive(g_otaProgressMutex);
        
        // Send UI update
        sendUIUpdate(OTA_UI_UPDATE_PROGRESS, progress);
    }
}

void updateProgressField(uint8_t overallProgress, const char* message) {
    DetailedProgress_t progress = g_otaProgress;
    progress.overallProgress = overallProgress;
    if (message) {
        strncpy(progress.detailedMessage, message, sizeof(progress.detailedMessage) - 1);
        progress.detailedMessage[sizeof(progress.detailedMessage) - 1] = '\0';
    }
    updateProgressAtomic(&progress);
}

void updateNetworkProgress(uint8_t progress, const char* message) {
    DetailedProgress_t currentProgress = g_otaProgress;
    currentProgress.networkProgress = progress;
    if (message) {
        strncpy(currentProgress.detailedMessage, message, sizeof(currentProgress.detailedMessage) - 1);
        currentProgress.detailedMessage[sizeof(currentProgress.detailedMessage) - 1] = '\0';
    }
    updateProgressAtomic(&currentProgress);
}

void updateDownloadProgress(uint8_t progress, uint32_t bytesDownloaded, uint32_t totalBytes) {
    DetailedProgress_t currentProgress = g_otaProgress;
    currentProgress.downloadProgress = progress;
    currentProgress.bytesDownloaded = bytesDownloaded;
    currentProgress.totalBytes = totalBytes;
    
    uint32_t elapsedTime = millis() - s_downloadStartTime;
    currentProgress.downloadSpeed = calculateDownloadSpeed(bytesDownloaded, elapsedTime);
    currentProgress.eta = calculateETA(bytesDownloaded, totalBytes, currentProgress.downloadSpeed);
    
    char speedStr[32];
    if (currentProgress.downloadSpeed > 1024) {
        snprintf(speedStr, sizeof(speedStr), "%.1f KB/s", currentProgress.downloadSpeed / 1024.0f);
    } else {
        snprintf(speedStr, sizeof(speedStr), "%lu B/s", currentProgress.downloadSpeed);
    }
    
    snprintf(currentProgress.detailedMessage, sizeof(currentProgress.detailedMessage),
             "Downloaded %lu/%lu bytes (%s)", bytesDownloaded, totalBytes, speedStr);
    
    updateProgressAtomic(&currentProgress);
}

void setState(OTAState_t newState) {
    if (xSemaphoreTake(g_otaStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        OTAState_t oldState = g_otaProgress.state;
        g_otaProgress.state = newState;
        
        // Update control flags based on state
        switch (newState) {
            case OTA_STATE_CONNECTING:
            case OTA_STATE_DOWNLOADING:
                g_otaProgress.canCancel = true;
                g_otaProgress.canRetry = false;
                break;
            case OTA_STATE_INSTALLING:
            case OTA_STATE_VERIFYING:
                g_otaProgress.canCancel = false;
                g_otaProgress.canRetry = false;
                break;
            case OTA_STATE_FAILED:
                g_otaProgress.canCancel = true;
                g_otaProgress.canRetry = true;
                break;
            case OTA_STATE_SUCCESS:
            case OTA_STATE_CANCELLED:
                g_otaProgress.canCancel = false;
                g_otaProgress.canRetry = false;
                break;
            default:
                g_otaProgress.canCancel = true;
                g_otaProgress.canRetry = false;
                break;
        }
        
        xSemaphoreGive(g_otaStateMutex);
        
        ESP_LOGI(TAG, "State transition: %s -> %s", getStateString(oldState), getStateString(newState));
        sendUIUpdate(OTA_UI_UPDATE_STATE);
    }
}

OTAState_t getState() {
    OTAState_t state = OTA_STATE_IDLE;
    if (xSemaphoreTake(g_otaStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        state = g_otaProgress.state;
        xSemaphoreGive(g_otaStateMutex);
    }
    return state;
}

void setError(OTAError_t error, const char* message) {
    g_otaProgress.lastError = error;
    if (message) {
        strncpy(g_otaProgress.detailedMessage, message, sizeof(g_otaProgress.detailedMessage) - 1);
        g_otaProgress.detailedMessage[sizeof(g_otaProgress.detailedMessage) - 1] = '\0';
    }
    setState(OTA_STATE_FAILED);
    sendUIUpdate(OTA_UI_UPDATE_ERROR);
    
    if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_otaStats.errorCount++;
        xSemaphoreGive(g_otaStatsMutex);
    }
}

// =============================================================================
// COMMAND SYSTEM
// =============================================================================

bool sendOTACommand(OTACommandType_t type, uint32_t parameter, const char* data) {
    if (!g_otaCommandQueue) return false;
    
    OTACommand_t command = {0};
    command.type = type;
    command.parameter = parameter;
    command.timestamp = millis();
    
    if (data) {
        strncpy(command.data, data, sizeof(command.data) - 1);
        command.data[sizeof(command.data) - 1] = '\0';
    }
    
    BaseType_t result = xQueueSend(g_otaCommandQueue, &command, pdMS_TO_TICKS(100));
    if (result == pdTRUE) {
        if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_otaStats.commandCount++;
            xSemaphoreGive(g_otaStatsMutex);
        }
        ESP_LOGD(TAG, "Command sent: type=%d, param=%lu", type, parameter);
        return true;
    }
    
    ESP_LOGW(TAG, "Failed to send command: type=%d", type);
    return false;
}

bool receiveOTACommand(OTACommand_t* command, uint32_t timeoutMs) {
    if (!g_otaCommandQueue || !command) return false;
    
    BaseType_t result = xQueueReceive(g_otaCommandQueue, command, pdMS_TO_TICKS(timeoutMs));
    if (result == pdTRUE) {
        ESP_LOGD(TAG, "Command received: type=%d, param=%lu", command->type, command->parameter);
        return true;
    }
    
    return false;
}

// =============================================================================
// UI UPDATE SYSTEM
// =============================================================================

bool sendUIUpdate(OTAUIUpdateType_t type, const DetailedProgress_t* progress, const char* logMessage) {
    if (!g_otaUIUpdateQueue) return false;
    
    OTAUIUpdate_t update = {0};
    update.type = type;
    update.timestamp = millis();
    
    if (progress) {
        memcpy(&update.progress, progress, sizeof(DetailedProgress_t));
    } else {
        update.progress = g_otaProgress;
    }
    
    if (logMessage) {
        strncpy(update.logMessage, logMessage, sizeof(update.logMessage) - 1);
        update.logMessage[sizeof(update.logMessage) - 1] = '\0';
    }
    
    BaseType_t result = xQueueSend(g_otaUIUpdateQueue, &update, 0); // Non-blocking
    if (result == pdTRUE) {
        if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_otaStats.uiUpdateCount++;
            xSemaphoreGive(g_otaStatsMutex);
        }
        return true;
    }
    
    return false;
}

void addLogMessage(const char* message) {
    if (message) {
        ESP_LOGI(TAG, "[LOG] %s", message);
        sendUIUpdate(OTA_UI_UPDATE_LOG, nullptr, message);
    }
}

// =============================================================================
// NETWORK OPERATIONS
// =============================================================================

bool connectWiFi() {
    ESP_LOGI(TAG, "Starting WiFi connection...");
    updateNetworkProgress(5, "Initializing WiFi adapter...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
    
    uint32_t startTime = millis();
    uint8_t progress = 5;
    
    while (WiFi.status() != WL_CONNECTED && g_otaRunning) {
        // Check for cancellation
        if (getState() == OTA_STATE_CANCELLED) {
            WiFi.disconnect();
            return false;
        }
        
        // Update progress every 2 seconds
        uint32_t elapsed = millis() - startTime;
        if (elapsed >= 2000) {
            progress = min(progress + 3, 25);
            
            wl_status_t status = WiFi.status();
            const char* statusMsg = "Connecting to WiFi...";
            
            switch (status) {
                case WL_IDLE_STATUS:
                    statusMsg = "WiFi initializing...";
                    break;
                case WL_NO_SSID_AVAIL:
                    statusMsg = "WiFi network not found";
                    setError(OTA_ERROR_WIFI_TIMEOUT, "Network not found - check SSID");
                    return false;
                case WL_SCAN_COMPLETED:
                    statusMsg = "Network scan complete";
                    break;
                case WL_CONNECT_FAILED:
                    statusMsg = "WiFi connection failed";
                    setError(OTA_ERROR_WIFI_TIMEOUT, "Connection failed - check password");
                    return false;
                case WL_CONNECTION_LOST:
                    statusMsg = "WiFi connection lost";
                    break;
                case WL_DISCONNECTED:
                    statusMsg = "WiFi disconnected";
                    break;
                default:
                    statusMsg = "Establishing WiFi connection...";
                    break;
            }
            
            updateNetworkProgress(progress, statusMsg);
            startTime = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Timeout after 30 seconds
        if (millis() - startTime > OTA_NETWORK_TIMEOUT_MS) {
            setError(OTA_ERROR_WIFI_TIMEOUT, "WiFi connection timeout");
            return false;
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        char ipMsg[128];
        snprintf(ipMsg, sizeof(ipMsg), "WiFi connected - IP: %s", WiFi.localIP().toString().c_str());
        updateNetworkProgress(30, ipMsg);
        addLogMessage(ipMsg);
        return true;
    }
    
    return false;
}

bool downloadFirmware() {
    ESP_LOGI(TAG, "Starting firmware download...");
    updateProgressField(30, "Connecting to firmware server...");
    
    HTTPClient http;
    http.setTimeout(OTA_NETWORK_TIMEOUT_MS / 1000);
    http.begin(OTA_SERVER_URL);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %d", httpCode);
        http.end();
        setError(OTA_ERROR_SERVER_UNREACHABLE, "Failed to connect to update server");
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", contentLength);
        http.end();
        setError(OTA_ERROR_DOWNLOAD_FAILED, "Invalid firmware file");
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    
    char sizeMsg[128];
    snprintf(sizeMsg, sizeof(sizeMsg), "Downloading firmware: %d bytes", contentLength);
    updateProgressField(35, sizeMsg);
    addLogMessage(sizeMsg);
    
    s_downloadStartTime = millis();
    uint32_t downloaded = 0;
    
    while (downloaded < contentLength && g_otaRunning) {
        // Check for cancellation
        if (getState() == OTA_STATE_CANCELLED) {
            http.end();
            return false;
        }
        
        // Read chunk
        uint32_t remainingBytes = contentLength - downloaded;
        uint32_t chunkSize = min(remainingBytes, OTA_DOWNLOAD_CHUNK_SIZE);
        
        int bytesRead = stream->readBytes(s_downloadBuffer, chunkSize);
        if (bytesRead <= 0) {
            ESP_LOGE(TAG, "Download interrupted at %lu/%d bytes", downloaded, contentLength);
            http.end();
            setError(OTA_ERROR_DOWNLOAD_FAILED, "Download interrupted");
            return false;
        }
        
        // Queue chunk for processing
        DownloadChunk_t chunk = {0};
        chunk.data = s_downloadBuffer;
        chunk.size = bytesRead;
        chunk.offset = downloaded;
        chunk.totalSize = contentLength;
        chunk.progress = 35 + ((downloaded * 50) / contentLength); // 35-85% for download
        
        snprintf(chunk.message, sizeof(chunk.message), "Downloaded %lu/%d bytes", 
                 downloaded, contentLength);
        
        if (xQueueSend(g_otaDownloadQueue, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to queue download chunk");
            http.end();
            setError(OTA_ERROR_DOWNLOAD_FAILED, "Download queue full");
            return false;
        }
        
        downloaded += bytesRead;
        updateDownloadProgress(chunk.progress, downloaded, contentLength);
        
        // Yield to other tasks
        taskYIELD();
        
        // Check download timeout
        if (millis() - s_downloadStartTime > OTA_DOWNLOAD_TIMEOUT_MS) {
            http.end();
            setError(OTA_ERROR_DOWNLOAD_FAILED, "Download timeout");
            return false;
        }
    }
    
    http.end();
    
    if (downloaded == contentLength) {
        updateProgressField(85, "Download completed successfully");
        addLogMessage("Firmware download completed");
        return true;
    }
    
    return false;
}

bool installFirmware() {
    ESP_LOGI(TAG, "Starting firmware installation...");
    updateProgressField(85, "Preparing for installation...");
    
    // Get update partition
    s_updatePartition = esp_ota_get_next_update_partition(NULL);
    if (!s_updatePartition) {
        setError(OTA_ERROR_FLASH_FAILED, "No update partition available");
        return false;
    }
    
    // Begin OTA update
    esp_err_t err = esp_ota_begin(s_updatePartition, OTA_SIZE_UNKNOWN, &s_otaHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        setError(OTA_ERROR_FLASH_FAILED, "Failed to begin OTA update");
        return false;
    }
    
    updateProgressField(90, "Installing firmware...");
    addLogMessage("Firmware installation started");
    return true;
}

bool verifyFirmware() {
    ESP_LOGI(TAG, "Verifying firmware...");
    updateProgressField(95, "Verifying firmware integrity...");
    
    esp_err_t err = esp_ota_end(s_otaHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        setError(OTA_ERROR_VERIFICATION_FAILED, "Firmware verification failed");
        return false;
    }
    
    err = esp_ota_set_boot_partition(s_updatePartition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        setError(OTA_ERROR_VERIFICATION_FAILED, "Failed to set boot partition");
        return false;
    }
    
    updateProgressField(100, "Firmware verified and ready");
    addLogMessage("Firmware verification completed");
    return true;
}

// =============================================================================
// TASK IMPLEMENTATIONS
// =============================================================================

void otaUITask(void* parameter) {
    ESP_LOGI(TAG, "UI Task started on Core %d", xPortGetCoreID());
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    while (g_otaRunning) {
        feedTaskWatchdog("UI");
        
        // Process UI updates
        processUIUpdates();
        
        // Update LVGL
        Application::TaskManager::lvglLock();
        lv_timer_handler();
        Application::TaskManager::lvglUnlock();
        
        // Process LVGL message queue
        Application::LVGLMessageHandler::processMessageQueue(nullptr);
        
        // Precise timing for 60 FPS
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(OTA_UI_UPDATE_INTERVAL_MS));
    }
    
    ESP_LOGI(TAG, "UI Task ended");
    vTaskDelete(NULL);
}

void processUIUpdates() {
    OTAUIUpdate_t update;
    
    // Process all pending UI updates (non-blocking)
    while (xQueueReceive(g_otaUIUpdateQueue, &update, 0) == pdTRUE) {
        switch (update.type) {
            case OTA_UI_UPDATE_PROGRESS:
                Application::LVGLMessageHandler::updateOtaScreenProgress(
                    update.progress.overallProgress, update.progress.detailedMessage);
                break;
            case OTA_UI_UPDATE_STATE:
                // Update UI state indicators
                break;
            case OTA_UI_UPDATE_ERROR:
                ESP_LOGE(TAG, "OTA Error: %s", update.progress.detailedMessage);
                break;
            case OTA_UI_UPDATE_LOG:
                // Add log message to UI
                break;
            case OTA_UI_UPDATE_STATS:
                // Update statistics display
                break;
        }
    }
}

void otaNetworkTask(void* parameter) {
    ESP_LOGI(TAG, "Network Task started on Core %d", xPortGetCoreID());
    
    while (g_otaRunning) {
        feedTaskWatchdog("Network");
        
        OTACommand_t command;
        if (receiveOTACommand(&command, 1000)) {
            switch (command.type) {
                case OTA_CMD_START:
                    setState(OTA_STATE_CONNECTING);
                    if (connectWiFi()) {
                        setState(OTA_STATE_CONNECTED);
                        sendOTACommand(OTA_CMD_DOWNLOAD);
                    }
                    break;
                    
                case OTA_CMD_DOWNLOAD:
                    setState(OTA_STATE_DOWNLOADING);
                    if (downloadFirmware()) {
                        sendOTACommand(OTA_CMD_INSTALL);
                    }
                    break;
                    
                case OTA_CMD_INSTALL:
                    setState(OTA_STATE_INSTALLING);
                    if (installFirmware()) {
                        // Installation continues in download task
                    }
                    break;
                    
                case OTA_CMD_CANCEL:
                    setState(OTA_STATE_CANCELLED);
                    addLogMessage("OTA cancelled by user");
                    break;
                    
                case OTA_CMD_RETRY:
                    setState(OTA_STATE_INITIALIZING);
                    sendOTACommand(OTA_CMD_START);
                    break;
                    
                case OTA_CMD_EXIT:
                case OTA_CMD_REBOOT:
                    g_otaRunning = false;
                    break;
                    
                case OTA_CMD_CLEANUP:
                    setState(OTA_STATE_CLEANUP);
                    break;
            }
        }
        
        // Background network monitoring
        if (getState() == OTA_STATE_CONNECTED || getState() == OTA_STATE_DOWNLOADING) {
            if (WiFi.status() != WL_CONNECTED) {
                setError(OTA_ERROR_WIFI_TIMEOUT, "WiFi connection lost");
            }
        }
    }
    
    ESP_LOGI(TAG, "Network Task ended");
    vTaskDelete(NULL);
}

void otaDownloadTask(void* parameter) {
    ESP_LOGI(TAG, "Download Task started on Core %d", xPortGetCoreID());
    
    while (g_otaRunning) {
        feedTaskWatchdog("Download");
        
        DownloadChunk_t chunk;
        if (xQueueReceive(g_otaDownloadQueue, &chunk, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Write chunk to flash
            esp_err_t err = esp_ota_write(s_otaHandle, chunk.data, chunk.size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
                setError(OTA_ERROR_FLASH_FAILED, "Flash write failed");
                continue;
            }
            
            // Update statistics
            if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_otaStats.downloadChunkCount++;
                xSemaphoreGive(g_otaStatsMutex);
            }
            
            // Check if download is complete
            if (chunk.offset + chunk.size >= chunk.totalSize) {
                ESP_LOGI(TAG, "Download complete, starting verification");
                sendOTACommand(OTA_CMD_INSTALL); // Trigger verification
            }
            
            taskYIELD();
        }
        
        // Handle installation completion
        if (getState() == OTA_STATE_INSTALLING) {
            setState(OTA_STATE_VERIFYING);
            if (verifyFirmware()) {
                setState(OTA_STATE_SUCCESS);
                addLogMessage("OTA update completed successfully");
                
                // Auto-reboot after success
                vTaskDelay(pdMS_TO_TICKS(3000));
                if (g_otaRunning) {
                    Boot::BootManager::clearBootRequest();
                    esp_restart();
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Download Task ended");
    vTaskDelete(NULL);
}

void otaMonitorTask(void* parameter) {
    ESP_LOGI(TAG, "Monitor Task started on Core %d", xPortGetCoreID());
    
    while (g_otaRunning) {
        feedTaskWatchdog("Monitor");
        
        // Update task statistics
        if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_otaStats.uiTaskHighWaterMark = uxTaskGetStackHighWaterMark(g_otaUITaskHandle);
            g_otaStats.networkTaskHighWaterMark = uxTaskGetStackHighWaterMark(g_otaNetworkTaskHandle);
            g_otaStats.downloadTaskHighWaterMark = uxTaskGetStackHighWaterMark(g_otaDownloadTaskHandle);
            g_otaStats.monitorTaskHighWaterMark = uxTaskGetStackHighWaterMark(g_otaMonitorTaskHandle);
            
            // Calculate average download speed
            if (g_otaProgress.bytesDownloaded > 0 && s_downloadStartTime > 0) {
                uint32_t elapsed = millis() - s_downloadStartTime;
                g_otaStats.averageDownloadSpeed = calculateDownloadSpeed(g_otaProgress.bytesDownloaded, elapsed);
                g_otaStats.totalDownloadTime = elapsed;
            }
            
            xSemaphoreGive(g_otaStatsMutex);
        }
        
        // Check for stalled operations
        uint32_t now = millis();
        if (now - s_lastProgressUpdate > 60000) { // 1 minute without progress
            if (getState() == OTA_STATE_DOWNLOADING || getState() == OTA_STATE_CONNECTING) {
                ESP_LOGW(TAG, "Operation appears stalled");
                setError(OTA_ERROR_DOWNLOAD_FAILED, "Operation timeout");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
    
    ESP_LOGI(TAG, "Monitor Task ended");
    vTaskDelete(NULL);
}

// =============================================================================
// CORE API IMPLEMENTATION
// =============================================================================

bool init() {
    ESP_LOGI(TAG, "Initializing Multithreaded OTA System");
    
    if (g_otaRunning) {
        ESP_LOGW(TAG, "OTA already running");
        return false;
    }
    
    // Allocate download buffer
    s_downloadBuffer = (uint8_t*)heap_caps_malloc(OTA_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    if (!s_downloadBuffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        return false;
    }
    
    // Create synchronization objects
    g_otaProgressMutex = xSemaphoreCreateRecursiveMutex();
    g_otaStateMutex = xSemaphoreCreateRecursiveMutex();
    g_otaStatsMutex = xSemaphoreCreateRecursiveMutex();
    
    if (!g_otaProgressMutex || !g_otaStateMutex || !g_otaStatsMutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        deinit();
        return false;
    }
    
    // Create queues
    g_otaCommandQueue = xQueueCreate(OTA_COMMAND_QUEUE_SIZE, sizeof(OTACommand_t));
    g_otaDownloadQueue = xQueueCreate(OTA_DOWNLOAD_QUEUE_SIZE, sizeof(DownloadChunk_t));
    g_otaUIUpdateQueue = xQueueCreate(OTA_UI_UPDATE_QUEUE_SIZE, sizeof(OTAUIUpdate_t));
    
    if (!g_otaCommandQueue || !g_otaDownloadQueue || !g_otaUIUpdateQueue) {
        ESP_LOGE(TAG, "Failed to create queues");
        deinit();
        return false;
    }
    
    // Initialize state
    memset(&g_otaProgress, 0, sizeof(g_otaProgress));
    memset(&g_otaStats, 0, sizeof(g_otaStats));
    g_otaProgress.state = OTA_STATE_IDLE;
    g_otaProgress.canCancel = true;
    
    g_otaRunning = true;
    
    // Create tasks
    BaseType_t result;
    
    result = xTaskCreatePinnedToCore(otaUITask, "OTA_UI", OTA_UI_TASK_STACK_SIZE, 
                                     NULL, OTA_UI_TASK_PRIORITY, &g_otaUITaskHandle, OTA_UI_TASK_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        deinit();
        return false;
    }
    
    result = xTaskCreatePinnedToCore(otaNetworkTask, "OTA_Network", OTA_NETWORK_TASK_STACK_SIZE,
                                     NULL, OTA_NETWORK_TASK_PRIORITY, &g_otaNetworkTaskHandle, OTA_NETWORK_TASK_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task");
        deinit();
        return false;
    }
    
    result = xTaskCreatePinnedToCore(otaDownloadTask, "OTA_Download", OTA_DOWNLOAD_TASK_STACK_SIZE,
                                     NULL, OTA_DOWNLOAD_TASK_PRIORITY, &g_otaDownloadTaskHandle, OTA_DOWNLOAD_TASK_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create download task");
        deinit();
        return false;
    }
    
    result = xTaskCreatePinnedToCore(otaMonitorTask, "OTA_Monitor", OTA_MONITOR_TASK_STACK_SIZE,
                                     NULL, OTA_MONITOR_TASK_PRIORITY, &g_otaMonitorTaskHandle, OTA_MONITOR_TASK_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        deinit();
        return false;
    }
    
    ESP_LOGI(TAG, "Multithreaded OTA System initialized successfully");
    ESP_LOGI(TAG, "Task distribution: UI(Core %d), Network(Core %d), Download(Core %d), Monitor(Core %d)",
             OTA_UI_TASK_CORE, OTA_NETWORK_TASK_CORE, OTA_DOWNLOAD_TASK_CORE, OTA_MONITOR_TASK_CORE);
    
    return true;
}

void deinit() {
    ESP_LOGI(TAG, "Deinitializing Multithreaded OTA System");
    
    g_otaRunning = false;
    
    // Wait for tasks to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Delete tasks
    if (g_otaUITaskHandle) {
        vTaskDelete(g_otaUITaskHandle);
        g_otaUITaskHandle = NULL;
    }
    if (g_otaNetworkTaskHandle) {
        vTaskDelete(g_otaNetworkTaskHandle);
        g_otaNetworkTaskHandle = NULL;
    }
    if (g_otaDownloadTaskHandle) {
        vTaskDelete(g_otaDownloadTaskHandle);
        g_otaDownloadTaskHandle = NULL;
    }
    if (g_otaMonitorTaskHandle) {
        vTaskDelete(g_otaMonitorTaskHandle);
        g_otaMonitorTaskHandle = NULL;
    }
    
    // Delete queues
    if (g_otaCommandQueue) {
        vQueueDelete(g_otaCommandQueue);
        g_otaCommandQueue = NULL;
    }
    if (g_otaDownloadQueue) {
        vQueueDelete(g_otaDownloadQueue);
        g_otaDownloadQueue = NULL;
    }
    if (g_otaUIUpdateQueue) {
        vQueueDelete(g_otaUIUpdateQueue);
        g_otaUIUpdateQueue = NULL;
    }
    
    // Delete mutexes
    if (g_otaProgressMutex) {
        vSemaphoreDelete(g_otaProgressMutex);
        g_otaProgressMutex = NULL;
    }
    if (g_otaStateMutex) {
        vSemaphoreDelete(g_otaStateMutex);
        g_otaStateMutex = NULL;
    }
    if (g_otaStatsMutex) {
        vSemaphoreDelete(g_otaStatsMutex);
        g_otaStatsMutex = NULL;
    }
    
    // Free download buffer
    if (s_downloadBuffer) {
        free(s_downloadBuffer);
        s_downloadBuffer = NULL;
    }
    
    ESP_LOGI(TAG, "Multithreaded OTA System deinitialized");
}

bool isRunning() {
    return g_otaRunning;
}

bool startOTA() {
    ESP_LOGI(TAG, "Starting OTA process");
    
    if (!g_otaRunning) {
        ESP_LOGE(TAG, "OTA system not initialized");
        return false;
    }
    
    setState(OTA_STATE_INITIALIZING);
    addLogMessage("OTA update initiated by user");
    
    return sendOTACommand(OTA_CMD_START);
}

bool cancelOTA() {
    ESP_LOGI(TAG, "Cancelling OTA process");
    return sendOTACommand(OTA_CMD_CANCEL);
}

bool retryOTA() {
    ESP_LOGI(TAG, "Retrying OTA process");
    return sendOTACommand(OTA_CMD_RETRY);
}

void exitOTA() {
    ESP_LOGI(TAG, "Exiting OTA mode");
    sendOTACommand(OTA_CMD_EXIT);
    Boot::BootManager::clearBootRequest();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

DetailedProgress_t getProgress() {
    DetailedProgress_t progress = {0};
    if (xSemaphoreTake(g_otaProgressMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        progress = g_otaProgress;
        xSemaphoreGive(g_otaProgressMutex);
    }
    return progress;
}

OTAStats_t getStats() {
    OTAStats_t stats = {0};
    if (xSemaphoreTake(g_otaStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats = g_otaStats;
        xSemaphoreGive(g_otaStatsMutex);
    }
    return stats;
}

} // namespace MultiOTA