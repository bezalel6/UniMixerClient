#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "OTAConfig.h"
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <functional>

namespace Hardware {
namespace OTA {

// =============================================================================
// OTA STATES - Unified State Machine
// =============================================================================

enum OTAState {
    OTA_IDLE,            // No OTA activity, network-free mode
    OTA_USER_INITIATED,  // User requested OTA via UI
    OTA_CONNECTING,      // Connecting to WiFi for OTA
    OTA_CONNECTED,       // WiFi connected, ready for download
    OTA_DOWNLOADING,     // Downloading firmware via HTTPUpdate
    OTA_INSTALLING,      // Installing firmware (handled by HTTPUpdate)
    OTA_SUCCESS,         // OTA completed successfully
    OTA_FAILED,          // OTA failed with error
    OTA_CANCELLED,       // User cancelled OTA
    OTA_CLEANUP          // Cleaning up resources
};

enum OTAResult {
    OTA_RESULT_SUCCESS,
    OTA_RESULT_NETWORK_FAILED,
    OTA_RESULT_DOWNLOAD_FAILED,
    OTA_RESULT_INSTALL_FAILED,
    OTA_RESULT_CANCELLED,
    OTA_RESULT_TIMEOUT,
    OTA_RESULT_UNKNOWN_ERROR
};

// =============================================================================
// UI-FIRST CALLBACK SYSTEM
// =============================================================================

typedef std::function<void(OTAState state, const char* message)> OTAStateCallback;
typedef std::function<void(uint8_t progress, const char* message)> OTAProgressCallback;
typedef std::function<void(OTAResult result, const char* message)> OTACompleteCallback;

// =============================================================================
// UNIFIED OTA MANAGER - HTTPUpdate + Bulletproof Monitoring
// =============================================================================

class OTAManager {
   public:
    // Core lifecycle
    static bool init(void);
    static void deinit(void);
    static void update(void);

    // OTA control - UI-initiated
    static bool startOTA(void);   // User clicks OTA button
    static void cancelOTA(void);  // User cancels OTA
    static bool isActive(void);   // Check if OTA is running

    // State and progress - UI queries
    static OTAState getCurrentState(void);
    static uint8_t getProgress(void);
    static const char* getStateMessage(void);
    static bool canCancel(void);

    // UI callback registration
    static void setStateCallback(OTAStateCallback callback);
    static void setProgressCallback(OTAProgressCallback callback);
    static void setCompleteCallback(OTACompleteCallback callback);

    // Network-free architecture support
    static bool isNetworkFree(void);
    static size_t getFreedMemory(void);

    // Network status functions (for components that need network info)
    static bool isNetworkConnected(void);
    static const char* getNetworkStatusString(void);
    static const char* getIpAddress(void);
    static int getSignalStrength(void);

   private:
    // =============================================================================
    // INTERNAL STATE MANAGEMENT
    // =============================================================================

    static OTAState currentState;
    static uint8_t currentProgress;
    static char stateMessage[128];
    static uint32_t otaStartTime;
    static bool userCancelRequested;

    // UI callbacks
    static OTAStateCallback stateCallback;
    static OTAProgressCallback progressCallback;
    static OTACompleteCallback completeCallback;

    // =============================================================================
    // BULLETPROOF MONITORING (from old OTAManager)
    // =============================================================================

    static uint32_t lastProgressTime;
    static uint8_t lastProgressPercent;
    static int progressStallCount;
    static uint32_t lastHeartbeat;
    static bool emergencyMode;

    // =============================================================================
    // WATCHDOG SAFETY (from OnDemandOTAManager)
    // =============================================================================

    static uint32_t lastWatchdogReset;
    static uint32_t lastTaskYield;

    // =============================================================================
    // CORE STATE MACHINE
    // =============================================================================

    static void processStateMachine(void);
    static void enterState(OTAState newState, const char* message = nullptr);
    static void updateProgress(uint8_t progress, const char* message = nullptr);
    static void completeOTA(OTAResult result, const char* message = nullptr);

    // =============================================================================
    // NETWORK MANAGEMENT - Minimal for OTA only
    // =============================================================================

    static bool startNetwork(void);
    static void stopNetwork(void);
    static bool isNetworkReady(void);

    // =============================================================================
    // HTTP UPDATE IMPLEMENTATION
    // =============================================================================

    static bool downloadAndInstall(void);
    static void setupHTTPUpdateCallbacks(void);
    static void onHTTPUpdateProgress(int current, int total);

    // =============================================================================
    // BULLETPROOF MONITORING FUNCTIONS
    // =============================================================================

    static bool checkTimeouts(void);
    static bool checkProgressStalls(void);
    static bool checkUserCancel(void);
    static void emergencyRecovery(const char* reason);
    static void resetMonitoring(void);

    // =============================================================================
    // WATCHDOG SAFETY FUNCTIONS
    // =============================================================================

    static void feedWatchdogAndYield(const char* context);
    static void safeDelay(uint32_t ms, const char* context);

    // =============================================================================
    // UTILITY FUNCTIONS
    // =============================================================================

    static const char* getStateString(OTAState state);
    static void cleanup(void);
};

// =============================================================================
// UI CONVENIENCE FUNCTIONS
// =============================================================================

// Simple UI integration functions
bool initiateOTAFromUI(void);         // UI button handler
void cancelOTAFromUI(void);           // UI cancel handler
const char* getOTAStatusForUI(void);  // UI status display
uint8_t getOTAProgressForUI(void);    // UI progress bar

}  // namespace OTA
}  // namespace Hardware

#endif  // OTA_MANAGER_H
