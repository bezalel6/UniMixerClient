#include "OTAManager.h"
#include "../../include/DebugUtils.h"
#include "../application/LVGLMessageHandler.h"
#include "../application/TaskManager.h"
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "OTAManager";

// =============================================================================
// UTILITY MACROS - Reduce Boilerplate
// =============================================================================

#define OTA_LOG_STATE_CHANGE(oldState, newState, message) \
    ESP_LOGI(TAG, "[STATE] %s -> %s: %s",                 \
             getStateString(oldState), getStateString(newState), message)

#define OTA_LOG_PROGRESS(progress, message) \
    ESP_LOGD(TAG, "[PROGRESS] %d%% - %s", progress, message)

#define OTA_UPDATE_UI_THROTTLED(progress, message, interval_ms)                          \
    do {                                                                                 \
        static uint32_t lastUIUpdate = 0;                                                \
        uint32_t now = millis();                                                         \
        if (now - lastUIUpdate >= interval_ms) {                                         \
            Application::LVGLMessageHandler::updateOtaScreenProgress(progress, message); \
            lastUIUpdate = now;                                                          \
        }                                                                                \
    } while (0)

#define OTA_EMERGENCY_CHECK_AND_RECOVER(condition, reason) \
    do {                                                   \
        if (condition) {                                   \
            ESP_LOGE(TAG, "[EMERGENCY] %s", reason);       \
            emergencyRecovery(reason);                     \
            return;                                        \
        }                                                  \
    } while (0)

#define OTA_SAFE_CALLBACK_INVOKE(callback, ...)                                 \
    do {                                                                        \
        if (callback) {                                                         \
            try {                                                               \
                callback(__VA_ARGS__);                                          \
            } catch (...) {                                                     \
                ESP_LOGW(TAG, "[CALLBACK] Exception in callback - continuing"); \
            }                                                                   \
        }                                                                       \
    } while (0)

// =============================================================================
// CONSTANTS - Bulletproof Monitoring + Watchdog Safety
// =============================================================================

// Use different names to avoid macro conflicts with OTAConfig.h
static const uint32_t OTA_GLOBAL_TIMEOUT_MS = 300000;          // 5 minutes max
static const uint32_t OTA_PROGRESS_STALL_TIMEOUT_MS = 60000;   // 1 minute without progress
static const uint32_t OTA_HEARTBEAT_CHECK_INTERVAL_MS = 5000;  // 5 second heartbeat
static const uint32_t OTA_WATCHDOG_RESET_INTERVAL_MS = 1000;   // Feed watchdog every 1s
static const uint32_t OTA_TASK_YIELD_PERIOD_MS = 50;           // Yield every 50ms
static const uint32_t OTA_UI_THROTTLE_NORMAL_MS = 250;         // UI updates max 4/second
static const uint32_t OTA_UI_THROTTLE_DOWNLOAD_MS = 500;       // Slower during download
static const int OTA_MAX_PROGRESS_STALL_COUNT = 5;             // Max stalls before recovery

