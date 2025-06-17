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
  ESP_LOGI(TAG, "[INIT] Starting Task Manager initialization for ESP32-S3 dual-core");

  tasksRunning = true;
  // Create LVGL mutex for thread safety
  ESP_LOGI(TAG, "[INIT] Creating LVGL mutex...");
  lvglMutex = xSemaphoreCreateRecursiveMutex();
  if (lvglMutex == NULL) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create LVGL mutex");
    return false;
  }
  ESP_LOGI(TAG, "[INIT] LVGL mutex created successfully");

  // Initialize LVGL Message Handler
  ESP_LOGI(TAG, "[INIT] Initializing LVGL Message Handler...");
  if (!LVGLMessageHandler::init()) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to initialize LVGL Message Handler");
    return false;
  }
  ESP_LOGI(TAG, "[INIT] LVGL Message Handler initialized successfully");

  // Create OTA progress queue (kept for compatibility)
  ESP_LOGI(TAG, "[INIT] Creating OTA progress queue...");
  otaProgressQueue = xQueueCreate(1, sizeof(OTAProgressData_t));
  if (otaProgressQueue == NULL) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create OTA progress queue");
    return false;
  }
  ESP_LOGI(TAG, "[INIT] OTA progress queue created successfully");

  // Create LVGL task on Core 0 (highest priority)
  ESP_LOGI(TAG, "[INIT] Creating LVGL task on Core %d with priority %d...", LVGL_TASK_CORE, LVGL_TASK_PRIORITY);
  BaseType_t result = xTaskCreatePinnedToCore(
      lvglTask, "LVGL_Task", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY,
      &lvglTaskHandle, LVGL_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create LVGL task - result: %d", result);
    return false;
  }
  ESP_LOGI(TAG, "[INIT] LVGL task created successfully");

  // Create Network task on Core 1
  ESP_LOGI(TAG, "[INIT] Creating Network task on Core %d with priority %d...", NETWORK_TASK_CORE, NETWORK_TASK_PRIORITY);
  result = xTaskCreatePinnedToCore(
      networkTask, "Network_Task", NETWORK_TASK_STACK_SIZE, NULL,
      NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create Network task - result: %d", result);
    return false;
  }
  ESP_LOGI(TAG, "[INIT] Network task created successfully");

  // Create Messaging task on Core 0
  ESP_LOGI(TAG, "[INIT] Creating Messaging task on Core %d with priority %d...", MESSAGING_TASK_CORE, MESSAGING_TASK_PRIORITY);
  result = xTaskCreatePinnedToCore(
      messagingTask, "Messaging_Task", MESSAGING_TASK_STACK_SIZE, NULL,
      MESSAGING_TASK_PRIORITY, &messagingTaskHandle, MESSAGING_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create Messaging task - result: %d", result);
    return false;
  }
  ESP_LOGI(TAG, "[INIT] Messaging task created successfully");

#if OTA_ENABLE_UPDATES
  // Create OTA task on Core 1
  ESP_LOGI(TAG, "[INIT] Creating OTA task on Core %d with priority %d...", OTA_TASK_CORE, OTA_TASK_PRIORITY);
  result =
      xTaskCreatePinnedToCore(otaTask, "OTA_Task", OTA_TASK_STACK_SIZE, NULL,
                              OTA_TASK_PRIORITY, &otaTaskHandle, OTA_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create OTA task - result: %d", result);
    return false;
  }
  ESP_LOGI(TAG, "[INIT] OTA task created successfully");
#else
  ESP_LOGI(TAG, "[INIT] OTA updates disabled - skipping OTA task creation");
#endif

  // Create Audio task on Core 0
  ESP_LOGI(TAG, "[INIT] Creating Audio task on Core %d with priority %d...", AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY);
  result = xTaskCreatePinnedToCore(
      audioTask, "Audio_Task", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY,
      &audioTaskHandle, AUDIO_TASK_CORE);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[INIT] CRITICAL: Failed to create Audio task - result: %d", result);
    return false;
  }
  ESP_LOGI(TAG, "[INIT] Audio task created successfully");

  ESP_LOGI(TAG, "[INIT] SUCCESS: All tasks created successfully with dual-core configuration");

  // Print initial task configuration
  printTaskStats();

  ESP_LOGI(TAG, "[INIT] Task Manager initialization completed successfully");
  return true;
}

