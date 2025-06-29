#ifndef ON_DEMAND_OTA_MANAGER_H
#define ON_DEMAND_OTA_MANAGER_H

#include "../../include/OTAConfig.h"
#include <Arduino.h>
#include <functional>

namespace Hardware {
namespace OnDemandOTA {

// OTA States for network-free architecture
enum OTAState {
    OTA_IDLE,                // No OTA activity, network disconnected
    OTA_USER_INITIATED,      // User clicked OTA button
    OTA_CONNECTING_NETWORK,  // Connecting to WiFi for OTA
    OTA_NETWORK_CONNECTED,   // WiFi connected, ready for OTA
    OTA_DOWNLOADING,         // Downloading firmware
    OTA_INSTALLING,          // Installing firmware
    OTA_SUCCESS,             // OTA completed successfully
    OTA_FAILED,              // OTA failed
    OTA_CANCELLED,           // User cancelled OTA
    OTA_CLEANUP              // Cleaning up network resources
};

// OTA Result codes
enum OTAResult {
    OTA_RESULT_SUCCESS,
    OTA_RESULT_NETWORK_FAILED,
    OTA_RESULT_DOWNLOAD_FAILED,
    OTA_RESULT_INSTALL_FAILED,
    OTA_RESULT_CANCELLED,
    OTA_RESULT_TIMEOUT
};

// Callback types for UI integration
typedef std::function<void(OTAState state, const char* message)> OTAStateCallback;
typedef std::function<void(uint8_t progress, const char* message)> OTAProgressCallback;
typedef std::function<void(OTAResult result, const char* message)> OTACompleteCallback;

// NETWORK-FREE ARCHITECTURE: On-Demand OTA Manager
class OnDemandOTAManager {
   public:
    // Initialization (does NOT start network)
    static bool init(void);
    static void deinit(void);

    // OTA Control - only starts network when requested
    static bool startOTAMode(void);  // User-initiated OTA mode
    static void cancelOTA(void);     // User cancels OTA
    static void stopOTAMode(void);   // Complete cleanup and return to normal

    // State and Progress
    static OTAState getCurrentState(void);
    static uint8_t getProgress(void);
    static const char* getStateMessage(void);
    static bool isOTAActive(void);
    static bool canCancel(void);

    // UI Callbacks
    static void setStateCallback(OTAStateCallback callback);
    static void setProgressCallback(OTAProgressCallback callback);
    static void setCompleteCallback(OTACompleteCallback callback);

    // Update function - only processes when OTA is active
    static void update(void);

    // Resource management for network-free architecture
    static void reallocateNetworkResources(void);  // Boost UI/audio with freed network resources
    static void restoreNetworkResources(void);     // Restore during OTA

    // Network-free status
    static bool isNetworkFree(void);     // True when no network activity
    static size_t getFreedMemory(void);  // Memory freed from network tasks

   private:
    static OTAState currentState;
    static uint8_t currentProgress;
    static char stateMessage[128];
    static uint32_t otaStartTime;
    static uint32_t lastProgressUpdate;
    static bool userCancelRequested;

    static OTAStateCallback stateCallback;
    static OTAProgressCallback progressCallback;
    static OTACompleteCallback completeCallback;

    // State machine functions
    static void processStateMachine(void);
    static void enterState(OTAState newState, const char* message = nullptr);
    static void updateProgress(uint8_t progress, const char* message = nullptr);
    static void completeOTA(OTAResult result, const char* message = nullptr);

    // Network management for OTA-only mode
    static bool startMinimalNetwork(void);
    static void stopMinimalNetwork(void);
    static bool isNetworkReady(void);

    // OTA process functions
    static bool downloadFirmware(void);
    static bool installFirmware(void);
    static void cleanupOTA(void);

    // Resource reallocation for performance boost
    static void boostUIResources(void);
    static void restoreUIResources(void);

    // Timeout and cancel checking
    static bool checkTimeouts(void);
    static bool checkUserCancel(void);
};

// Convenience functions for UI integration
bool initiateOTAFromUI(void);         // Called from UI button
void cancelOTAFromUI(void);           // Called from UI cancel button
const char* getOTAStatusForUI(void);  // Get current status string for UI
uint8_t getOTAProgressForUI(void);    // Get current progress for UI

}  // namespace OnDemandOTA
}  // namespace Hardware

#endif  // ON_DEMAND_OTA_MANAGER_H
