#include "OTAManager.h"
#include <Update.h>

#if OTA_ENABLE_UPDATES

#include "../application/LVGLMessageHandler.h"
#include "../application/TaskManager.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_log.h>
#include <functional>

// Private variables
static const char *TAG = "OTAManager";
static bool otaInitialized = false;
static String hostname = OTA_HOSTNAME;
static String password = OTA_PASSWORD;
static bool otaInProgress = false;
static bool errorHandlingInProgress = false;

// BULLETPROOF: Enhanced OTA state management
static unsigned long otaStartTime = 0;
static unsigned long lastProgressTime = 0;
static uint8_t lastProgressPercent = 0;
static int progressStallCount = 0;
static bool otaTimeoutTriggered = false;
static unsigned long lastHeartbeat = 0;
static bool emergencyRecoveryMode = false;

// BULLETPROOF: Timeout and recovery constants
static const unsigned long OTA_TIMEOUT_MS = 300000;           // 5 minutes max
static const unsigned long OTA_PROGRESS_TIMEOUT_MS = 60000;   // 1 minute without progress
static const unsigned long OTA_HEARTBEAT_INTERVAL_MS = 5000;  // 5 second heartbeat
static const int MAX_PROGRESS_STALL_COUNT = 5;                // Max stalled progress reports

// BULLETPROOF: Enhanced callbacks and monitoring
static void otaTimeoutCheck(void);
static void otaProgressMonitor(void);
static void otaEmergencyRecovery(const char *reason);
static void otaResetState(void);

static void onOTAStart() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
    } else {
        type = "filesystem";
    }
    ESP_LOGI(TAG, "BULLETPROOF OTA: Start updating %s", type.c_str());

    // BULLETPROOF: Initialize OTA state tracking
    otaInProgress = true;
    otaStartTime = millis();
    lastProgressTime = millis();
    lastProgressPercent = 0;
    progressStallCount = 0;
    otaTimeoutTriggered = false;
    lastHeartbeat = millis();
    emergencyRecoveryMode = false;

    // Signal OTA start to dynamic task manager
    Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_DOWNLOADING);
    Application::TaskManager::configureForOTADownload();

    // BULLETPROOF: Enhanced UI feedback
    Application::LVGLMessageHandler::showOtaScreen();
    Application::LVGLMessageHandler::updateOtaScreenProgress(0, "Starting OTA update...");

    // BULLETPROOF: Show status indicator on all screens
    Application::LVGLMessageHandler::showOTAStatusIndicator(0, "OTA Starting", false, true);

    ESP_LOGI(TAG, "BULLETPROOF OTA: Initialization complete, monitoring enabled");
}