void deinit(void) {
  ESP_LOGI(TAG, "[DEINIT] Starting Task Manager deinitialization");

  tasksRunning = false;

  // Wait a bit for tasks to finish their current operations
  vTaskDelay(pdMS_TO_TICKS(100));

  // Delete tasks
  if (lvglTaskHandle) {
    ESP_LOGI(TAG, "[DEINIT] Deleting LVGL task");
    vTaskDelete(lvglTaskHandle);
    lvglTaskHandle = NULL;
  }
  if (networkTaskHandle) {
    ESP_LOGI(TAG, "[DEINIT] Deleting Network task");
    vTaskDelete(networkTaskHandle);
    networkTaskHandle = NULL;
  }
  if (messagingTaskHandle) {
    ESP_LOGI(TAG, "[DEINIT] Deleting Messaging task");
    vTaskDelete(messagingTaskHandle);
    messagingTaskHandle = NULL;
  }
  if (otaTaskHandle) {
    ESP_LOGI(TAG, "[DEINIT] Deleting OTA task");
    vTaskDelete(otaTaskHandle);
    otaTaskHandle = NULL;
  }
  if (audioTaskHandle) {
    ESP_LOGI(TAG, "[DEINIT] Deleting Audio task");
    vTaskDelete(audioTaskHandle);
    audioTaskHandle = NULL;
  }

  // Clean up synchronization objects
  if (lvglMutex) {
    ESP_LOGI(TAG, "[DEINIT] Deleting LVGL mutex");
    vSemaphoreDelete(lvglMutex);
    lvglMutex = NULL;
  }
  if (otaProgressQueue) {
    ESP_LOGI(TAG, "[DEINIT] Deleting OTA progress queue");
    vQueueDelete(otaProgressQueue);
    otaProgressQueue = NULL;
  }
  
  ESP_LOGI(TAG, "[DEINIT] Task Manager deinitialization completed");
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

void suspendForOTA(void) {
  if (tasksRunning) {
    ESP_LOGI(TAG, "[OTA] Suspending tasks for OTA update...");
    // DO NOT suspend networkTaskHandle, it's running the OTA process.
    // DO NOT suspend otaTaskHandle, it's also part of the OTA process.
    // DO NOT suspend lvglTaskHandle, it's updating the screen.

    if (messagingTaskHandle != NULL &&
        eTaskGetState(messagingTaskHandle) != eSuspended) {
      ESP_LOGI(TAG, "[OTA] Suspending Messaging_Task...");
      vTaskSuspend(messagingTaskHandle);
    }
    if (audioTaskHandle != NULL &&
        eTaskGetState(audioTaskHandle) != eSuspended) {
      ESP_LOGI(TAG, "[OTA] Suspending Audio_Task...");
      vTaskSuspend(audioTaskHandle);
    }
    ESP_LOGI(TAG, "[OTA] Finished suspending tasks for OTA.");
  }else{
    ESP_LOGW(TAG, "[OTA] Cannot suspend tasks - tasks not running");
  }
}

void resumeFromOTA(void) {
  if (tasksRunning) {
    ESP_LOGI(TAG, "[OTA] Resuming tasks after OTA update...");
    // Network, OTA, and LVGL tasks were not suspended.

    if (messagingTaskHandle != NULL &&
        eTaskGetState(messagingTaskHandle) == eSuspended) {
      ESP_LOGI(TAG, "[OTA] Resuming Messaging_Task...");
      vTaskResume(messagingTaskHandle);
    }
    if (audioTaskHandle != NULL &&
        eTaskGetState(audioTaskHandle) == eSuspended) {
      ESP_LOGI(TAG, "[OTA] Resuming Audio_Task...");
      vTaskResume(audioTaskHandle);
    }
    ESP_LOGI(TAG, "[OTA] Finished resuming tasks after OTA.");
  }else{
    ESP_LOGW(TAG, "[OTA] Cannot resume tasks - tasks not running");
  }
}

void lvglLock(void) {
  if (lvglMutex) {
    if (xSemaphoreTakeRecursive(lvglMutex, portMAX_DELAY) != pdTRUE) {
      ESP_LOGE(TAG, "[MUTEX] CRITICAL: Failed to acquire LVGL mutex");
    }
  }else{
    ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL");
  }
}

void lvglUnlock(void) {
  if (lvglMutex) {
    if (xSemaphoreGiveRecursive(lvglMutex) != pdTRUE) {
      ESP_LOGE(TAG, "[MUTEX] CRITICAL: Failed to release LVGL mutex");
    }
  }else{
    ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL during unlock");
  }
}

bool lvglTryLock(uint32_t timeoutMs) {
  if (lvglMutex) {
    bool result = xSemaphoreTakeRecursive(lvglMutex, pdMS_TO_TICKS(timeoutMs)) ==
           pdTRUE;
    if (!result) {
      ESP_LOGE(TAG, "[MUTEX] WARNING: Failed to acquire LVGL mutex within %d ms", timeoutMs);
    }
    return result;
  }
  ESP_LOGE(TAG, "[MUTEX] CRITICAL: LVGL mutex is NULL during tryLock");
  return false;
}

void updateOTAProgress(uint8_t progress, bool inProgress, bool success,
                       const char *message) {
  if (!otaProgressQueue) {
    ESP_LOGE(TAG, "[OTA] CRITICAL: OTA progress queue is NULL");
    return;
  }

  OTAProgressData_t data;
  data.progress = progress;
  data.inProgress = inProgress;
  data.success = success;
  strncpy(data.message, message ? message : "", sizeof(data.message) - 1);
  data.message[sizeof(data.message) - 1] = '\0';

  // Update current progress (non-blocking)
  currentOTAProgress = data;

  // Send to queue (non-blocking)
  if (xQueueOverwrite(otaProgressQueue, &data) != pdTRUE) {
    ESP_LOGE(TAG, "[OTA] WARNING: Failed to update OTA progress queue");
  }
}

bool getOTAProgress(OTAProgressData_t *data) {
  if (data == NULL) {
    ESP_LOGE(TAG, "[OTA] CRITICAL: NULL data pointer passed to getOTAProgress");
    return false;
  }

  if (!otaProgressQueue) {
    ESP_LOGE(TAG, "[OTA] CRITICAL: OTA progress queue is NULL");
    *data = currentOTAProgress;
    return true;
  }

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
  ESP_LOGI(TAG, "[LVGL_TASK] LVGL Task started on Core %d", xPortGetCoreID());

  // CRITICAL: Wait for display hardware to fully stabilize before starting LVGL operations
  // This prevents race conditions when logging level is ERROR (no debug delays)
  ESP_LOGI(TAG, "[LVGL_TASK] Waiting for display hardware stabilization...");
  
  // Progressive stability check with increasing delays
  for (int attempt = 0; attempt < 5; attempt++) {
    vTaskDelay(pdMS_TO_TICKS(200 + (attempt * 100))); // 200ms, 300ms, 400ms, 500ms, 600ms
    
    // Verify LVGL default display is available and ready
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != NULL) {
      ESP_LOGI(TAG, "[LVGL_TASK] Display found on attempt %d, verifying stability...", attempt + 1);
      
      // Additional small delay to ensure SPI/display controller is settled
      vTaskDelay(pdMS_TO_TICKS(100));
      
      // Test basic LVGL operation safety
      if (!disp->rendering_in_progress) {
        ESP_LOGI(TAG, "[LVGL_TASK] Display hardware confirmed stable after %d attempts", attempt + 1);
        break;
      }
    }
    
    if (attempt == 4) {
      ESP_LOGW(TAG, "[LVGL_TASK] WARNING: Display stability check failed after 5 attempts, proceeding anyway");
    }
  }

  // Additional safety delay for ERROR log level (no debug timing delays)
  ESP_LOGI(TAG, "[LVGL_TASK] Applying final stability delay for ERROR log level...");
  vTaskDelay(pdMS_TO_TICKS(300));
  
  ESP_LOGI(TAG, "[LVGL_TASK] Starting main LVGL operations loop");

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
    lvglLock();
    lv_timer_handler();
    lvglUnlock();

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

  ESP_LOGI(TAG, "[LVGL_TASK] LVGL Task ended");
  vTaskDelete(NULL);
}

