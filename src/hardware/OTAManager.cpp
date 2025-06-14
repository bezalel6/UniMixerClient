#include "OTAManager.h"

#if OTA_ENABLE_UPDATES

#include "../application/LVGLMessageHandler.h"
#include "../application/TaskManager.h"
#include "../display/DisplayManager.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_log.h>
#include <functional>
#include <ui/ui.h>

// Private variables
static const char *TAG = "OTAManager";
static bool otaInitialized = false;
static String hostname = OTA_HOSTNAME;
static String password = OTA_PASSWORD;
static bool otaInProgress = false;

// OTA callback functions
static std::function<void()> onOTAStart = []() {
  String type;
  if (ArduinoOTA.getCommand() == U_FLASH) {
    type = "sketch";
  } else {
    type = "filesystem";
  }
  ESP_LOGI(TAG, "Start updating %s", type.c_str());

  otaInProgress = true;

  // Use message handler for thread-safe UI updates
  Application::LVGLMessageHandler::updateOTAProgress(0, true, false,
                                                     "Starting update...");
};

static std::function<void()> onOTAEnd = []() {
  ESP_LOGI(TAG, "OTA update completed successfully");

  // Use message handler for thread-safe UI updates only
  Application::LVGLMessageHandler::updateOTAProgress(
      100, false, true, "Update completed! Restarting...");

  otaInProgress = false;
};

static std::function<void(unsigned int, unsigned int)> onOTAProgress =
    [](unsigned int progress, unsigned int total) {
      unsigned int progressPercent = (progress / (total / 100));
      static unsigned int lastProgressPercent = 0;

      if (progressPercent >= lastProgressPercent + 1 ||
          progressPercent == 100) {
        ESP_LOGI(TAG, "OTA Progress: %u%%", progressPercent);

        // Use message handler for thread-safe UI updates only
        if (otaInProgress) {
          char progressText[64];
          snprintf(progressText, sizeof(progressText), "Progress: %u%%",
                   progressPercent);
          Application::LVGLMessageHandler::updateOTAProgress(
              progressPercent, true, false, progressText);
          // Manually update LVGL to show progress, but in a thread-safe way
          if (Application::TaskManager::lvglTryLock(10)) {
            lv_timer_handler();
            Display::tickUpdate();
            Application::TaskManager::lvglUnlock();
          }
          vTaskDelay(
              pdMS_TO_TICKS(5)); // Add small delay to prevent watchdog timeout
        }

        lastProgressPercent = progressPercent;
      }
    };

static std::function<void(ota_error_t)> onOTAError = [](ota_error_t error) {
  ESP_LOGE(TAG, "OTA Error[%u]: ", error);

  // OTA_BEGIN_ERROR is sometimes reported erroneously, but the update can
  // still succeed. We will log it as a warning and allow the update to
  // continue.
  if (error == OTA_BEGIN_ERROR) {
    ESP_LOGW(TAG, "Begin Failed (non-fatal), allowing update to continue...");
    // Do not set otaInProgress to false or send a failure message.
    return;
  }

  const char *errorMsg = "Unknown error";
  if (error == OTA_AUTH_ERROR) {
    ESP_LOGE(TAG, "Auth Failed");
    errorMsg = "Authentication failed";
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

  // For all other errors, treat them as fatal.
  if (otaInProgress) {
    Application::LVGLMessageHandler::updateOTAProgress(0, false, false,
                                                       errorMsg);
  }

  otaInProgress = false;
};

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

  // Set up Arduino OTA callbacks with thread-safe UI updates
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    ESP_LOGI(TAG, "Start updating %s", type.c_str());

    otaInProgress = true;

    // Use message handler for thread-safe UI updates
    Application::LVGLMessageHandler::updateOTAProgress(0, true, false,
                                                       "Starting update...");
  });
  ArduinoOTA.onEnd([]() {
    ESP_LOGI(TAG, "OTA update completed successfully");

    // Use message handler for thread-safe UI updates only
    Application::LVGLMessageHandler::updateOTAProgress(
        100, false, true, "Update completed! Restarting...");

    otaInProgress = false;
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int progressPercent = (progress / (total / 100));
    static unsigned int lastProgressPercent = 0;

    if (progressPercent >= lastProgressPercent + 1 || progressPercent == 100) {
      ESP_LOGI(TAG, "OTA Progress: %u%%", progressPercent);

      // Use message handler for thread-safe UI updates only
      if (otaInProgress) {
        char progressText[64];
        snprintf(progressText, sizeof(progressText), "Progress: %u%%",
                 progressPercent);
        Application::LVGLMessageHandler::updateOTAProgress(
            progressPercent, true, false, progressText);
        // Manually update LVGL to show progress, but in a thread-safe way
        if (Application::TaskManager::lvglTryLock(10)) {
          lv_timer_handler();
          Display::tickUpdate();
          Application::TaskManager::lvglUnlock();
        }
        vTaskDelay(
            pdMS_TO_TICKS(5)); // Add small delay to prevent watchdog timeout
      }

      lastProgressPercent = progressPercent;
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ESP_LOGE(TAG, "OTA Error[%u]: ", error);

    // OTA_BEGIN_ERROR is sometimes reported erroneously, but the update can
    // still succeed. We will log it as a warning and allow the update to
    // continue.
    if (error == OTA_BEGIN_ERROR) {
      ESP_LOGW(TAG, "Begin Failed (non-fatal), allowing update to continue...");
      // Do not set otaInProgress to false or send a failure message.
      return;
    }

    const char *errorMsg = "Unknown error";
    if (error == OTA_AUTH_ERROR) {
      ESP_LOGE(TAG, "Auth Failed");
      errorMsg = "Authentication failed";
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

    // For all other errors, treat them as fatal.
    if (otaInProgress) {
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

    // Call the full initialization function to avoid code duplication
    if (init()) {
      ESP_LOGI(TAG, "OTA initialization on WiFi connection successful");
    } else {
      ESP_LOGW(TAG, "OTA initialization on WiFi connection failed");
    }
    return;
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