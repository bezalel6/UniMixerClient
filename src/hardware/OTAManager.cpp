#include "OTAManager.h"

#if OTA_ENABLE_UPDATES

#include "../application/LVGLMessageHandler.h"
#include <ESPmDNS.h>
#include <esp_log.h>
#include <ui/ui.h>

// Private variables
static const char *TAG = "OTAManager";
static bool otaInitialized = false;
static String hostname = OTA_HOSTNAME;
static String password = OTA_PASSWORD;
static bool otaInProgress = false;

namespace Hardware {
namespace OTA {

bool init(void) {
  ESP_LOGI(TAG, "Initializing OTA Manager");

  if (!WiFi.isConnected()) {
    ESP_LOGW(TAG, "WiFi not connected - OTA will initialize when connection is "
                  "available");
    // Don't fail initialization, just mark as not ready yet
    otaInitialized = false;
    return true; // Return success, OTA will initialize later when WiFi connects
  }

  // Configure Arduino OTA
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPort(OTA_PORT);

#if OTA_REQUIRE_PASSWORD
  ArduinoOTA.setPassword(password.c_str());
  ESP_LOGI(TAG, "OTA password protection enabled");
#endif

  // Set up Arduino OTA callbacks
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    ESP_LOGI(TAG, "Start updating %s", type.c_str());

    otaInProgress = true;

    // Send message to update UI instead of direct LVGL calls
    Application::LVGLMessageHandler::updateOTAProgress(0, true, false,
                                                       "Starting update...");
  });

