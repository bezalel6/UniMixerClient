#include "TaskManager.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include "../display/DisplayManager.h"
#include "../events/UiEventHandlers.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../messaging/MessageBus.h"
#include "AppController.h"
#include "AudioStatusManager.h"
#include "LVGLMessageHandler.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <ui/ui.h>

static const char *TAG = "TaskManager";

namespace Application {
namespace TaskManager {

// Task handles
TaskHandle_t lvglTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t messagingTaskHandle = NULL;
TaskHandle_t otaTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;

// Synchronization objects
SemaphoreHandle_t lvglMutex = NULL;
QueueHandle_t otaProgressQueue = NULL;

// Task state variables
static bool tasksRunning = false;
static OTAProgressData_t currentOTAProgress = {0, false, false, "Ready"};

bool init(void) {
  ESP_LOGI(TAG, "Initializing Task Manager for ESP32-S3 dual-core");

  // Initialize LVGL Message Handler
  if (!LVGLMessageHandler::init()) {
    ESP_LOGE(TAG, "Failed to initialize LVGL Message Handler");
    return false;
  }

  // Create OTA progress queue (kept for compatibility)
  otaProgressQueue = xQueueCreate(1, sizeof(OTAProgressData_t));
  if (otaProgressQueue == NULL) {
    ESP_LOGE(TAG, "Failed to create OTA progress queue");
    return false;
  }

  // Create LVGL task on Core 0 (highest priority)
  BaseType_t result = xTaskCreatePinnedToCore(
      lvglTask, "LVGL_Task", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY,
      &lvglTaskHandle, LVGL_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    return false;
  }

  // Create Network task on Core 1
  result = xTaskCreatePinnedToCore(
      networkTask, "Network_Task", NETWORK_TASK_STACK_SIZE, NULL,
      NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Network task");
    return false;
  }

  // Create Messaging task on Core 0
  result = xTaskCreatePinnedToCore(
      messagingTask, "Messaging_Task", MESSAGING_TASK_STACK_SIZE, NULL,
      MESSAGING_TASK_PRIORITY, &messagingTaskHandle, MESSAGING_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Messaging task");
    return false;
  }

#if OTA_ENABLE_UPDATES
  // Create OTA task on Core 1
  result =
      xTaskCreatePinnedToCore(otaTask, "OTA_Task", OTA_TASK_STACK_SIZE, NULL,
                              OTA_TASK_PRIORITY, &otaTaskHandle, OTA_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    return false;
  }
#endif

  // Create Audio task on Core 0
  result = xTaskCreatePinnedToCore(
      audioTask, "Audio_Task", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY,
      &audioTaskHandle, AUDIO_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Audio task");
    return false;
  }

  tasksRunning = true;
  ESP_LOGI(TAG, "All tasks created successfully with dual-core configuration");

  // Print initial task configuration
  printTaskStats();

  return true;
}

void deinit(void) {
  ESP_LOGI(TAG, "Deinitializing Task Manager");

  tasksRunning = false;

  // Wait a bit for tasks to finish their current operations
  vTaskDelay(pdMS_TO_TICKS(100));

  // Delete tasks
  if (lvglTaskHandle) {
    vTaskDelete(lvglTaskHandle);
    lvglTaskHandle = NULL;
  }
  if (networkTaskHandle) {
    vTaskDelete(networkTaskHandle);
    networkTaskHandle = NULL;
  }
  if (messagingTaskHandle) {
    vTaskDelete(messagingTaskHandle);
    messagingTaskHandle = NULL;
  }
  if (otaTaskHandle) {
    vTaskDelete(otaTaskHandle);
    otaTaskHandle = NULL;
  }
  if (audioTaskHandle) {
    vTaskDelete(audioTaskHandle);
    audioTaskHandle = NULL;
  }

  // Clean up synchronization objects
  if (lvglMutex) {
    vSemaphoreDelete(lvglMutex);
    lvglMutex = NULL;
  }
  if (otaProgressQueue) {
    vQueueDelete(otaProgressQueue);
    otaProgressQueue = NULL;
  }
}

void suspend(void) {
  if (tasksRunning) {
    if (lvglTaskHandle)
      vTaskSuspend(lvglTaskHandle);
    if (networkTaskHandle)
      vTaskSuspend(networkTaskHandle);
    if (messagingTaskHandle)
      vTaskSuspend(messagingTaskHandle);
    if (otaTaskHandle)
      vTaskSuspend(otaTaskHandle);
    if (audioTaskHandle)
      vTaskSuspend(audioTaskHandle);
  }
}

void resume(void) {
  if (tasksRunning) {
    if (lvglTaskHandle)
      vTaskResume(lvglTaskHandle);
    if (networkTaskHandle)
      vTaskResume(networkTaskHandle);
    if (messagingTaskHandle)
      vTaskResume(messagingTaskHandle);
    if (otaTaskHandle)
      vTaskResume(otaTaskHandle);
    if (audioTaskHandle)
      vTaskResume(audioTaskHandle);
  }
}

void lvglLock(void) {
  if (lvglMutex) {
    xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY);
  }
}

void lvglUnlock(void) {
  if (lvglMutex) {
    xSemaphoreGiveRecursive(lvglMutex);
  }
}

bool lvglTryLock(uint32_t timeoutMs) {
  if (lvglMutex) {
    return xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) ==
           pdTRUE;
  }
  return false;
}