namespace Hardware {
namespace OTA {

// =============================================================================
// STATIC MEMBER DEFINITIONS
// =============================================================================

// Core state
OTAState OTAManager::currentState = OTA_IDLE;
uint8_t OTAManager::currentProgress = 0;
char OTAManager::stateMessage[128] = "OTA Ready (Network-Free Mode)";
uint32_t OTAManager::otaStartTime = 0;
bool OTAManager::userCancelRequested = false;

// UI callbacks
OTAStateCallback OTAManager::stateCallback = nullptr;
OTAProgressCallback OTAManager::progressCallback = nullptr;
OTACompleteCallback OTAManager::completeCallback = nullptr;

// Bulletproof monitoring
uint32_t OTAManager::lastProgressTime = 0;
uint8_t OTAManager::lastProgressPercent = 0;
int OTAManager::progressStallCount = 0;
uint32_t OTAManager::lastHeartbeat = 0;
bool OTAManager::emergencyMode = false;

// Watchdog safety
uint32_t OTAManager::lastWatchdogReset = 0;
uint32_t OTAManager::lastTaskYield = 0;

// =============================================================================
// WATCHDOG SAFETY FUNCTIONS
// =============================================================================

void OTAManager::feedWatchdogAndYield(const char* context) {
    uint32_t currentTime = millis();

    // Reset watchdog periodically
    if (currentTime - lastWatchdogReset >= OTA_WATCHDOG_RESET_INTERVAL_MS) {
#ifdef CONFIG_ESP_TASK_WDT_EN
        esp_task_wdt_reset();
#endif
        lastWatchdogReset = currentTime;
        ESP_LOGV(TAG, "[WATCHDOG] Reset during %s", context ? context : "OTA operation");
    }

    // Yield to other tasks periodically
    if (currentTime - lastTaskYield >= OTA_TASK_YIELD_PERIOD_MS) {
        vTaskDelay(pdMS_TO_TICKS(1));
        lastTaskYield = currentTime;
    }
}

void OTAManager::safeDelay(uint32_t ms, const char* context) {
    uint32_t remaining = ms;
    while (remaining > 0 && !userCancelRequested) {
        uint32_t chunkSize = min(remaining, 100U);
        vTaskDelay(pdMS_TO_TICKS(chunkSize));
        feedWatchdogAndYield(context);
        remaining -= chunkSize;

        if (userCancelRequested) {
            ESP_LOGW(TAG, "[SAFE_DELAY] Interrupted by user cancellation");
            break;
        }
    }
}

// =============================================================================
// STATE MANAGEMENT
// =============================================================================

const char* OTAManager::getStateString(OTAState state) {
    switch (state) {
        case OTA_IDLE:
            return "IDLE";
        case OTA_USER_INITIATED:
            return "USER_INITIATED";
        case OTA_CONNECTING:
            return "CONNECTING";
        case OTA_CONNECTED:
            return "CONNECTED";
        case OTA_DOWNLOADING:
            return "DOWNLOADING";
        case OTA_INSTALLING:
            return "INSTALLING";
        case OTA_SUCCESS:
            return "SUCCESS";
        case OTA_FAILED:
            return "FAILED";
        case OTA_CANCELLED:
            return "CANCELLED";
        case OTA_CLEANUP:
            return "CLEANUP";
        default:
            return "UNKNOWN";
    }
}

void OTAManager::enterState(OTAState newState, const char* message) {
    OTAState oldState = currentState;
    currentState = newState;

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    OTA_LOG_STATE_CHANGE(oldState, newState, stateMessage);

    // UI update with appropriate throttling
    uint32_t throttleMs = (newState == OTA_DOWNLOADING) ? OTA_UI_THROTTLE_DOWNLOAD_MS : OTA_UI_THROTTLE_NORMAL_MS;
    OTA_UPDATE_UI_THROTTLED(currentProgress, stateMessage, throttleMs);

    // Invoke state callback
    OTA_SAFE_CALLBACK_INVOKE(stateCallback, newState, stateMessage);

    // Update TaskManager state for resource management
    switch (newState) {
        case OTA_DOWNLOADING:
        case OTA_INSTALLING:
            Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_DOWNLOADING);
            break;
        case OTA_SUCCESS:
        case OTA_FAILED:
        case OTA_CANCELLED:
            Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_COMPLETE);
            break;
        default:
            Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_CHECKING);
            break;
    }
}

void OTAManager::updateProgress(uint8_t progress, const char* message) {
    currentProgress = progress;
    lastHeartbeat = millis();

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    // Throttled UI updates - slower during download to prevent LVGL blocking
    uint32_t throttleMs = (currentState == OTA_DOWNLOADING) ? OTA_UI_THROTTLE_DOWNLOAD_MS : OTA_UI_THROTTLE_NORMAL_MS;
    OTA_UPDATE_UI_THROTTLED(progress, stateMessage, throttleMs);

    // Invoke progress callback
    OTA_SAFE_CALLBACK_INVOKE(progressCallback, progress, stateMessage);

    // Update TaskManager with progress
    Application::TaskManager::updateOTAProgress(progress, true, false, stateMessage);

    OTA_LOG_PROGRESS(progress, stateMessage);
}