  ArduinoOTA.onEnd([]() {
    ESP_LOGI(TAG, "OTA update completed successfully");

    // Update progress directly (single-threaded)
    if (ui_barOTAUpdateProgress) {
      lv_bar_set_value(ui_barOTAUpdateProgress, 100, LV_ANIM_OFF);
    }
    if (ui_lblOTAUpdateProgress) {
      lv_label_set_text(ui_lblOTAUpdateProgress,
                        "Update completed! Restarting...");
    }

    otaInProgress = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int progressPercent = (progress / (total / 100));
    static unsigned int lastProgressPercent = 0;

    if (progressPercent >= lastProgressPercent + 1 || progressPercent == 100) {
      ESP_LOGI(TAG, "OTA Progress: %u%%", progressPercent);

      // Update progress bar directly (single-threaded)
      if (otaInProgress) {
        if (ui_barOTAUpdateProgress) {
          lv_bar_set_value(ui_barOTAUpdateProgress, progressPercent,
                           LV_ANIM_OFF);
        }
        if (ui_lblOTAUpdateProgress) {
          char progressText[64];
          snprintf(progressText, sizeof(progressText), "Progress: %u%%",
                   progressPercent);
          lv_label_set_text(ui_lblOTAUpdateProgress, progressText);
        }
      }

      lastProgressPercent = progressPercent;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    ESP_LOGE(TAG, "OTA Error[%u]: ", error);
    const char *errorMsg = "Unknown error";

    if (error == OTA_AUTH_ERROR) {
      ESP_LOGE(TAG, "Auth Failed");
      errorMsg = "Authentication failed";
    } else if (error == OTA_BEGIN_ERROR) {
      ESP_LOGE(TAG, "Begin Failed");
      errorMsg = "Begin failed";
    } else if (error == OTA_CONNECT_ERROR) {
      ESP_LOGE(TAG, "Connect Failed");
      errorMsg = "Connection failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      ESP_LOGE(TAG, "Receive Failed");
      errorMsg = "Receive failed";
    } else if (error == OTA_END_ERROR) {
      ESP_LOGE(TAG, "End Failed");
      errorMsg = "End failed";
    }

    // Update error message directly (single-threaded)
    if (otaInProgress) {
      if (ui_barOTAUpdateProgress) {
        lv_bar_set_value(ui_barOTAUpdateProgress, 0, LV_ANIM_OFF);
      }
      if (ui_lblOTAUpdateProgress) {
        lv_label_set_text(ui_lblOTAUpdateProgress, errorMsg);
      }
    }

    otaInProgress = false;
  });

  ArduinoOTA.begin();

  // Set up mDNS
  if (!MDNS.begin(hostname.c_str())) {
    ESP_LOGW(TAG, "Error setting up MDNS responder");
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
  // If OTA is not initialized but WiFi is now connected, try to initialize
  if (!otaInitialized && WiFi.isConnected()) {
    ESP_LOGI(TAG, "WiFi now connected - initializing OTA");

    // Configure Arduino OTA
    ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.setPort(OTA_PORT);

#if OTA_REQUIRE_PASSWORD
    ArduinoOTA.setPassword(password.c_str());
    ESP_LOGI(TAG, "OTA password protection enabled");
#endif

    // Set up Arduino OTA callbacks (same as in init)
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {
        type = "filesystem";
      }
      ESP_LOGI(TAG, "Start updating %s", type.c_str());

      otaInProgress = true;

      // Send message to update UI instead of direct LVGL calls
      Application::LVGLMessageHandler::updateOTAProgress(0, true, false,
                                                         "Starting update...");
    });

    ArduinoOTA.onEnd([]() {
      ESP_LOGI(TAG, "OTA update completed successfully");

      // Send message to update UI instead of direct LVGL calls
      Application::LVGLMessageHandler::updateOTAProgress(
          100, false, true, "Update completed! Restarting...");

      otaInProgress = false;
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      unsigned int progressPercent = (progress / (total / 100));
      static unsigned int lastProgressPercent = 0;

      if (progressPercent >= lastProgressPercent + 1 ||
          progressPercent == 100) {
        ESP_LOGI(TAG, "OTA Progress: %u%%", progressPercent);

        if (otaInProgress) {
          char progressText[64];
          snprintf(progressText, sizeof(progressText), "Progress: %u%%",
                   progressPercent);

          // Send message to update UI instead of direct LVGL calls
          Application::LVGLMessageHandler::updateOTAProgress(
              progressPercent, true, false, progressText);
        }

        lastProgressPercent = progressPercent;
      }
    });

    ArduinoOTA.onError([](ota_error_t error) {
      ESP_LOGE(TAG, "OTA Error[%u]: ", error);
      const char *errorMsg = "Unknown error";

      if (error == OTA_AUTH_ERROR) {
        ESP_LOGE(TAG, "Auth Failed");
        errorMsg = "Authentication failed";
      } else if (error == OTA_BEGIN_ERROR) {
        ESP_LOGE(TAG, "Begin Failed");
        errorMsg = "Begin failed";
      } else if (error == OTA_CONNECT_ERROR) {
        ESP_LOGE(TAG, "Connect Failed");
        errorMsg = "Connection failed";
      } else if (error == OTA_RECEIVE_ERROR) {
        ESP_LOGE(TAG, "Receive Failed");
        errorMsg = "Receive failed";
      } else if (error == OTA_END_ERROR) {
        ESP_LOGE(TAG, "End Failed");
        errorMsg = "End failed";
      }

      if (otaInProgress) {
        // Send message to update UI instead of direct LVGL calls
        Application::LVGLMessageHandler::updateOTAProgress(0, false, false,
                                                           errorMsg);
      }

      otaInProgress = false;
    });

    ArduinoOTA.begin();

    // Set up mDNS
    if (!MDNS.begin(hostname.c_str())) {
      ESP_LOGW(TAG, "Error setting up MDNS responder");
    } else {
      ESP_LOGI(TAG, "mDNS responder started: %s.local", hostname.c_str());
    }

    otaInitialized = true;
    ESP_LOGI(TAG, "OTA Manager initialized successfully on %s:%d",
             hostname.c_str(), OTA_PORT);
  }

  // Handle OTA updates if initialized and connected
  if (otaInitialized && WiFi.isConnected()) {
    ArduinoOTA.handle();
  }
}

bool isReady(void) { return otaInitialized && WiFi.isConnected(); }

const char *getHostname(void) { return hostname.c_str(); }

void setHostname(const char *newHostname) {
  if (newHostname && strlen(newHostname) > 0) {
    hostname = String(newHostname);
    ESP_LOGI(TAG, "Hostname set to: %s", hostname.c_str());
  }
}

void setPassword(const char *newPassword) {
  if (newPassword && strlen(newPassword) > 0) {
    password = String(newPassword);
    ESP_LOGI(TAG, "OTA password updated");
  }
}

bool isInProgress(void) { return otaInProgress; }

} // namespace OTA
} // namespace Hardware

#endif // OTA_ENABLE_UPDATES