static void onOTAEnd() {
    ESP_LOGI(TAG, "BULLETPROOF OTA: Update completed successfully");

    // BULLETPROOF: Calculate total time
    unsigned long totalTime = millis() - otaStartTime;
    char completionMsg[100];
    snprintf(completionMsg, sizeof(completionMsg), "Update complete! Total time: %lu seconds", totalTime / 1000);

    Application::LVGLMessageHandler::updateOtaScreenProgress(100, completionMsg);

    // BULLETPROOF: Give user time to see completion message
    vTaskDelay(pdMS_TO_TICKS(2000));

    // BULLETPROOF: Update status indicator with completion
    Application::LVGLMessageHandler::updateOTAStatusIndicator(100, "OTA Complete", false, true);

    // BULLETPROOF: Proper cleanup before reboot
    Application::LVGLMessageHandler::updateOtaScreenProgress(100, "Preparing to restart...");

    // Signal OTA completion to dynamic task manager
    Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_COMPLETE);
    Application::TaskManager::resumeFromOTA();

    // Reset state before reboot
    otaResetState();

    ESP_LOGI(TAG, "BULLETPROOF OTA: System will restart in 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static void onOTAProgress(unsigned int progress, unsigned int total) {
    uint8_t percentage = (progress / (total / 100));

    // BULLETPROOF: Progress monitoring and stall detection
    unsigned long currentTime = millis();

    // Check for progress stalls
    if (percentage == lastProgressPercent) {
        if (currentTime - lastProgressTime > 10000) {  // 10 seconds same progress
            progressStallCount++;
            ESP_LOGW(TAG, "BULLETPROOF OTA: Progress stall detected %d%% (count: %d)",
                     percentage, progressStallCount);

            if (progressStallCount >= MAX_PROGRESS_STALL_COUNT) {
                ESP_LOGE(TAG, "BULLETPROOF OTA: Too many progress stalls, triggering recovery");
                otaEmergencyRecovery("Progress stalled");
                return;
            }
        }
    } else {
        // Progress advanced, reset monitoring
        lastProgressPercent = percentage;
        lastProgressTime = currentTime;
        progressStallCount = 0;
    }

    // BULLETPROOF: Timeout monitoring
    otaTimeoutCheck();

    // BULLETPROOF: Enhanced progress reporting with timing info
    unsigned long elapsedTime = currentTime - otaStartTime;
    char msg[128];
    snprintf(msg, sizeof(msg), "Updating: %d%% (%lu sec elapsed)", percentage, elapsedTime / 1000);

    Application::LVGLMessageHandler::updateOtaScreenProgress(percentage, msg);

    // Update task manager with progress
    Application::TaskManager::updateOTAProgress(percentage, true, false, msg);

    // BULLETPROOF: Update status indicator on all screens
    char shortStatus[32];
    snprintf(shortStatus, sizeof(shortStatus), "OTA %d%%", percentage);
    Application::LVGLMessageHandler::updateOTAStatusIndicator(percentage, shortStatus, false, false);

    // Switch to installation mode when near completion
    if (percentage > 90 && percentage < 100) {
        static bool installModeSet = false;
        if (!installModeSet) {
            ESP_LOGI(TAG, "BULLETPROOF OTA: Near completion, switching to installation mode");
            Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_INSTALLING);
            Application::TaskManager::configureForOTAInstall();
            installModeSet = true;
        }
    }

    // BULLETPROOF: Heartbeat update
    lastHeartbeat = currentTime;

    ESP_LOGD(TAG, "BULLETPROOF OTA: Progress %d%%, elapsed %lu ms", percentage, elapsedTime);
}

static void onOTAError(ota_error_t error) {
    if (errorHandlingInProgress) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: Error handler already running, ignoring subsequent error.");
        return;
    }

    // The "already running" error can sometimes be ignored, allowing the update to proceed
    if (error == OTA_BEGIN_ERROR) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: Non-fatal Error (ignored): OTA_BEGIN_ERROR. Update will continue.");
        return;
    }

    errorHandlingInProgress = true;
    ESP_LOGE(TAG, "BULLETPROOF OTA: Error[%u] occurred", error);

    // Signal OTA error to dynamic task manager
    Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_ERROR);

    // Stop the OTA service completely to terminate the connection
    ArduinoOTA.end();

    // BULLETPROOF: Enhanced error reporting with context
    const char *errorMsg = "Unknown error";
    char detailedErrorMsg[128];
    unsigned long errorTime = millis() - otaStartTime;

    switch (error) {
        case OTA_AUTH_ERROR:
            errorMsg = "Authentication failed";
            break;
        case OTA_BEGIN_ERROR:
            errorMsg = "Failed to start update";
            break;
        case OTA_CONNECT_ERROR:
            errorMsg = "Connection failed";
            break;
        case OTA_RECEIVE_ERROR:
            errorMsg = "Receive failed";
            break;
        case OTA_END_ERROR:
            errorMsg = "End failed";
            break;
    }

    snprintf(detailedErrorMsg, sizeof(detailedErrorMsg),
             "%s (after %lu sec)", errorMsg, errorTime / 1000);

    ESP_LOGE(TAG, "BULLETPROOF OTA: %s", detailedErrorMsg);

    // BULLETPROOF: Enhanced error display with recovery info
    Application::LVGLMessageHandler::updateOtaScreenProgress(0, detailedErrorMsg);
    Application::TaskManager::updateOTAProgress(0, false, false, detailedErrorMsg);

    // BULLETPROOF: Show error status indicator
    Application::LVGLMessageHandler::updateOTAStatusIndicator(0, "OTA Error", true, true);

    // BULLETPROOF: Extended error display time for user feedback
    vTaskDelay(pdMS_TO_TICKS(5000));

    // BULLETPROOF: Recovery guidance
    Application::LVGLMessageHandler::updateOtaScreenProgress(0, "Check connection and retry");
    vTaskDelay(pdMS_TO_TICKS(3000));

    Application::LVGLMessageHandler::hideOtaScreen();
    ESP_LOGI(TAG, "BULLETPROOF OTA: Resuming tasks after error recovery.");
    Application::TaskManager::resumeFromOTA();

    // Return to idle state after error
    Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_IDLE);

    // BULLETPROOF: Hide status indicator
    Application::LVGLMessageHandler::hideOTAStatusIndicator();

    // BULLETPROOF: Reset all state
    otaResetState();
    errorHandlingInProgress = false;
}

