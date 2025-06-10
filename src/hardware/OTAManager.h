#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "../../include/OTAConfig.h"

#if OTA_ENABLE_UPDATES

#include <ArduinoOTA.h>
#include <WiFi.h>

namespace Hardware {
namespace OTA {

// OTA manager functions
bool init(void);
void deinit(void);
void update(void);

// Status query functions
bool isReady(void);
bool isInProgress(void);
const char* getHostname(void);

// Configuration functions
void setHostname(const char* hostname);
void setPassword(const char* password);

}  // namespace OTA
}  // namespace Hardware

#endif  // OTA_ENABLE_UPDATES

#endif  // OTA_MANAGER_H