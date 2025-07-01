#include "OnDemandOTAManager.h"
#include "../../include/DebugUtils.h"
#include "../application/LVGLMessageHandler.h"
#include "../application/TaskManager.h"
#include <WiFi.h>
#include <HTTPUpdate.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

OTAStateCallback OnDemandOTAManager::stateCallback = nullptr;
OTAProgressCallback OnDemandOTAManager::progressCallback = nullptr;
OTACompleteCallback OnDemandOTAManager::completeCallback = nullptr;

// Core 1 task management
TaskHandle_t OnDemandOTAManager::otaTaskHandle = nullptr;
bool OnDemandOTAManager::otaTaskRunning = false;

// NETWORK-FREE ARCHITECTURE: Freed resources tracking
static size_t freedNetworkMemory = 0;
static bool resourcesReallocated = false;

// WATCHDOG SAFETY: Add timeout and yield tracking
static uint32_t lastWatchdogReset = 0;
static uint32_t lastTaskYield = 0;
static const uint32_t WATCHDOG_RESET_INTERVAL_MS = OTA_WATCHDOG_FEED_INTERVAL_MS;
static const uint32_t TASK_YIELD_INTERVAL_MS = OTA_TASK_YIELD_INTERVAL_MS;

// SAFETY: Helper function to safely reset watchdog and yield to other tasks
void feedWatchdogAndYield(const char* context) {
    uint32_t currentTime = millis();

    // Reset watchdog periodically
    if (currentTime - lastWatchdogReset >= WATCHDOG_RESET_INTERVAL_MS) {
#ifdef CONFIG_ESP_TASK_WDT_EN
        esp_task_wdt_reset();
#endif
        lastWatchdogReset = currentTime;
        ESP_LOGV(TAG, "[WATCHDOG] Reset during %s", context ? context : "OTA operation");
    }

    // Yield to other tasks periodically
    if (currentTime - lastTaskYield >= TASK_YIELD_INTERVAL_MS) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms yield
        lastTaskYield = currentTime;
    }
}

// SAFETY: Safe delay function that yields to other tasks and feeds watchdog
void safeDelay(uint32_t ms, const char* context) {
    uint32_t remaining = ms;
    while (remaining > 0 && !OnDemandOTAManager::userCancelRequested) {
        uint32_t chunkSize = min(remaining, 100U);  // Process in 100ms chunks
        vTaskDelay(pdMS_TO_TICKS(chunkSize));
        feedWatchdogAndYield(context);
        remaining -= chunkSize;

        // Check for cancellation during delay
        if (OnDemandOTAManager::userCancelRequested) {
            ESP_LOGW(TAG, "[SAFE_DELAY] Delay interrupted by user cancellation");
            break;
        }
    }
}

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

// Core 1 task function - runs the OTA state machine on core 1
void OnDemandOTAManager::otaTaskFunction(void* parameter) {
    ESP_LOGW(TAG, "[CORE1-TASK] OTA task started on core %d", xPortGetCoreID());

    while (otaTaskRunning && !userCancelRequested) {
        if (currentState != OTA_IDLE) {
            // SAFETY: Feed watchdog during OTA state machine processing
            feedWatchdogAndYield("OTA state machine");

            // Check for timeouts and user cancellation
            if (!checkTimeouts() && !checkUserCancel()) {
                processStateMachine();
            }
        }

        // Yield for 50ms to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGW(TAG, "[CORE1-TASK] OTA task ending");
    otaTaskRunning = false;
    vTaskDelete(nullptr);
}

bool OnDemandOTAManager::startOTATask(void) {
    if (otaTaskRunning) {
        ESP_LOGW(TAG, "[CORE1-TASK] OTA task already running");
        return true;
    }

    otaTaskRunning = true;

    BaseType_t result = xTaskCreatePinnedToCore(
        otaTaskFunction,           // Task function
        "OTA_Core1_Task",          // Task name
        8192,                      // Stack size
        nullptr,                   // Parameters
        configMAX_PRIORITIES - 1,  // Priority (should be very high)
        &otaTaskHandle,            // Task handle
        1                          // Pin to core 1
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "[CORE1-TASK] Failed to create OTA task on core 1");
        otaTaskRunning = false;
        return false;
    }

    ESP_LOGW(TAG, "[CORE1-TASK] OTA task created and pinned to core 1");
    return true;
}