void OTAManager::completeOTA(OTAResult result, const char* message) {
    ESP_LOGI(TAG, "[COMPLETE] OTA finished with result: %d - %s", result, message ? message : "");

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    // Final progress update
    if (result == OTA_RESULT_SUCCESS) {
        currentProgress = 100;
        enterState(OTA_SUCCESS, message);
    } else {
        enterState(OTA_FAILED, message);
    }

    // Invoke completion callback
    OTA_SAFE_CALLBACK_INVOKE(completeCallback, result, stateMessage);

    // Auto-reboot on success
#if OTA_AUTO_REBOOT_ON_SUCCESS
    if (result == OTA_RESULT_SUCCESS) {
        updateProgress(100, "Rebooting in 3 seconds...");

        for (int i = 3; i > 0; i--) {
            if (userCancelRequested) {
                ESP_LOGW(TAG, "[REBOOT] Cancelled by user");
                return;
            }

            char countdownMsg[64];
            snprintf(countdownMsg, sizeof(countdownMsg), "Rebooting in %d second%s...",
                     i, (i == 1) ? "" : "s");
            updateProgress(100, countdownMsg);

            safeDelay(1000, "reboot countdown");
        }

        if (!userCancelRequested) {
            ESP_LOGI(TAG, "[REBOOT] Restarting system...");
            ESP.restart();
        }
    }
#endif
}

// =============================================================================
// BULLETPROOF MONITORING
// =============================================================================

bool OTAManager::checkTimeouts(void) {
    if (currentState == OTA_IDLE) return false;

    uint32_t now = millis();
    uint32_t elapsed = now - otaStartTime;

    // Global timeout
    if (elapsed > OTA_GLOBAL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "[TIMEOUT] Global timeout after %u ms", elapsed);
        completeOTA(OTA_RESULT_TIMEOUT, "OTA timeout");
        return true;
    }

    // Progress timeout
    if (now - lastProgressTime > OTA_PROGRESS_STALL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "[TIMEOUT] Progress timeout (%u ms without progress)",
                 OTA_PROGRESS_STALL_TIMEOUT_MS);
        emergencyRecovery("Progress timeout");
        return true;
    }

    return false;
}

bool OTAManager::checkProgressStalls(void) {
    uint32_t now = millis();

    // Check for progress stalls
    if (currentProgress == lastProgressPercent) {
        if (now - lastProgressTime > 10000) {  // 10 seconds same progress
            progressStallCount++;
            ESP_LOGW(TAG, "[STALL] Progress stall detected %d%% (count: %d)",
                     currentProgress, progressStallCount);

            if (progressStallCount >= OTA_MAX_PROGRESS_STALL_COUNT) {
                ESP_LOGE(TAG, "[STALL] Too many stalls, triggering recovery");
                emergencyRecovery("Too many progress stalls");
                return true;
            }
        }
    } else {
        // Progress advanced, reset monitoring
        lastProgressPercent = currentProgress;
        lastProgressTime = now;
        progressStallCount = 0;
    }

    return false;
}

bool OTAManager::checkUserCancel(void) {
    if (!userCancelRequested) return false;

    if (canCancel()) {
        ESP_LOGI(TAG, "[CANCEL] Processing user cancellation");
        completeOTA(OTA_RESULT_CANCELLED, "Cancelled by user");
        return true;
    }

    return false;
}

void OTAManager::emergencyRecovery(const char* reason) {
    if (emergencyMode) {
        ESP_LOGW(TAG, "[EMERGENCY] Already in recovery mode");
        return;
    }

    emergencyMode = true;
    ESP_LOGE(TAG, "[EMERGENCY] Emergency recovery: %s", reason);

    // Stop any ongoing operations (HTTPUpdate doesn't have explicit end method)
    // Note: HTTPUpdate operations are atomic and will complete or fail

    // Enhanced error reporting
    char recoveryMsg[128];
    snprintf(recoveryMsg, sizeof(recoveryMsg), "Recovery: %s", reason);

    updateProgress(currentProgress, recoveryMsg);
    safeDelay(2000, "emergency display");

    updateProgress(currentProgress, "System recovery in progress...");
    safeDelay(1000, "recovery process");

    // Complete with error
    completeOTA(OTA_RESULT_UNKNOWN_ERROR, reason);
}

void OTAManager::resetMonitoring(void) {
    lastProgressTime = millis();
    lastProgressPercent = 0;
    progressStallCount = 0;
    lastHeartbeat = millis();
    emergencyMode = false;
    lastWatchdogReset = millis();
    lastTaskYield = millis();
}

// =============================================================================
// NETWORK MANAGEMENT
// =============================================================================

