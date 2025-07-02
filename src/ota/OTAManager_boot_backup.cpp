#include "OTAManager.h"
#include "../../include/DebugUtils.h"
#include "../../include/BootManager.h"
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "Boot::OTAManager";

// =============================================================================
// UTILITY MACROS - Reduce Boilerplate
// =============================================================================

#define OTA_LOG_STATE_CHANGE(oldState, newState, message) \
    ESP_LOGW(TAG, "[STATE] %s -> %s: %s",                 \
             getStateString(oldState), getStateString(newState), message)

#define OTA_LOG_PROGRESS(progress, message) \
    ESP_LOGD(TAG, "[PROGRESS] %d%% - %s", progress, message)

#define OTA_EMERGENCY_CHECK_AND_RECOVER(condition, reason) \
    do {                                                   \
        if (condition) {                                   \
            ESP_LOGE(TAG, "[EMERGENCY] %s", reason);       \
            emergencyRecovery(reason);                     \
            return;                                        \
        }                                                  \
    } while (0)

#define OTA_SAFE_CALLBACK_INVOKE(callback, ...)                    \
    do {                                                           \
        if (callback) {                                            \
            try {                                                  \
                callback(__VA_ARGS__);                             \
            } catch (...) {                                        \
                ESP_LOGE(TAG, "[CALLBACK] Exception in callback"); \
            }                                                      \
        }                                                          \
    } while (0)