void updateOTAProgress(uint8_t progress, bool inProgress, bool success,
                       const char *message) {
  OTAProgressData_t data;
  data.progress = progress;
  data.inProgress = inProgress;
  data.success = success;
  strncpy(data.message, message ? message : "", sizeof(data.message) - 1);
  data.message[sizeof(data.message) - 1] = '\0';

  // Update current progress (non-blocking)
  currentOTAProgress = data;

  // Send to queue (non-blocking)
  xQueueOverwrite(otaProgressQueue, &data);
}

bool getOTAProgress(OTAProgressData_t *data) {
  if (data == NULL)
    return false;

  // Try to get latest from queue
  if (xQueuePeek(otaProgressQueue, data, 0) == pdTRUE) {
    return true;
  }

  // Fall back to current progress
  *data = currentOTAProgress;
  return true;
}

// LVGL Task - Core 0, Highest Priority
void lvglTask(void *parameter) {
  ESP_LOGI(TAG, "LVGL Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastLedUpdate = 0;

  while (tasksRunning) {
    // Update LVGL tick system first (critical for animations)
    Display::tickUpdate();
    // Update display frame counter for accurate FPS
    Display::tick();

    // Handle LVGL tasks and rendering - this MUST run every cycle for smooth
    // animations The LVGLMessageHandler processes queue messages via LVGL timer
    lv_timer_handler();

    // Do less frequent operations to keep animation smooth
    unsigned long currentTime = millis();

    // Update display stats only every 200ms (reduced frequency for performance)
    if (currentTime - lastDisplayUpdate >= 200) {
      Display::update();
      lastDisplayUpdate = currentTime;
    }

    // Update LED colors less frequently (every 500ms)
#ifdef BOARD_HAS_RGB_LED
    if (currentTime - lastLedUpdate >= 500) {
      Hardware::Device::ledCycleColors();
      lastLedUpdate = currentTime;
    }
#endif

    // Sleep for exactly 16ms to maintain 60fps animation timing
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(LVGL_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "LVGL Task ended");
  vTaskDelete(NULL);
}

// Network Task - Core 1, Medium-High Priority
void networkTask(void *parameter) {
  ESP_LOGI(TAG, "Network Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (tasksRunning) {
    // Update network status
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 ||    \
    OTA_ENABLE_UPDATES
    Hardware::Network::update();

    bool connected = Hardware::Network::isConnected();
    const char *status = Hardware::Network::getWifiStatusString();
    const char *ssid = Hardware::Network::getSsid();
    const char *ip = Hardware::Network::getIpAddress();

    // Send UI update messages instead of direct LVGL calls
    LVGLMessageHandler::updateWifiStatus(status, connected);
    LVGLMessageHandler::updateNetworkInfo(ssid, ip);
#endif

    // Sleep until next update
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(NETWORK_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "Network Task ended");
  vTaskDelete(NULL);
}

// Messaging Task - Core 0, High Priority
void messagingTask(void *parameter) {
  ESP_LOGI(TAG, "Messaging Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (tasksRunning) {
    // Update message bus (high frequency for responsiveness)
    Messaging::MessageBus::Update();

    // Sleep until next update
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MESSAGING_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "Messaging Task ended");
  vTaskDelete(NULL);
}

#if OTA_ENABLE_UPDATES
// OTA Task - Core 1, Medium Priority
void otaTask(void *parameter) {
  ESP_LOGI(TAG, "OTA Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (tasksRunning) {
    // Only check for OTA updates if WiFi is connected
    if (Hardware::Network::isConnected()) {
      Hardware::OTA::update();
    }

    // Sleep until next update
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(OTA_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "OTA Task ended");
  vTaskDelete(NULL);
}
#endif

// Audio Task - Core 0, Medium-High Priority
void audioTask(void *parameter) {
  ESP_LOGI(TAG, "Audio Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();
  static unsigned long lastFpsUpdate = 0;

  while (tasksRunning) {
    // Yield immediately to prevent watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));

    // Update FPS display regularly for monitoring
    unsigned long currentTime = millis();
    if (currentTime - lastFpsUpdate >= 1000) { // Every second
      ESP_LOGD(TAG, "Audio task: Updating FPS display");

      // Get actual FPS from display manager
      float currentFps = Display::getFPS();
      LVGLMessageHandler::updateFpsDisplay(currentFps);
      lastFpsUpdate = currentTime;
    }

    // Re-enable audio status updates
    static unsigned long lastAudioUpdate = 0;
    if (currentTime - lastAudioUpdate >= 2000) { // Every 2 seconds only
      ESP_LOGD(TAG, "Audio task: Processing audio status");
      Application::Audio::StatusManager::onAudioLevelsChangedUI();
      lastAudioUpdate = currentTime;
    }

    // Sleep until next update with much longer interval
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(AUDIO_UPDATE_INTERVAL));

// Reset watchdog to prevent timeout
#ifdef CONFIG_ESP_TASK_WDT_EN
    esp_task_wdt_reset();
#endif

    // Additional debug logging to monitor task health
    static int heartbeat = 0;
    if (++heartbeat % 10 == 0) {
      ESP_LOGI(TAG, "Audio task heartbeat: %d", heartbeat);
    }
  }

  ESP_LOGI(TAG, "Audio Task ended");
  vTaskDelete(NULL);
}

void printTaskStats(void) {
  ESP_LOGI(TAG, "=== Task Configuration ===");
  ESP_LOGI(TAG, "LVGL Task:     Core %d, Priority %d, Stack %d bytes",
           LVGL_TASK_CORE, LVGL_TASK_PRIORITY, LVGL_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "Network Task:  Core %d, Priority %d, Stack %d bytes",
           NETWORK_TASK_CORE, NETWORK_TASK_PRIORITY, NETWORK_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "Messaging Task: Core %d, Priority %d, Stack %d bytes",
           MESSAGING_TASK_CORE, MESSAGING_TASK_PRIORITY,
           MESSAGING_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "OTA Task:      Core %d, Priority %d, Stack %d bytes",
           OTA_TASK_CORE, OTA_TASK_PRIORITY, OTA_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "Audio Task:    Core %d, Priority %d, Stack %d bytes",
           AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, AUDIO_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "=========================");
}

uint32_t getLvglTaskHighWaterMark(void) {
  if (lvglTaskHandle) {
    return uxTaskGetStackHighWaterMark(lvglTaskHandle);
  }
  return 0;
}

uint32_t getNetworkTaskHighWaterMark(void) {
  if (networkTaskHandle) {
    return uxTaskGetStackHighWaterMark(networkTaskHandle);
  }
  return 0;
}

} // namespace TaskManager
} // namespace Application