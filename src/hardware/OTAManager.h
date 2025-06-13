#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <functional>
#include "../../include/OTAConfig.h"

namespace Hardware {
namespace OTA {

// OTA callback types
typedef std::function<void(uint8_t progress)> OTAProgressCallback;
typedef std::function<void(const char* message)> OTAStatusCallback;
typedef std::function<void(bool success, const char* message)> OTACompleteCallback;

// OTA Manager functions
bool init(void);
void deinit(void);
void update(void);

// Status functions
bool isInProgress(void);
uint8_t getProgress(void);
const char* getStatusMessage(void);

// Callback registration
void setProgressCallback(OTAProgressCallback callback);
void setStatusCallback(OTAStatusCallback callback);
void setCompleteCallback(OTACompleteCallback callback);

// Manual OTA control
void startOTA(void);
void stopOTA(void);

// Enhanced OTA with UI integration
class EnhancedOTAManager {
public:
    static bool initialize(void);
    static void handleOTA(void);
    static void setUIProgressCallback(OTAProgressCallback progressCb, 
                                     OTAStatusCallback statusCb, 
                                     OTACompleteCallback completeCb);
    
    // Progress monitoring
    static bool isOTAActive(void);
    static uint8_t getCurrentProgress(void);
    static const char* getCurrentStatus(void);
    
    // OTA control
    static void enableOTA(bool enable);
    static bool isOTAEnabled(void);
    
private:
    static OTAProgressCallback progressCallback;
    static OTAStatusCallback statusCallback;
    static OTACompleteCallback completeCallback;
    static bool otaInProgress;
    static uint8_t currentProgress;
    static char statusMessage[64];
    static bool otaEnabled;
    
    static void onOTAStart(void);
    static void onOTAProgress(unsigned int progress, unsigned int total);
    static void onOTAEnd(void);
    static void onOTAError(ota_error_t error);
};

}  // namespace OTA
}  // namespace Hardware

#endif  // OTA_MANAGER_H