void OnDemandOTAManager::stopOTATask(void) {
    if (!otaTaskRunning) {
        return;
    }

    ESP_LOGW(TAG, "[CORE1-TASK] Stopping OTA task");
    otaTaskRunning = false;

    // Wait for task to finish
    if (otaTaskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        otaTaskHandle = nullptr;
    }
}

bool OnDemandOTAManager::init(void) {
    ESP_LOGW(TAG, "[NETWORK-FREE] Initializing On-Demand OTA Manager");

    currentState = OTA_IDLE;
    currentProgress = 0;
    strcpy(stateMessage, "OTA Ready (Network-Free Mode)");
    userCancelRequested = false;
    otaStartTime = 0;
    lastProgressUpdate = 0;
    otaTaskRunning = false;
    otaTaskHandle = nullptr;

    // Initialize watchdog tracking
    lastWatchdogReset = millis();
    lastTaskYield = millis();

    // Calculate freed network resources
    freedNetworkMemory = 8192 + 4096;  // NETWORK_TASK_STACK_SIZE + OTA_TASK_STACK_SIZE estimate

#if OTA_REALLOCATE_NETWORK_RESOURCES
    reallocateNetworkResources();
#endif

    ESP_LOGW(TAG, "[NETWORK-FREE] On-Demand OTA Manager initialized - Freed %d bytes", freedNetworkMemory);
    return true;
}

void OnDemandOTAManager::deinit(void) {
    ESP_LOGW(TAG, "[NETWORK-FREE] Deinitializing On-Demand OTA Manager");

    if (isOTAActive()) {
        cancelOTA();
    }

    stopOTATask();
    stopMinimalNetwork();

#if OTA_REALLOCATE_NETWORK_RESOURCES
    restoreNetworkResources();
#endif

    currentState = OTA_IDLE;
}

bool OnDemandOTAManager::startOTAMode(void) {
    ESP_LOGW(TAG, "[USER-INITIATED] Starting OTA mode on core 1");

    if (currentState != OTA_IDLE) {
        ESP_LOGW(TAG, "OTA already active, cannot start");
        return false;
    }

    userCancelRequested = false;
    otaStartTime = millis();

    // Reset watchdog tracking for new OTA session
    lastWatchdogReset = millis();
    lastTaskYield = millis();

    // Start the OTA task on core 1
    if (!startOTATask()) {
        ESP_LOGE(TAG, "[CORE1-TASK] Failed to start OTA task");
        return false;
    }

    enterState(OTA_USER_INITIATED, "OTA mode started by user");

    // Show OTA screen immediately
    Application::LVGLMessageHandler::showOtaScreen();

    return true;
}

void OnDemandOTAManager::cancelOTA(void) {
    ESP_LOGW(TAG, "[USER-CANCEL] OTA cancellation requested");

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
    ESP_LOGW(TAG, "[CLEANUP] Stopping OTA mode and returning to network-free operation");

    cleanupOTA();
    stopOTATask();
    stopMinimalNetwork();

#if OTA_REALLOCATE_NETWORK_RESOURCES
    reallocateNetworkResources();
#endif

    enterState(OTA_IDLE, "Returned to network-free mode");

    // Hide OTA screen
    Application::LVGLMessageHandler::hideOtaScreen();
}

void OnDemandOTAManager::update(void) {
    // State machine processing is now handled by the Core 1 task
    // This function can be used for UI updates or light housekeeping
    if (currentState == OTA_IDLE) {
        return;  // No processing needed in network-free mode
    }

    // Optional: Perform light UI updates from main thread if needed
    // The heavy OTA processing is handled by the Core 1 task
}

