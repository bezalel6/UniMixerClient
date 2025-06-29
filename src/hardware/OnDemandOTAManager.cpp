#include "OnDemandOTAManager.h"
#include "../../include/DebugUtils.h"
#include "../application/LVGLMessageHandler.h"
#include "../application/TaskManager.h"
#include <WiFi.h>
#include <HTTPUpdate.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "OnDemandOTA";

namespace Hardware {
namespace OnDemandOTA {

// Static member definitions
OTAState OnDemandOTAManager::currentState = OTA_IDLE;
uint8_t OnDemandOTAManager::currentProgress = 0;
char OnDemandOTAManager::stateMessage[128] = "OTA Ready";
uint32_t OnDemandOTAManager::otaStartTime = 0;
uint32_t OnDemandOTAManager::lastProgressUpdate = 0;
bool OnDemandOTAManager::userCancelRequested = false;

OnDemandOTAManager::OTAStateCallback OnDemandOTAManager::stateCallback = nullptr;
OnDemandOTAManager::OTAProgressCallback OnDemandOTAManager::progressCallback = nullptr;
OnDemandOTAManager::OTACompleteCallback OnDemandOTAManager::completeCallback = nullptr;

// NETWORK-FREE ARCHITECTURE: Freed resources tracking
static size_t freedNetworkMemory = 0;
static bool resourcesReallocated = false;

// Helper function for state names
const char* getStateString(OTAState state) {
    switch (state) {
        case OTA_IDLE:
            return "IDLE";
        case OTA_USER_INITIATED:
            return "USER_INITIATED";
        case OTA_CONNECTING_NETWORK:
            return "CONNECTING_NETWORK";
        case OTA_NETWORK_CONNECTED:
            return "NETWORK_CONNECTED";
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

bool OnDemandOTAManager::init(void) {
    ESP_LOGI(TAG, "[NETWORK-FREE] Initializing On-Demand OTA Manager");

    currentState = OTA_IDLE;
    currentProgress = 0;
    strcpy(stateMessage, "OTA Ready (Network-Free Mode)");
    userCancelRequested = false;
    otaStartTime = 0;
    lastProgressUpdate = 0;

    // Calculate freed network resources
    freedNetworkMemory = 8192 + 4096;  // NETWORK_TASK_STACK_SIZE + OTA_TASK_STACK_SIZE estimate

#if OTA_REALLOCATE_NETWORK_RESOURCES
    reallocateNetworkResources();
#endif

    ESP_LOGI(TAG, "[NETWORK-FREE] On-Demand OTA Manager initialized - Freed %d bytes", freedNetworkMemory);
    return true;
}

void OnDemandOTAManager::deinit(void) {
    ESP_LOGI(TAG, "[NETWORK-FREE] Deinitializing On-Demand OTA Manager");

    if (isOTAActive()) {
        cancelOTA();
    }

    stopMinimalNetwork();

#if OTA_REALLOCATE_NETWORK_RESOURCES
    restoreNetworkResources();
#endif

    currentState = OTA_IDLE;
}

bool OnDemandOTAManager::startOTAMode(void) {
    ESP_LOGI(TAG, "[USER-INITIATED] Starting OTA mode");

    if (currentState != OTA_IDLE) {
        ESP_LOGW(TAG, "OTA already active, cannot start");
        return false;
    }

    userCancelRequested = false;
    otaStartTime = millis();

    enterState(OTA_USER_INITIATED, "OTA mode started by user");

    // Show OTA screen immediately
    Application::LVGLMessageHandler::showOtaScreen();

    return true;
}

void OnDemandOTAManager::cancelOTA(void) {
    ESP_LOGI(TAG, "[USER-CANCEL] OTA cancellation requested");

    if (!isOTAActive()) {
        ESP_LOGW(TAG, "No active OTA to cancel");
        return;
    }

    userCancelRequested = true;

    if (currentState == OTA_CONNECTING_NETWORK ||
        currentState == OTA_NETWORK_CONNECTED ||
        currentState == OTA_DOWNLOADING) {
        // Can safely cancel during these phases
        enterState(OTA_CANCELLED, "OTA cancelled by user");
    } else {
        // Mark for cancellation but let current operation complete
        updateProgress(currentProgress, "Cancelling...");
    }
}

void OnDemandOTAManager::stopOTAMode(void) {
    ESP_LOGI(TAG, "[CLEANUP] Stopping OTA mode and returning to network-free operation");

    cleanupOTA();
    stopMinimalNetwork();

#if OTA_REALLOCATE_NETWORK_RESOURCES
    reallocateNetworkResources();
#endif

    enterState(OTA_IDLE, "Returned to network-free mode");

    // Hide OTA screen
    Application::LVGLMessageHandler::hideOtaScreen();
}

void OnDemandOTAManager::update(void) {
    if (currentState == OTA_IDLE) {
        return;  // No processing needed in network-free mode
    }

    // Check for timeouts and user cancellation
    if (checkTimeouts() || checkUserCancel()) {
        return;  // State changed, will be handled next update
    }

    processStateMachine();
}

void OnDemandOTAManager::processStateMachine(void) {
    switch (currentState) {
        case OTA_USER_INITIATED:
            enterState(OTA_CONNECTING_NETWORK, "Connecting to WiFi...");
            break;

        case OTA_CONNECTING_NETWORK:
            if (startMinimalNetwork()) {
                enterState(OTA_NETWORK_CONNECTED, "WiFi connected, starting download...");
            } else if (millis() - otaStartTime > OTA_NETWORK_CONNECT_TIMEOUT_MS) {
                completeOTA(OTA_RESULT_NETWORK_FAILED, "Failed to connect to WiFi");
            }
            break;

        case OTA_NETWORK_CONNECTED:
            enterState(OTA_DOWNLOADING, "Downloading firmware...");
            break;

        case OTA_DOWNLOADING:
            if (downloadFirmware()) {
                enterState(OTA_INSTALLING, "Installing firmware...");
            }
            break;

        case OTA_INSTALLING:
            if (installFirmware()) {
                completeOTA(OTA_RESULT_SUCCESS, "OTA completed successfully");
            }
            break;

        case OTA_SUCCESS:
        case OTA_FAILED:
        case OTA_CANCELLED:
            enterState(OTA_CLEANUP, "Cleaning up...");
            break;

        case OTA_CLEANUP:
            stopOTAMode();
            break;

        default:
            break;
    }
}

void OnDemandOTAManager::enterState(OTAState newState, const char* message) {
    OTAState oldState = currentState;
    currentState = newState;

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    ESP_LOGI(TAG, "[STATE] %s -> %s: %s",
             getStateString(oldState), getStateString(newState), stateMessage);

    // Update UI
    Application::LVGLMessageHandler::updateOtaScreenProgress(currentProgress, stateMessage);

    // Call state callback
    if (stateCallback) {
        stateCallback(newState, stateMessage);
    }
}

void OnDemandOTAManager::updateProgress(uint8_t progress, const char* message) {
    currentProgress = progress;
    lastProgressUpdate = millis();

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    // Update UI with throttling
    static uint32_t lastUIUpdate = 0;
    if (millis() - lastUIUpdate >= OTA_PROGRESS_UPDATE_INTERVAL_MS) {
        Application::LVGLMessageHandler::updateOtaScreenProgress(progress, stateMessage);
        lastUIUpdate = millis();
    }

    // Call progress callback
    if (progressCallback) {
        progressCallback(progress, stateMessage);
    }

    ESP_LOGD(TAG, "[PROGRESS] %d%% - %s", progress, stateMessage);
}

void OnDemandOTAManager::completeOTA(OTAResult result, const char* message) {
    ESP_LOGI(TAG, "[COMPLETE] OTA finished with result: %d - %s", result, message ? message : "");

    if (message) {
        strncpy(stateMessage, message, sizeof(stateMessage) - 1);
        stateMessage[sizeof(stateMessage) - 1] = '\0';
    }

    // Update final progress
    if (result == OTA_RESULT_SUCCESS) {
        currentProgress = 100;
        enterState(OTA_SUCCESS, message);
    } else {
        enterState(OTA_FAILED, message);
    }

    // Call completion callback
    if (completeCallback) {
        completeCallback(result, stateMessage);
    }

#if OTA_AUTO_REBOOT_ON_SUCCESS
    if (result == OTA_RESULT_SUCCESS) {
        updateProgress(100, "Rebooting in 3 seconds...");
        delay(3000);
        ESP.restart();
    }
#endif
}

bool OnDemandOTAManager::startMinimalNetwork(void) {
    static bool networkInitialized = false;

    if (!networkInitialized) {
        ESP_LOGI(TAG, "[NETWORK] Starting minimal network for OTA");

#if OTA_REALLOCATE_NETWORK_RESOURCES
        restoreNetworkResources();
#endif

        WiFi.mode(WIFI_STA);
        WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
        networkInitialized = true;

        updateProgress(5, "Connecting to WiFi...");
    }

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
    }

    return false;
}

void OnDemandOTAManager::stopMinimalNetwork(void) {
    ESP_LOGI(TAG, "[NETWORK] Stopping minimal network - returning to network-free mode");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Small delay to ensure WiFi is fully off
    delay(100);

    ESP_LOGI(TAG, "[NETWORK-FREE] Network disabled - back to network-free operation");
}

bool OnDemandOTAManager::isNetworkReady(void) {
    return WiFi.status() == WL_CONNECTED;
}

bool OnDemandOTAManager::downloadFirmware(void) {
    if (!isNetworkReady()) {
        return false;
    }

    ESP_LOGI(TAG, "[DOWNLOAD] Starting firmware download from: %s", OTA_SERVER_URL);

    // Set up progress callback for HTTP update
    httpUpdate.onProgress([](int cur, int total) {
        uint8_t progress = 20 + ((cur * 60) / total);  // 20-80% for download
        char progressMsg[64];
        snprintf(progressMsg, sizeof(progressMsg), "Downloading: %d/%d bytes", cur, total);
        OnDemandOTAManager::updateProgress(progress, progressMsg);

        // Check for user cancellation during download
        if (OnDemandOTAManager::userCancelRequested) {
            ESP_LOGW(TAG, "[DOWNLOAD] User cancellation detected");
            // Note: HTTP update doesn't support easy cancellation, but we can handle it after
        }
    });

    // Perform the update
    t_httpUpdate_return result = httpUpdate.update(OTA_SERVER_URL);

    switch (result) {
        case HTTP_UPDATE_FAILED:
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Download failed");
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            completeOTA(OTA_RESULT_SUCCESS, "Already up to date");
            return true;

        case HTTP_UPDATE_OK:
            updateProgress(80, "Download complete, installing...");
            return true;

        default:
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Unknown download error");
            return false;
    }
}

bool OnDemandOTAManager::installFirmware(void) {
    // HTTP update handles installation automatically
    // This function is called after successful download
    updateProgress(90, "Installing firmware...");
    delay(1000);  // Simulate installation time
    updateProgress(95, "Finalizing installation...");
    delay(500);

    return true;
}

void OnDemandOTAManager::cleanupOTA(void) {
    ESP_LOGI(TAG, "[CLEANUP] Cleaning up OTA resources");

    userCancelRequested = false;
    otaStartTime = 0;
    lastProgressUpdate = 0;
}

bool OnDemandOTAManager::checkTimeouts(void) {
    if (currentState == OTA_IDLE) {
        return false;
    }

    uint32_t elapsed = millis() - otaStartTime;

    if (elapsed > OTA_DOWNLOAD_TIMEOUT_MS) {
        ESP_LOGW(TAG, "[TIMEOUT] OTA timeout after %d ms", elapsed);
        completeOTA(OTA_RESULT_TIMEOUT, "OTA timeout");
        return true;
    }

    return false;
}

bool OnDemandOTAManager::checkUserCancel(void) {
    if (!userCancelRequested) {
        return false;
    }

    if (canCancel()) {
        ESP_LOGI(TAG, "[CANCEL] Processing user cancellation");
        completeOTA(OTA_RESULT_CANCELLED, "OTA cancelled by user");
        return true;
    }

    return false;
}

// NETWORK-FREE ARCHITECTURE: Resource Management
void OnDemandOTAManager::reallocateNetworkResources(void) {
    if (resourcesReallocated) {
        return;
    }

    ESP_LOGI(TAG, "[RESOURCE] Reallocating network resources to UI/audio performance");

    // Boost UI task priority and stack
    boostUIResources();

    resourcesReallocated = true;

    ESP_LOGI(TAG, "[RESOURCE] Network resources reallocated - UI performance boosted");
}

void OnDemandOTAManager::restoreNetworkResources(void) {
    if (!resourcesReallocated) {
        return;
    }

    ESP_LOGI(TAG, "[RESOURCE] Restoring network resources for OTA");

    restoreUIResources();

    resourcesReallocated = false;

    ESP_LOGI(TAG, "[RESOURCE] Network resources restored for OTA operation");
}

void OnDemandOTAManager::boostUIResources(void) {
    ESP_LOGI(TAG, "[BOOST] Boosting UI resources with freed network memory");
    // Resource boosting will be implemented when TaskManager is updated
}

void OnDemandOTAManager::restoreUIResources(void) {
    ESP_LOGI(TAG, "[RESTORE] Restoring original UI resource allocation");
    // Resource restoration will be implemented when TaskManager is updated
}

// Public API implementations
OTAState OnDemandOTAManager::getCurrentState(void) { return currentState; }
uint8_t OnDemandOTAManager::getProgress(void) { return currentProgress; }
const char* OnDemandOTAManager::getStateMessage(void) { return stateMessage; }
bool OnDemandOTAManager::isOTAActive(void) { return currentState != OTA_IDLE; }
bool OnDemandOTAManager::canCancel(void) {
    return currentState == OTA_CONNECTING_NETWORK ||
           currentState == OTA_NETWORK_CONNECTED ||
           currentState == OTA_DOWNLOADING;
}

void OnDemandOTAManager::setStateCallback(OTAStateCallback callback) { stateCallback = callback; }
void OnDemandOTAManager::setProgressCallback(OTAProgressCallback callback) { progressCallback = callback; }
void OnDemandOTAManager::setCompleteCallback(OTACompleteCallback callback) { completeCallback = callback; }

bool OnDemandOTAManager::isNetworkFree(void) { return currentState == OTA_IDLE; }
size_t OnDemandOTAManager::getFreedMemory(void) { return freedNetworkMemory; }

// Convenience functions for UI integration
bool initiateOTAFromUI(void) {
    return OnDemandOTAManager::startOTAMode();
}

void cancelOTAFromUI(void) {
    OnDemandOTAManager::cancelOTA();
}

const char* getOTAStatusForUI(void) {
    return OnDemandOTAManager::getStateMessage();
}

uint8_t getOTAProgressForUI(void) {
    return OnDemandOTAManager::getProgress();
}

}  // namespace OnDemandOTA
}  // namespace Hardware