// Network Task - Core 1, Medium-High Priority
void networkTask(void *parameter) {
  ESP_LOGI(TAG, "[NETWORK_TASK] Network Task started on Core %d", xPortGetCoreID());

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

  ESP_LOGI(TAG, "[NETWORK_TASK] Network Task ended");
  vTaskDelete(NULL);
}

// Messaging Task - Core 0, High Priority
void messagingTask(void *parameter) {
  ESP_LOGI(TAG, "[MESSAGING_TASK] Messaging Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (tasksRunning) {
    // Update message bus (high frequency for responsiveness)
    Messaging::MessageBus::Update();

    // Sleep until next update
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MESSAGING_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "[MESSAGING_TASK] Messaging Task ended");
  vTaskDelete(NULL);
}

#if OTA_ENABLE_UPDATES
// OTA Task - Core 1, Medium Priority
void otaTask(void *parameter) {
  ESP_LOGI(TAG, "[OTA_TASK] OTA Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (tasksRunning) {
    // Check for OTA updates or re-initialize if needed
    Hardware::OTA::update();

    // Sleep until next update
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(OTA_UPDATE_INTERVAL));
  }

  ESP_LOGI(TAG, "[OTA_TASK] OTA Task ended");
  vTaskDelete(NULL);
}
#endif

// Audio Task - Core 0, Medium-High Priority
void audioTask(void *parameter) {
  ESP_LOGI(TAG, "[AUDIO_TASK] Audio Task started on Core %d", xPortGetCoreID());

  TickType_t lastWakeTime = xTaskGetTickCount();
  static unsigned long lastFpsUpdate = 0;

  while (tasksRunning) {
    // Yield immediately to prevent watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));

    // Update FPS display less frequently for monitoring
    unsigned long currentTime = millis();
    if (currentTime - lastFpsUpdate >= 2000) { // Every 2 seconds
      // ESP_LOGD(TAG, "Audio task: Updating FPS display");

      // Get actual FPS from display manager
      float currentFps = Display::getFPS();
      LVGLMessageHandler::updateFpsDisplay(currentFps);
      lastFpsUpdate = currentTime;
    }

    // Re-enable audio status updates
    static unsigned long lastAudioUpdate = 0;
    if (currentTime - lastAudioUpdate >= 500) { // Every 500ms
      // ESP_LOGD(TAG, "Audio task: Processing audio status");
      
      // Reset watchdog before potentially long-running UI operation
#ifdef CONFIG_ESP_TASK_WDT_EN
      esp_task_wdt_reset();
#endif

      // Use LVGL mutex protection for thread-safe UI updates
      lvglLock();
      Application::Audio::StatusManager::onAudioLevelsChangedUI();
      lvglUnlock();
      
      // Reset watchdog after UI operation
#ifdef CONFIG_ESP_TASK_WDT_EN
      esp_task_wdt_reset();
#endif
      
      lastAudioUpdate = currentTime;
    }

    // Sleep until next update with much longer interval
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(AUDIO_UPDATE_INTERVAL));

    // Additional debug logging to monitor task health
    static int heartbeat = 0;
    if (++heartbeat % 10 == 0) {
      // ESP_LOGI(TAG, "Audio task heartbeat: %d", heartbeat);
    }
  }

  ESP_LOGI(TAG, "[AUDIO_TASK] Audio Task ended");
  vTaskDelete(NULL);
}

void printTaskStats(void) {
  ESP_LOGI(TAG, "[STATS] === Task Configuration ===");
  ESP_LOGI(TAG, "[STATS] LVGL Task:     Core %d, Priority %d, Stack %d bytes",
           LVGL_TASK_CORE, LVGL_TASK_PRIORITY, LVGL_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "[STATS] Network Task:  Core %d, Priority %d, Stack %d bytes",
           NETWORK_TASK_CORE, NETWORK_TASK_PRIORITY, NETWORK_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "[STATS] Messaging Task: Core %d, Priority %d, Stack %d bytes",
           MESSAGING_TASK_CORE, MESSAGING_TASK_PRIORITY,
           MESSAGING_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "[STATS] OTA Task:      Core %d, Priority %d, Stack %d bytes",
           OTA_TASK_CORE, OTA_TASK_PRIORITY, OTA_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "[STATS] Audio Task:    Core %d, Priority %d, Stack %d bytes",
           AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, AUDIO_TASK_STACK_SIZE);
  ESP_LOGI(TAG, "[STATS] =========================");
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