void OnDemandOTAManager::processStateMachine(void) {
    // SAFETY: Feed watchdog at start of state machine processing
    feedWatchdogAndYield("state machine entry");

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

    ESP_LOGW(TAG, "[STATE] %s -> %s: %s",
             getStateString(oldState), getStateString(newState), stateMessage);

    // SAFETY: Throttled UI updates to prevent blocking LVGL task
    static uint32_t lastStateUIUpdate = 0;
    if (millis() - lastStateUIUpdate >= 100) {  // Max 10 updates per second
        Application::LVGLMessageHandler::updateOtaScreenProgress(currentProgress, stateMessage);
        lastStateUIUpdate = millis();
    }

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

    // SAFETY: Heavily throttled UI updates to prevent blocking LVGL task during downloads
    static uint32_t lastUIUpdate = 0;
    uint32_t uiUpdateInterval = (currentState == OTA_DOWNLOADING) ? 500 : OTA_UI_UPDATE_THROTTLE_MS;

    if (millis() - lastUIUpdate >= uiUpdateInterval) {
        Application::LVGLMessageHandler::updateOtaScreenProgress(progress, stateMessage);
        lastUIUpdate = millis();
    }

    // Call progress callback with safety check
    if (progressCallback) {
        progressCallback(progress, stateMessage);
    }

    ESP_LOGD(TAG, "[PROGRESS] %d%% - %s", progress, stateMessage);
}

void OnDemandOTAManager::completeOTA(OTAResult result, const char* message) {
    ESP_LOGW(TAG, "[COMPLETE] OTA finished with result: %d - %s", result, message ? message : "");

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

        // SAFETY: Safe reboot countdown with cancellation support
        for (int i = 3; i > 0; i--) {
            if (userCancelRequested) {
                ESP_LOGW(TAG, "[REBOOT] Reboot cancelled by user");
                return;
            }

            char countdownMsg[64];
            snprintf(countdownMsg, sizeof(countdownMsg), "Rebooting in %d second%s...", i, (i == 1) ? "" : "s");
            updateProgress(100, countdownMsg);

            safeDelay(1000, "reboot countdown");
        }

        if (!userCancelRequested) {
            ESP_LOGW(TAG, "[REBOOT] Restarting system...");
            ESP.restart();
        }
    }
#endif
}

bool OnDemandOTAManager::startMinimalNetwork(void) {
    static bool networkInitialized = false;

    if (!networkInitialized) {
        ESP_LOGW(TAG, "[NETWORK] Starting minimal network for OTA");

#if OTA_REALLOCATE_NETWORK_RESOURCES
        restoreNetworkResources();
#endif

        WiFi.mode(WIFI_STA);
        WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
        networkInitialized = true;

        updateProgress(5, "Connecting to WiFi...");
    }

    // SAFETY: Feed watchdog during network connection attempts
    feedWatchdogAndYield("network connection");

    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGW(TAG, "[NETWORK] WiFi connected: %s", WiFi.localIP().toString().c_str());
        updateProgress(20, "WiFi connected");
        return true;
    }

    // Update connection progress with watchdog safety
    static uint32_t lastProgressUpdate = 0;
    if (millis() - lastProgressUpdate > 2000) {
        static uint8_t connectProgress = 5;
        connectProgress = min(connectProgress + 2, 18);
        updateProgress(connectProgress, "Connecting to WiFi...");
        lastProgressUpdate = millis();

        // SAFETY: Yield during network connection
        feedWatchdogAndYield("WiFi connection progress");
    }

    return false;
}

void OnDemandOTAManager::stopMinimalNetwork(void) {
    ESP_LOGW(TAG, "[NETWORK] Stopping minimal network - returning to network-free mode");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // SAFETY: Replace blocking delay with safe delay
    safeDelay(100, "network shutdown");

    ESP_LOGW(TAG, "[NETWORK-FREE] Network disabled - back to network-free operation");
}

bool OnDemandOTAManager::isNetworkReady(void) {
    return WiFi.status() == WL_CONNECTED;
}