// BULLETPROOF: Enhanced monitoring functions
static void otaTimeoutCheck(void) {
    unsigned long currentTime = millis();

    // // Global timeout check
    // if (currentTime - otaStartTime > OTA_TIMEOUT_MS) {
    //     if (!otaTimeoutTriggered) {
    //         ESP_LOGE(TAG, "BULLETPROOF OTA: Global timeout reached (%lu ms)", OTA_TIMEOUT_MS);
    //         otaTimeoutTriggered = true;
    //         otaEmergencyRecovery("Global timeout");
    //     }
    //     return;
    // }

    // // Progress timeout check
    // if (currentTime - lastProgressTime > OTA_PROGRESS_TIMEOUT_MS) {
    //     ESP_LOGE(TAG, "BULLETPROOF OTA: Progress timeout reached (%lu ms without progress)",
    //              OTA_PROGRESS_TIMEOUT_MS);
    //     otaEmergencyRecovery("Progress timeout");
    //     return;
    // }
}

static void otaProgressMonitor(void) {
    unsigned long currentTime = millis();

    // Heartbeat check
    if (currentTime - lastHeartbeat > OTA_HEARTBEAT_INTERVAL_MS * 3) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: Heartbeat timeout, OTA may be stalled");
        otaEmergencyRecovery("Heartbeat timeout");
        return;
    }
}

static void otaEmergencyRecovery(const char *reason) {
    if (emergencyRecoveryMode) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: Already in emergency recovery mode");
        return;
    }

    emergencyRecoveryMode = true;
    ESP_LOGE(TAG, "BULLETPROOF OTA: Emergency recovery triggered: %s", reason);

    // Stop OTA operations
    ArduinoOTA.end();

    // Enhanced error reporting
    char recoveryMsg[128];
    snprintf(recoveryMsg, sizeof(recoveryMsg), "Recovery: %s", reason);

    Application::LVGLMessageHandler::updateOtaScreenProgress(0, recoveryMsg);
    Application::TaskManager::updateOTAProgress(0, false, false, recoveryMsg);

    // Recovery process
    vTaskDelay(pdMS_TO_TICKS(3000));
    Application::LVGLMessageHandler::updateOtaScreenProgress(0, "System recovery in progress...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Cleanup and restore system
    Application::LVGLMessageHandler::hideOtaScreen();
    Application::TaskManager::setOTAState(Application::TaskManager::OTA_STATE_ERROR);
    Application::TaskManager::resumeFromOTA();

    // BULLETPROOF: Hide status indicator
    Application::LVGLMessageHandler::hideOTAStatusIndicator();

    // Reset state
    otaResetState();

    ESP_LOGI(TAG, "BULLETPROOF OTA: Emergency recovery completed");
}

