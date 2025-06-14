#include "OTAManager.h"

#if OTA_ENABLE_UPDATES

#include "../application/TaskManager.h"
#include "../display/DisplayManager.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_log.h>
#include <functional>
#include <lvgl.h>
#include <ui/ui.h>

// Private variables
static const char *TAG = "OTAManager";
static bool otaInitialized = false;
static String hostname = OTA_HOSTNAME;
static String password = OTA_PASSWORD;
static bool otaInProgress = false;

// OTA screen elements
static lv_obj_t *ota_screen = NULL;
static lv_obj_t *ota_label = NULL;
static lv_obj_t *ota_bar = NULL;

// Helper function forward declarations (for callbacks)
static void show_ota_screen(const char *initial_message);
static void hide_ota_screen();
static void update_ota_screen(int progress, const char *message);

// OTA callback functions
static void onOTAStart() {
  String type;
  if (ArduinoOTA.getCommand() == U_FLASH) {
    type = "sketch";
  } else {
    type = "filesystem";
  }
  ESP_LOGI(TAG, "Start updating %s", type.c_str());

  otaInProgress = true;
  Application::TaskManager::suspend();
  show_ota_screen("Starting update...");
}

static void onOTAEnd() {
  ESP_LOGI(TAG, "OTA update completed successfully");
  update_ota_screen(100, "Update complete! Restarting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  hide_ota_screen();
  Application::TaskManager::resume();
  otaInProgress = false;
}

static void onOTAProgress(unsigned int progress, unsigned int total) {
  unsigned int progressPercent = (total > 0) ? (progress / (total / 100)) : 0;
  static unsigned int lastProgressPercent = 0;

  if (progressPercent > lastProgressPercent || progressPercent == 100) {
    ESP_LOGI(TAG, "OTA Progress: %u%%", progressPercent);
    char progressText[64];
    snprintf(progressText, sizeof(progressText), "Progress: %u%%",
             progressPercent);
    update_ota_screen(progressPercent, progressText);
    lastProgressPercent = progressPercent;
  }
}

static void onOTAError(ota_error_t error) {
  ESP_LOGE(TAG, "OTA Error[%u]: ", error);
  const char *errorMsg = "Unknown error";
  switch (error) {
  case OTA_AUTH_ERROR:
    errorMsg = "Authentication failed";
    break;
  case OTA_BEGIN_ERROR:
    ESP_LOGW(TAG, "Begin Failed (non-fatal), ignoring.");
    return; // Not a fatal error, so we just return.
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

  update_ota_screen(0, errorMsg);
  vTaskDelay(pdMS_TO_TICKS(3000));
  hide_ota_screen();
  Application::TaskManager::resume();
  otaInProgress = false;
}

// Helper function implementations
static void show_ota_screen(const char *initial_message) {
  if (Application::TaskManager::lvglTryLock(100)) {
    if (!ota_screen) {
      ota_screen = lv_obj_create(NULL);
      lv_obj_set_style_bg_color(ota_screen, lv_color_hex(0x000000), 0);

      ota_label = lv_label_create(ota_screen);
      lv_label_set_text(ota_label, initial_message);
      lv_obj_set_style_text_color(ota_label, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_26, 0);
      lv_obj_align(ota_label, LV_ALIGN_CENTER, 0, -20);

      ota_bar = lv_bar_create(ota_screen);
      lv_obj_set_size(ota_bar, 200, 20);
      lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 20);
      lv_bar_set_value(ota_bar, 0, LV_ANIM_OFF);
    }
    lv_scr_load(ota_screen);
    Application::TaskManager::lvglUnlock();
  }
}

static void hide_ota_screen() {
  if (Application::TaskManager::lvglTryLock(100)) {
    if (ota_screen) {
      lv_obj_del(ota_screen);
      ota_screen = NULL;
      ota_label = NULL;
      ota_bar = NULL;
    }
    lv_scr_load(ui_screenMain); // Restore main screen
    Application::TaskManager::lvglUnlock();
  }
}

static void update_ota_screen(int progress, const char *message) {
  if (Application::TaskManager::lvglTryLock(10)) {
    if (ota_label) {
      lv_label_set_text(ota_label, message);
    }
    if (ota_bar) {
      lv_bar_set_value(ota_bar, progress, LV_ANIM_ON);
    }
    lv_timer_handler(); // Manually update the screen
    Application::TaskManager::lvglUnlock();
  }
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