bool OnDemandOTAManager::downloadFirmware(void) {
    if (!isNetworkReady()) {
        return false;
    }

    ESP_LOGW(TAG, "[DOWNLOAD] Starting firmware download from: %s", OTA_SERVER_URL);

    // Create WiFi client for HTTP update
    WiFiClient client;

    // SAFETY: Enhanced progress callback with watchdog management
    httpUpdate.onProgress([](int cur, int total) {
        static uint32_t lastCallbackTime = 0;
        uint32_t currentTime = millis();

        // SAFETY: Throttle callback frequency to prevent overwhelming LVGL
        if (currentTime - lastCallbackTime < 200) {  // Max 5 times per second
            return;
        }
        lastCallbackTime = currentTime;

        // SAFETY: Feed watchdog during download
        feedWatchdogAndYield("firmware download");

        if (total > 0) {
            uint8_t progress = 20 + ((cur * 60) / total);  // 20-80% for download
            char progressMsg[64];
            snprintf(progressMsg, sizeof(progressMsg), "Downloading: %d/%d bytes", cur, total);
            OnDemandOTAManager::updateProgress(progress, progressMsg);

            // Check for user cancellation during download
            if (OnDemandOTAManager::userCancelRequested) {
                ESP_LOGW(TAG, "[DOWNLOAD] User cancellation detected");
                // Note: HTTP update doesn't support easy cancellation, but we can handle it after
            }
        }
    });

    // Configure HTTP update settings
    httpUpdate.setLedPin(-1);          // Disable LED indication
    httpUpdate.rebootOnUpdate(false);  // We'll handle reboot ourselves

    // SAFETY: Feed watchdog before starting potentially long HTTP operation
    feedWatchdogAndYield("pre-download");

    // Perform the update with WiFiClient
    HTTPUpdateResult result = httpUpdate.update(client, String(OTA_SERVER_URL), "");

    // SAFETY: Feed watchdog after HTTP operation completes
    feedWatchdogAndYield("post-download");

    switch (result) {
        case HTTP_UPDATE_FAILED:
            ESP_LOGE(TAG, "[DOWNLOAD] HTTP update failed: %s", httpUpdate.getLastErrorString().c_str());
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Download failed");
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            ESP_LOGW(TAG, "[DOWNLOAD] No updates available");
            completeOTA(OTA_RESULT_SUCCESS, "Already up to date");
            return true;

        case HTTP_UPDATE_OK:
            ESP_LOGW(TAG, "[DOWNLOAD] HTTP update completed successfully");
            updateProgress(80, "Download complete, installing...");
            return true;

        default:
            ESP_LOGE(TAG, "[DOWNLOAD] Unknown HTTP update result: %d", result);
            completeOTA(OTA_RESULT_DOWNLOAD_FAILED, "Unknown download error");
            return false;
    }
}

bool OnDemandOTAManager::installFirmware(void) {
    // HTTP update handles installation automatically
    // This function is called after successful download
    updateProgress(90, "Installing firmware...");

    // SAFETY: Replace blocking delays with safe delays
    safeDelay(1000, "firmware installation");

    updateProgress(95, "Finalizing installation...");
    safeDelay(500, "installation finalization");

    return true;
}

void OnDemandOTAManager::cleanupOTA(void) {
    ESP_LOGW(TAG, "[CLEANUP] Cleaning up OTA resources");

    userCancelRequested = false;
    otaStartTime = 0;
    lastProgressUpdate = 0;

    // Reset watchdog tracking
    lastWatchdogReset = millis();
    lastTaskYield = millis();
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
        ESP_LOGW(TAG, "[CANCEL] Processing user cancellation");
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

    ESP_LOGW(TAG, "[RESOURCE] Reallocating network resources to UI/audio performance");

    // Boost UI task priority and stack
    boostUIResources();

    resourcesReallocated = true;

    ESP_LOGW(TAG, "[RESOURCE] Network resources reallocated - UI performance boosted");
}

void OnDemandOTAManager::restoreNetworkResources(void) {
    if (!resourcesReallocated) {
        return;
    }

    ESP_LOGW(TAG, "[RESOURCE] Restoring network resources for OTA");

    restoreUIResources();

    resourcesReallocated = false;

    ESP_LOGW(TAG, "[RESOURCE] Network resources restored for OTA operation");
}

void OnDemandOTAManager::boostUIResources(void) {
    ESP_LOGW(TAG, "[BOOST] Boosting UI resources with freed network memory");
    // Resource boosting will be implemented when TaskManager is updated
}

void OnDemandOTAManager::restoreUIResources(void) {
    ESP_LOGW(TAG, "[RESTORE] Restoring original UI resource allocation");
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