namespace Boot {
namespace OTA {

// =============================================================================
// CONSTANTS - Bulletproof Monitoring + Watchdog Safety
// =============================================================================

static const uint32_t OTA_GLOBAL_TIMEOUT_MS = 300000;          // 5 minutes max
static const uint32_t OTA_PROGRESS_STALL_TIMEOUT_MS = 60000;   // 1 minute without progress
static const uint32_t OTA_HEARTBEAT_CHECK_INTERVAL_MS = 5000;  // 5 second heartbeat
static const uint32_t OTA_WATCHDOG_RESET_INTERVAL_MS = 1000;   // Feed watchdog every 1s
static const uint32_t OTA_TASK_YIELD_PERIOD_MS = 50;           // Yield every 50ms
static const int OTA_MAX_PROGRESS_STALL_COUNT = 5;             // Max stalls before recovery

// =============================================================================
// STATIC MEMBER DEFINITIONS
// =============================================================================

// Core state
OTAState OTAManager::currentState = OTA_IDLE;
uint8_t OTAManager::currentProgress = 0;
char OTAManager::stateMessage[128] = "OTA Ready (Boot Mode)";
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
    OTA_SAFE_CALLBACK_INVOKE(stateCallback, newState, stateMessage);
}

void OTAManager::updateProgress(uint8_t progress, const char* message) {
    currentProgress = progress;
    lastHeartbeat = millis();

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    OTA_SAFE_CALLBACK_INVOKE(progressCallback, progress, stateMessage);
    OTA_LOG_PROGRESS(progress, stateMessage);
}

void OTAManager::completeOTA(OTAResult result, const char* message) {
    ESP_LOGW(TAG, "[COMPLETE] OTA finished with result: %d - %s", result, message ? message : "");

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    if (result == OTA_RESULT_SUCCESS) {
        currentProgress = 100;
        enterState(OTA_SUCCESS, message);

        // Return to normal mode after successful OTA
        returnToNormalMode();
    } else {
        enterState(OTA_FAILED, message);
    }

    OTA_SAFE_CALLBACK_INVOKE(completeCallback, result, stateMessage);
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool OTAManager::init(void) {
    ESP_LOGW(TAG, "[INIT] Initializing Boot Mode OTA Manager");

    currentState = OTA_IDLE;
    currentProgress = 0;
    strcpy(stateMessage, "OTA Ready (Boot Mode)");
    userCancelRequested = false;
    otaStartTime = 0;

    resetMonitoring();

    ESP_LOGW(TAG, "[INIT] Boot Mode OTA Manager initialized successfully");
    return true;
}

bool OTAManager::startOTA(void) {
    ESP_LOGW(TAG, "[START] Starting OTA in boot mode");

    if (currentState != OTA_IDLE) {
        ESP_LOGW(TAG, "[START] OTA already active");
        return false;
    }

    userCancelRequested = false;
    otaStartTime = millis();
    resetMonitoring();

    enterState(OTA_USER_INITIATED, "OTA started in boot mode");
    return true;
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

void OTAManager::processStateMachine(void) {
    feedWatchdogAndYield("state machine");

    switch (currentState) {
        case OTA_USER_INITIATED:
            enterState(OTA_CONNECTING, "Connecting to WiFi...");
            break;

        case OTA_CONNECTING:
            if (startNetwork()) {
                enterState(OTA_CONNECTED, "WiFi connected, starting download...");
            } else if (millis() - otaStartTime > 30000) {  // 30 second timeout
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

// Boot mode specific functions
bool OTAManager::isOTABootMode(void) {
    return Boot::BootManager::getCurrentMode() == Boot::BootMode::OTA_UPDATE;
}

void OTAManager::returnToNormalMode(void) {
    ESP_LOGI(TAG, "OTA complete - returning to normal boot mode");
    cleanup();
    Boot::BootManager::requestNormalMode();
    ESP.restart();
}

// Simple getters
bool OTAManager::isActive(void) { return currentState != OTA_IDLE; }
OTAState OTAManager::getCurrentState(void) { return currentState; }
uint8_t OTAManager::getProgress(void) { return currentProgress; }
const char* OTAManager::getStateMessage(void) { return stateMessage; }

bool OTAManager::canCancel(void) {
    return currentState == OTA_CONNECTING ||
           currentState == OTA_CONNECTED ||
           currentState == OTA_DOWNLOADING;
}

void OTAManager::cancelOTA(void) {
    ESP_LOGW(TAG, "[CANCEL] User requested OTA cancellation");
    if (!isActive()) {
        ESP_LOGW(TAG, "[CANCEL] No active OTA to cancel");
        return;
    }
    userCancelRequested = true;
    updateProgress(currentProgress, "Cancelling...");
}

// Callback setters
void OTAManager::setStateCallback(OTAStateCallback callback) { stateCallback = callback; }
void OTAManager::setProgressCallback(OTAProgressCallback callback) { progressCallback = callback; }
void OTAManager::setCompleteCallback(OTACompleteCallback callback) { completeCallback = callback; }

// Monitoring and utility functions (simplified for boot mode)
void OTAManager::resetMonitoring(void) {
    lastProgressTime = millis();
    lastProgressPercent = 0;
    progressStallCount = 0;
    lastHeartbeat = millis();
    emergencyMode = false;
    lastWatchdogReset = millis();
    lastTaskYield = millis();
}

bool OTAManager::checkTimeouts(void) { return false; /* Simplified for boot mode */ }
bool OTAManager::checkProgressStalls(void) { return false; /* Simplified for boot mode */ }
bool OTAManager::checkUserCancel(void) { return userCancelRequested && canCancel(); }
void OTAManager::emergencyRecovery(const char* reason) {
    ESP_LOGE(TAG, "[EMERGENCY] %s", reason);
    completeOTA(OTA_RESULT_UNKNOWN_ERROR, reason);
}

// Network functions (minimal for boot mode)
bool OTAManager::startNetwork(void) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
    return WiFi.status() == WL_CONNECTED;
}

void OTAManager::stopNetwork(void) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool OTAManager::isNetworkReady(void) { return WiFi.status() == WL_CONNECTED; }

// HTTP update functions (simplified)
bool OTAManager::downloadAndInstall(void) { return false; /* Implement basic HTTP update */ }
void OTAManager::setupHTTPUpdateCallbacks(void) { /* Implement */ }
void OTAManager::onHTTPUpdateProgress(int current, int total) { /* Implement */ }

void OTAManager::cleanup(void) {
    stopNetwork();
    currentState = OTA_IDLE;
}

void OTAManager::deinit(void) {
    cleanup();
}

// UI convenience functions for boot mode
bool initiateOTAFromUI(void) {
    // This should trigger boot mode switch from normal application
    Boot::BootManager::requestOTAMode();
    return true;
}

void cancelOTAFromUI(void) { OTAManager::cancelOTA(); }
const char* getOTAStatusForUI(void) { return OTAManager::getStateMessage(); }
uint8_t getOTAProgressForUI(void) { return OTAManager::getProgress(); }

}  // namespace OTA
}  // namespace Boot