bool OTAManager::startNetwork(void) {
    static bool networkInitialized = false;

    if (!networkInitialized) {
        ESP_LOGI(TAG, "[NETWORK] Starting minimal network for OTA");

        WiFi.mode(WIFI_STA);
        WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
        networkInitialized = true;

        updateProgress(5, "Connecting to WiFi...");
    }

    feedWatchdogAndYield("network connection");

    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(TAG, "[NETWORK] WiFi connected: %s", WiFi.localIP().toString().c_str());
        updateProgress(20, "WiFi connected");
        return true;
    }

    // Update connection progress
    static uint32_t lastProgressUpdate = 0;
    if (millis() - lastProgressUpdate > 2000) {
        static uint8_t connectProgress = 5;
        connectProgress = min(connectProgress + 2, 18);
        updateProgress(connectProgress, "Connecting to WiFi...");
        lastProgressUpdate = millis();
        feedWatchdogAndYield("WiFi connection progress");
    }

    return false;
}

void OTAManager::stopNetwork(void) {
    ESP_LOGI(TAG, "[NETWORK] Stopping network - returning to network-free mode");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    safeDelay(100, "network shutdown");

    ESP_LOGI(TAG, "[NETWORK-FREE] Network disabled - back to network-free operation");
}

bool OTAManager::isNetworkReady(void) {
    return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
// HTTP UPDATE IMPLEMENTATION
// =============================================================================

void OTAManager::setupHTTPUpdateCallbacks(void) {
    httpUpdate.onProgress([](int current, int total) {
        onHTTPUpdateProgress(current, total);
    });

    httpUpdate.setLedPin(-1);          // Disable LED indication
    httpUpdate.rebootOnUpdate(false);  // We handle reboot ourselves
}

void OTAManager::onHTTPUpdateProgress(int current, int total) {
    static uint32_t lastCallbackTime = 0;
    uint32_t now = millis();

    // Throttle callback frequency to prevent overwhelming UI
    if (now - lastCallbackTime < 200) return;  // Max 5 times per second
    lastCallbackTime = now;

    feedWatchdogAndYield("download progress");

    if (total > 0) {
        uint8_t progress = 20 + ((current * 60) / total);  // 20-80% for download
        char progressMsg[64];
        snprintf(progressMsg, sizeof(progressMsg), "Downloading: %d/%d bytes", current, total);
        updateProgress(progress, progressMsg);

        // Check for user cancellation
        if (userCancelRequested) {
            ESP_LOGW(TAG, "[DOWNLOAD] User cancellation detected");
        }
    }
}

bool OTAManager::downloadAndInstall(void) {
    if (!isNetworkReady()) return false;

    ESP_LOGI(TAG, "[DOWNLOAD] Starting firmware download from: %s", OTA_SERVER_URL);

    setupHTTPUpdateCallbacks();
    feedWatchdogAndYield("pre-download");

    // Create WiFi client for HTTP update
    WiFiClient client;
    HTTPUpdateResult result = httpUpdate.update(client, String(OTA_SERVER_URL), "");

    feedWatchdogAndYield("post-download");

    switch (result) {
        case HTTP_UPDATE_FAILED:
            ESP_LOGE(TAG, "[DOWNLOAD] Failed: %s", httpUpdate.getLastErrorString().c_str());
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Download failed");
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            ESP_LOGI(TAG, "[DOWNLOAD] No updates available");
            completeOTA(OTA_RESULT_SUCCESS, "Already up to date");
            return true;

        case HTTP_UPDATE_OK:
            ESP_LOGI(TAG, "[DOWNLOAD] Download completed successfully");
            updateProgress(80, "Download complete, installing...");
            return true;

        default:
            ESP_LOGE(TAG, "[DOWNLOAD] Unknown result: %d", result);
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Unknown download error");
            return false;
    }
}

// =============================================================================
// STATE MACHINE
// =============================================================================

void OTAManager::processStateMachine(void) {
    feedWatchdogAndYield("state machine");

    switch (currentState) {
        case OTA_USER_INITIATED:
            enterState(OTA_CONNECTING, "Connecting to WiFi...");
            break;

        case OTA_CONNECTING:
            if (startNetwork()) {
                enterState(OTA_CONNECTED, "WiFi connected, starting download...");
            } else if (millis() - otaStartTime > OTA_NETWORK_CONNECT_TIMEOUT_MS) {
                completeOTA(OTA_RESULT_NETWORK_FAILED, "Failed to connect to WiFi");
            }
            break;

        case OTA_CONNECTED:
            enterState(OTA_DOWNLOADING, "Downloading firmware...");
            break;

        case OTA_DOWNLOADING:
            if (downloadAndInstall()) {
                enterState(OTA_INSTALLING, "Installing firmware...");
            }
            break;

        case OTA_INSTALLING:
            updateProgress(90, "Installing firmware...");
            safeDelay(1000, "installation");
            updateProgress(95, "Finalizing installation...");
            safeDelay(500, "finalization");
            completeOTA(OTA_RESULT_SUCCESS, "OTA completed successfully");
            break;

        case OTA_SUCCESS:
        case OTA_FAILED:
        case OTA_CANCELLED:
            enterState(OTA_CLEANUP, "Cleaning up...");
            break;

        case OTA_CLEANUP:
            cleanup();
            break;

        default:
            break;
    }
}

void OTAManager::cleanup(void) {
    ESP_LOGI(TAG, "[CLEANUP] Cleaning up OTA resources");

    stopNetwork();
    userCancelRequested = false;
    otaStartTime = 0;
    resetMonitoring();

    enterState(OTA_IDLE, "Returned to network-free mode");
    Application::LVGLMessageHandler::hideOtaScreen();
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool OTAManager::init(void) {
    ESP_LOGI(TAG, "[INIT] Initializing Unified OTA Manager");

    currentState = OTA_IDLE;
    currentProgress = 0;
    strcpy(stateMessage, "OTA Ready (Network-Free Mode)");
    userCancelRequested = false;
    otaStartTime = 0;

    resetMonitoring();

    ESP_LOGI(TAG, "[INIT] Unified OTA Manager initialized successfully");
    return true;
}

void OTAManager::deinit(void) {
    ESP_LOGI(TAG, "[DEINIT] Deinitializing Unified OTA Manager");

    if (isActive()) {
        cancelOTA();
    }

    cleanup();
}

void OTAManager::update(void) {
    if (currentState == OTA_IDLE) return;

    // Bulletproof monitoring checks
    OTA_EMERGENCY_CHECK_AND_RECOVER(checkTimeouts(), "Timeout detected");
    OTA_EMERGENCY_CHECK_AND_RECOVER(checkProgressStalls(), "Progress stall detected");
    OTA_EMERGENCY_CHECK_AND_RECOVER(checkUserCancel(), "User cancellation");

    // Process state machine
    processStateMachine();
}

bool OTAManager::startOTA(void) {
    ESP_LOGI(TAG, "[START] User initiated OTA");

    if (currentState != OTA_IDLE) {
        ESP_LOGW(TAG, "[START] OTA already active");
        return false;
    }

    userCancelRequested = false;
    otaStartTime = millis();
    resetMonitoring();

    enterState(OTA_USER_INITIATED, "OTA started by user");
    Application::LVGLMessageHandler::showOtaScreen();

    return true;
}

void OTAManager::cancelOTA(void) {
    ESP_LOGI(TAG, "[CANCEL] User requested OTA cancellation");

    if (!isActive()) {
        ESP_LOGW(TAG, "[CANCEL] No active OTA to cancel");
        return;
    }

    userCancelRequested = true;
    updateProgress(currentProgress, "Cancelling...");
}

// Simple getters
bool OTAManager::isActive(void) { return currentState != OTA_IDLE; }
OTAState OTAManager::getCurrentState(void) { return currentState; }
uint8_t OTAManager::getProgress(void) { return currentProgress; }
const char* OTAManager::getStateMessage(void) { return stateMessage; }
bool OTAManager::isNetworkFree(void) { return currentState == OTA_IDLE; }
size_t OTAManager::getFreedMemory(void) { return 8192 + 4096; }  // Estimated freed memory

bool OTAManager::canCancel(void) {
    return currentState == OTA_CONNECTING ||
           currentState == OTA_CONNECTED ||
           currentState == OTA_DOWNLOADING;
}

// Callback setters
void OTAManager::setStateCallback(OTAStateCallback callback) { stateCallback = callback; }
void OTAManager::setProgressCallback(OTAProgressCallback callback) { progressCallback = callback; }
void OTAManager::setCompleteCallback(OTACompleteCallback callback) { completeCallback = callback; }

// =============================================================================
// UI CONVENIENCE FUNCTIONS
// =============================================================================

bool initiateOTAFromUI(void) {
    return OTAManager::startOTA();
}

void cancelOTAFromUI(void) {
    OTAManager::cancelOTA();
}

const char* getOTAStatusForUI(void) {
    return OTAManager::getStateMessage();
}

uint8_t getOTAProgressForUI(void) {
    return OTAManager::getProgress();
}

}  // namespace OTA
}  // namespace Hardware
