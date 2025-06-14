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

static void onOTAStart() {
  String type;
  if (ArduinoOTA.getCommand() == U_FLASH) {
    type = "sketch";
  } else {
    type = "filesystem";
  }
  ESP_LOGI(TAG, "Start updating %s", type.c_str());

  otaInProgress = true;
  Application::TaskManager::suspendForOTA();
  Application::LVGLMessageHandler::showOtaScreen();
}

static void onOTAEnd() {
  ESP_LOGI(TAG, "OTA update completed successfully");
  Application::LVGLMessageHandler::updateOtaScreenProgress(
      100, "Update complete! Restarting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  Application::LVGLMessageHandler::hideOtaScreen();
  Application::TaskManager::resumeFromOTA();
  otaInProgress = false;
}

static void onOTAProgress(unsigned int progress, unsigned int total) {
  uint8_t percentage = (progress / (total / 100));
  char msg[64];
  snprintf(msg, sizeof(msg), "Updating: %d%%", percentage);
  Application::LVGLMessageHandler::updateOtaScreenProgress(percentage, msg);
}

static void onOTAError(ota_error_t error) {
  if (errorHandlingInProgress) {
    ESP_LOGW(TAG,
             "OTA error handler already running, ignoring subsequent error.");
    return;
  }

  // The "already running" error can sometimes be ignored, allowing the update
  // to proceed.
  if (error == OTA_BEGIN_ERROR) {
    ESP_LOGW(TAG, "Non-fatal OTA Error (ignored): OTA_BEGIN_ERROR. Update will "
                  "continue.");
    return;
  }

  errorHandlingInProgress = true;

  ESP_LOGE(TAG, "OTA Error[%u]: ", error);

  // Do not call Update.abort() here.
  // ArduinoOTA.end() will handle the necessary cleanup.

  // Stop the OTA service completely to terminate the connection
  ArduinoOTA.end();

  const char *errorMsg = "Unknown error";
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

  Application::LVGLMessageHandler::updateOtaScreenProgress(0, errorMsg);
  vTaskDelay(pdMS_TO_TICKS(3000));
  Application::LVGLMessageHandler::hideOtaScreen();
  ESP_LOGI(TAG, "Resuming tasks after OTA error.");
  Application::TaskManager::resumeFromOTA();
  otaInProgress = false;
  errorHandlingInProgress = false;
}

namespace Hardware {
namespace OTA {

bool init(void) {
  ESP_LOGI(TAG, "Initializing OTA Manager");

  if (!WiFi.isConnected()) {
    ESP_LOGW(TAG, "WiFi not connected - OTA will initialize later.");
    otaInitialized = false;
    return true;
  }

  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPort(OTA_PORT);

#if OTA_REQUIRE_PASSWORD
  ArduinoOTA.setPassword(password.c_str());
#endif

  ArduinoOTA.onStart(onOTAStart);
  ArduinoOTA.onEnd(onOTAEnd);
  ArduinoOTA.onProgress(onOTAProgress);
  ArduinoOTA.onError(onOTAError);

  ArduinoOTA.begin();

  if (!MDNS.begin(hostname.c_str())) {
    ESP_LOGW(TAG, "Error setting up mDNS responder");
  } else {
    ESP_LOGI(TAG, "mDNS responder started: %s.local", hostname.c_str());
  }

  otaInitialized = true;
  ESP_LOGI(TAG, "OTA Manager initialized successfully on %s:%d",
           hostname.c_str(), OTA_PORT);
  return true;
}

void deinit(void) {
  ESP_LOGI(TAG, "Deinitializing OTA Manager");
  if (otaInitialized) {
    ArduinoOTA.end();
    otaInitialized = false;
  }
  MDNS.end();
}

void update(void) {
  if (!otaInitialized && WiFi.isConnected()) {
    ESP_LOGI(TAG, "WiFi connected, initializing OTA...");
    if (!init()) {
      ESP_LOGE(TAG, "OTA initialization failed.");
    }
    return;
  }

  if (otaInitialized) {
    ArduinoOTA.handle();
  }
}

bool isReady(void) { return otaInitialized && WiFi.isConnected(); }
const char *getHostname(void) { return hostname.c_str(); }

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

bool isInProgress(void) { return otaInProgress; }

} // namespace OTA
} // namespace Hardware

#endif // OTA_ENABLE_UPDATES