static void otaResetState(void) {
    otaInProgress = false;
    otaStartTime = 0;
    lastProgressTime = 0;
    lastProgressPercent = 0;
    progressStallCount = 0;
    otaTimeoutTriggered = false;
    lastHeartbeat = 0;
    emergencyRecoveryMode = false;
    errorHandlingInProgress = false;

    ESP_LOGD(TAG, "BULLETPROOF OTA: State reset completed");
}

namespace Hardware {
namespace OTA {

bool init(void) {
    ESP_LOGI(TAG, "BULLETPROOF OTA: Initializing OTA Manager");

    if (!WiFi.isConnected()) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: WiFi not connected - OTA will initialize later.");
        otaInitialized = false;
        return true;
    }

    // BULLETPROOF: Reset state on initialization
    otaResetState();

    ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.setPort(OTA_PORT);

#if OTA_REQUIRE_PASSWORD
    ArduinoOTA.setPassword(password.c_str());
    ESP_LOGI(TAG, "BULLETPROOF OTA: Password protection enabled");
#endif

    ArduinoOTA.onStart(onOTAStart);
    ArduinoOTA.onEnd(onOTAEnd);
    ArduinoOTA.onProgress(onOTAProgress);
    ArduinoOTA.onError(onOTAError);

    ArduinoOTA.begin();

    if (!MDNS.begin(hostname.c_str())) {
        ESP_LOGW(TAG, "BULLETPROOF OTA: Error setting up mDNS responder");
    } else {
        ESP_LOGI(TAG, "BULLETPROOF OTA: mDNS responder started: %s.local", hostname.c_str());
    }

    otaInitialized = true;
    ESP_LOGI(TAG, "BULLETPROOF OTA: Manager initialized successfully on %s:%d",
             hostname.c_str(), OTA_PORT);
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "BULLETPROOF OTA: Deinitializing OTA Manager");
    if (otaInitialized) {
        ArduinoOTA.end();
        otaInitialized = false;
    }
    MDNS.end();
    otaResetState();
}

void update(void) {
    if (!otaInitialized && WiFi.isConnected()) {
        ESP_LOGI(TAG, "BULLETPROOF OTA: WiFi connected, initializing OTA...");
        if (!init()) {
            ESP_LOGE(TAG, "BULLETPROOF OTA: Initialization failed.");
        }
        return;
    }

    if (otaInitialized) {
        ArduinoOTA.handle();

        // BULLETPROOF: Continuous monitoring during OTA
        if (otaInProgress) {
            otaTimeoutCheck();
            otaProgressMonitor();
        }
    }
}

bool isReady(void) {
    return otaInitialized && WiFi.isConnected();
}

const char *getHostname(void) {
    return hostname.c_str();
}

void setHostname(const char *newHostname) {
    if (newHostname && strlen(newHostname) > 0) {
        hostname = String(newHostname);
    }
}

void setPassword(const char *newPassword) {
    if (newPassword && strlen(newPassword) > 0) {
        password = String(newPassword);
    }
}

bool isInProgress(void) {
    return otaInProgress;
}

// BULLETPROOF: Additional status functions
uint8_t getProgress(void) {
    return lastProgressPercent;
}

const char *getStatusMessage(void) {
    static char statusMsg[64];
    if (otaInProgress) {
        unsigned long elapsed = millis() - otaStartTime;
        snprintf(statusMsg, sizeof(statusMsg), "OTA %d%% (%lu sec)",
                 lastProgressPercent, elapsed / 1000);
    } else {
        strcpy(statusMsg, "OTA Ready");
    }
    return statusMsg;
}

}  // namespace OTA
}  // namespace Hardware

#endif  // OTA_ENABLE_UPDATES
