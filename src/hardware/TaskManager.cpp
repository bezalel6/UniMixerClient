#include "TaskManager.h"
#include "../display/DisplayManager.h"
#include "../messaging/MessageBus.h"
#include "NetworkManager.h"
#include "OTAManager.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include <ui/ui.h>
#include <esp_log.h>

static const char* TAG = "TaskManager";

namespace Hardware {
namespace TaskManager {

// System event constants
const EventBits_t EVENT_WIFI_CONNECTED = (1 << 0);
const EventBits_t EVENT_WIFI_DISCONNECTED = (1 << 1);
const EventBits_t EVENT_OTA_START = (1 << 2);
const EventBits_t EVENT_OTA_PROGRESS = (1 << 3);
const EventBits_t EVENT_OTA_COMPLETE = (1 << 4);
const EventBits_t EVENT_OTA_ERROR = (1 << 5);

// Task handles
TaskHandle_t lvglRenderTaskHandle = NULL;
TaskHandle_t displayMgrTaskHandle = NULL;
TaskHandle_t networkMgrTaskHandle = NULL;
TaskHandle_t appCtrlTaskHandle = NULL;
TaskHandle_t otaMgrTaskHandle = NULL;

// Inter-task communication objects
QueueHandle_t uiUpdateQueue = NULL;
QueueHandle_t networkStatusQueue = NULL;
QueueHandle_t otaProgressQueue = NULL;
SemaphoreHandle_t displayMutex = NULL;
SemaphoreHandle_t networkMutex = NULL;
EventGroupHandle_t systemEventGroup = NULL;

bool init(void) {
    ESP_LOGI(TAG, "Initializing Task Manager");

    // Initialize messaging system first
    if (!Messaging::MessageBus::Init()) {
        ESP_LOGE(TAG, "Failed to initialize messaging system");
        return false;
    }

    // Determine if network manager is needed (for MQTT or OTA)
    bool networkNeeded = false;

#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2
    networkNeeded = true;  // MQTT transport modes need network
#endif

#if OTA_ENABLE_UPDATES
    networkNeeded = true;  // OTA always needs network
#endif

    // Initialize network manager if needed
    if (networkNeeded) {
        ESP_LOGI(TAG, "Network required for MQTT/OTA - initializing network manager");
        if (!Hardware::Network::init()) {
            ESP_LOGE(TAG, "Failed to initialize network manager");
            return false;
        }

        // Enable auto-reconnect (WiFi connection will be started automatically by NetworkManager)
        Hardware::Network::enableAutoReconnect(true);
    }

    // Configure transport based on MessagingConfig.h settings
#if MESSAGING_DEFAULT_TRANSPORT == 0
// MQTT only
#if MESSAGING_ENABLE_MQTT_TRANSPORT
    ESP_LOGI(TAG, "Configuring MQTT transport (config: MQTT only)");
    Messaging::MessageBus::EnableMqttTransport();
#else
    ESP_LOGE(TAG, "MQTT transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 1
// Serial only - no additional network configuration needed
#if MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring Serial transport (config: Serial only)");
    Messaging::MessageBus::EnableSerialTransport();
#else
    ESP_LOGE(TAG, "Serial transport requested but disabled in config");
    return false;
#endif
#elif MESSAGING_DEFAULT_TRANSPORT == 2
// Both transports
#if MESSAGING_ENABLE_MQTT_TRANSPORT && MESSAGING_ENABLE_SERIAL_TRANSPORT
    ESP_LOGI(TAG, "Configuring dual transport (config: MQTT + Serial)");
    Messaging::MessageBus::EnableBothTransports();
#else
    ESP_LOGE(TAG, "Dual transport requested but one or both transports disabled in config");
    return false;
#endif
#else
    ESP_LOGE(TAG, "Invalid MESSAGING_DEFAULT_TRANSPORT value: %d", MESSAGING_DEFAULT_TRANSPORT);
    return false;
#endif

    // Create recursive mutexes (better for multi-core ESP32)
    displayMutex = xSemaphoreCreateRecursiveMutex();
    if (!displayMutex) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return false;
    }

    networkMutex = xSemaphoreCreateRecursiveMutex();
    if (!networkMutex) {
        ESP_LOGE(TAG, "Failed to create network mutex");
        return false;
    }

    // Create event group
    systemEventGroup = xEventGroupCreate();
    if (!systemEventGroup) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return false;
    }

    // Create queues
    uiUpdateQueue = xQueueCreate(10, sizeof(ui_update_msg_t));
    if (!uiUpdateQueue) {
        ESP_LOGE(TAG, "Failed to create UI update queue");
        return false;
    }

    networkStatusQueue = xQueueCreate(5, sizeof(network_status_msg_t));
    if (!networkStatusQueue) {
        ESP_LOGE(TAG, "Failed to create network status queue");
        return false;
    }

    otaProgressQueue = xQueueCreate(5, sizeof(ota_progress_msg_t));
    if (!otaProgressQueue) {
        ESP_LOGE(TAG, "Failed to create OTA progress queue");
        return false;
    }

    ESP_LOGI(TAG, "Task Manager initialized successfully");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Task Manager");

    stopAllTasks();

    // Deinitialize messaging system
    Messaging::MessageBus::Deinit();

    // Deinitialize network manager if it was initialized
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || OTA_ENABLE_UPDATES
    Hardware::Network::deinit();
#endif

    // Delete queues
    if (uiUpdateQueue) {
        vQueueDelete(uiUpdateQueue);
        uiUpdateQueue = NULL;
    }
    if (networkStatusQueue) {
        vQueueDelete(networkStatusQueue);
        networkStatusQueue = NULL;
    }
    if (otaProgressQueue) {
        vQueueDelete(otaProgressQueue);
        otaProgressQueue = NULL;
    }

    // Delete mutexes
    if (displayMutex) {
        vSemaphoreDelete(displayMutex);
        displayMutex = NULL;
    }
    if (networkMutex) {
        vSemaphoreDelete(networkMutex);
        networkMutex = NULL;
    }

    // Delete event group
    if (systemEventGroup) {
        vEventGroupDelete(systemEventGroup);
        systemEventGroup = NULL;
    }
}

bool startAllTasks(void) {
    ESP_LOGI(TAG, "Starting all tasks");

    // Create LVGL render task on Core 0 (highest priority)
    BaseType_t result = xTaskCreatePinnedToCore(
        lvglRenderTask,
        "lvgl_render",
        STACK_SIZE_LVGL_RENDER,
        NULL,
        PRIORITY_LVGL_RENDER,
        &lvglRenderTaskHandle,
        DISPLAY_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL render task");
        return false;
    }

    // Create Display Manager task on Core 0
    result = xTaskCreatePinnedToCore(
        displayMgrTask,
        "display_mgr",
        STACK_SIZE_DISPLAY_MGR,
        NULL,
        PRIORITY_DISPLAY_MGR,
        &displayMgrTaskHandle,
        DISPLAY_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Display Manager task");
        return false;
    }

    // Create Network Manager task on Core 1
    result = xTaskCreatePinnedToCore(
        networkMgrTask,
        "network_mgr",
        STACK_SIZE_NETWORK_MGR,
        NULL,
        PRIORITY_NETWORK_MGR,
        &networkMgrTaskHandle,
        APPLICATION_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Network Manager task");
        return false;
    }

    // Create Application Controller task on Core 1
    result = xTaskCreatePinnedToCore(
        appCtrlTask,
        "app_ctrl",
        STACK_SIZE_APP_CTRL,
        NULL,
        PRIORITY_APP_CTRL,
        &appCtrlTaskHandle,
        APPLICATION_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Application Controller task");
        return false;
    }

    // Create OTA Manager task on Core 1 (lowest priority)
    result = xTaskCreatePinnedToCore(
        otaMgrTask,
        "ota_mgr",
        STACK_SIZE_OTA_MGR,
        NULL,
        PRIORITY_OTA_MGR,
        &otaMgrTaskHandle,
        APPLICATION_CORE);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA Manager task");
        return false;
    }

    ESP_LOGI(TAG, "All tasks started successfully");
    return true;
}

void stopAllTasks(void) {
    ESP_LOGI(TAG, "Stopping all tasks");

    if (lvglRenderTaskHandle) {
        vTaskDelete(lvglRenderTaskHandle);
        lvglRenderTaskHandle = NULL;
    }
    if (displayMgrTaskHandle) {
        vTaskDelete(displayMgrTaskHandle);
        displayMgrTaskHandle = NULL;
    }
    if (networkMgrTaskHandle) {
        vTaskDelete(networkMgrTaskHandle);
        networkMgrTaskHandle = NULL;
    }
    if (appCtrlTaskHandle) {
        vTaskDelete(appCtrlTaskHandle);
        appCtrlTaskHandle = NULL;
    }
    if (otaMgrTaskHandle) {
        vTaskDelete(otaMgrTaskHandle);
        otaMgrTaskHandle = NULL;
    }
}

// Inter-task communication helpers
bool sendUIUpdate(const ui_update_msg_t* msg) {
    if (!uiUpdateQueue || !msg) return false;
    return xQueueSend(uiUpdateQueue, msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool sendNetworkStatus(const network_status_msg_t* msg) {
    if (!networkStatusQueue || !msg) return false;
    return xQueueSend(networkStatusQueue, msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool sendOTAProgress(const ota_progress_msg_t* msg) {
    if (!otaProgressQueue || !msg) return false;
    return xQueueSend(otaProgressQueue, msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

void setSystemEvent(EventBits_t events) {
    if (systemEventGroup) {
        xEventGroupSetBits(systemEventGroup, events);
    }
}

void clearSystemEvent(EventBits_t events) {
    if (systemEventGroup) {
        xEventGroupClearBits(systemEventGroup, events);
    }
}

EventBits_t waitForSystemEvent(EventBits_t events, TickType_t timeout) {
    if (!systemEventGroup) return 0;
    return xEventGroupWaitBits(systemEventGroup, events, pdFALSE, pdFALSE, timeout);
}

// Mutex helpers
bool lockDisplay(TickType_t timeout) {
    if (!displayMutex) return false;
    return xSemaphoreTakeRecursive(displayMutex, timeout) == pdTRUE;
}

void unlockDisplay(void) {
    if (displayMutex) {
        xSemaphoreGiveRecursive(displayMutex);
    }
}

bool lockNetwork(TickType_t timeout) {
    if (!networkMutex) return false;
    return xSemaphoreTakeRecursive(networkMutex, timeout) == pdTRUE;
}

void unlockNetwork(void) {
    if (networkMutex) {
        xSemaphoreGiveRecursive(networkMutex);
    }
}

// LVGL Render Task - Core 0, Highest Priority
void lvglRenderTask(void* parameter) {
    ESP_LOGI(TAG, "LVGL Render Task started on Core %d", xPortGetCoreID());

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50);  // 20 FPS for stability

    while (true) {
        // Wait for the next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        // Update LVGL timers and handle rendering
        if (lockDisplay(pdMS_TO_TICKS(5))) {
            Display::tickUpdate();
            lv_timer_handler();
            unlockDisplay();
        }
    }
}

// Display Manager Task - Core 0, High Priority
void displayMgrTask(void* parameter) {
    ESP_LOGI(TAG, "Display Manager Task started on Core %d", xPortGetCoreID());

    ui_update_msg_t uiMsg;

    while (true) {
        // Check for UI update messages
        if (xQueueReceive(uiUpdateQueue, &uiMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (lockDisplay(pdMS_TO_TICKS(50))) {
                switch (uiMsg.type) {
                    case UI_UPDATE_NETWORK_STATUS:
                        Display::updateWifiStatusInternal(ui_lblWifiStatus, ui_objWifiIndicator,
                                                          uiMsg.data.network.status, uiMsg.data.network.connected);
                        Display::updateNetworkInfoInternal(ui_lblSSIDValue, ui_lblIPValue,
                                                           uiMsg.data.network.ssid, uiMsg.data.network.ip);
                        break;
                    case UI_UPDATE_OTA_PROGRESS:
                        if (uiMsg.data.ota.in_progress) {
                            lv_bar_set_value(ui_barOTAUpdateProgress, uiMsg.data.ota.progress, LV_ANIM_OFF);
                            lv_label_set_text(ui_lblOTAUpdateProgress, uiMsg.data.ota.status);
                        }
                        break;
                    case UI_UPDATE_FPS:
                        Display::updateFpsDisplayInternal(ui_lblFPS);
                        break;
                    default:
                        break;
                }
                unlockDisplay();
            }
        }
    }
}

// Network Manager Task - Core 1, Medium Priority
void networkMgrTask(void* parameter) {
    ESP_LOGI(TAG, "Network Manager Task started on Core %d", xPortGetCoreID());

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);  // Check every second

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        if (lockNetwork(pdMS_TO_TICKS(100))) {
#if MESSAGING_DEFAULT_TRANSPORT == 0 || MESSAGING_DEFAULT_TRANSPORT == 2 || OTA_ENABLE_UPDATES
            Hardware::Network::update();

            // Send network status update
            network_status_msg_t netStatus;
            netStatus.connected = Hardware::Network::isConnected();
            strncpy(netStatus.status, Hardware::Network::getWifiStatusString(), sizeof(netStatus.status) - 1);
            strncpy(netStatus.ssid, Hardware::Network::getSsid(), sizeof(netStatus.ssid) - 1);
            strncpy(netStatus.ip, Hardware::Network::getIpAddress(), sizeof(netStatus.ip) - 1);

            // Send UI update
            ui_update_msg_t uiMsg;
            uiMsg.type = UI_UPDATE_NETWORK_STATUS;
            uiMsg.data.network.connected = netStatus.connected;
            strncpy(uiMsg.data.network.status, netStatus.status, sizeof(uiMsg.data.network.status) - 1);
            strncpy(uiMsg.data.network.ssid, netStatus.ssid, sizeof(uiMsg.data.network.ssid) - 1);
            strncpy(uiMsg.data.network.ip, netStatus.ip, sizeof(uiMsg.data.network.ip) - 1);

            sendUIUpdate(&uiMsg);

            // Set/clear network events
            if (netStatus.connected) {
                setSystemEvent(EVENT_WIFI_CONNECTED);
                clearSystemEvent(EVENT_WIFI_DISCONNECTED);
            } else {
                setSystemEvent(EVENT_WIFI_DISCONNECTED);
                clearSystemEvent(EVENT_WIFI_CONNECTED);
            }
#endif
            unlockNetwork();
        }
    }
}

// Application Controller Task - Core 1, Medium Priority
void appCtrlTask(void* parameter) {
    ESP_LOGI(TAG, "Application Controller Task started on Core %d", xPortGetCoreID());

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(500);  // Update every 500ms

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        // Update messaging system
        Messaging::MessageBus::Update();

        // Send FPS update
        ui_update_msg_t uiMsg;
        uiMsg.type = UI_UPDATE_FPS;
        uiMsg.data.fps.fps = Display::getFPS();
        sendUIUpdate(&uiMsg);

        // Handle other periodic updates as needed
        // (Audio status, etc. can be added here)
    }
}

// OTA Manager Task - Core 1, Lowest Priority
void otaMgrTask(void* parameter) {
    ESP_LOGI(TAG, "OTA Manager Task started on Core %d", xPortGetCoreID());

    while (true) {
        // Wait for WiFi to be connected before handling OTA
        EventBits_t events = waitForSystemEvent(EVENT_WIFI_CONNECTED, pdMS_TO_TICKS(5000));

        if (events & EVENT_WIFI_CONNECTED) {
#if OTA_ENABLE_UPDATES
            if (lockNetwork(pdMS_TO_TICKS(100))) {
                Hardware::OTA::update();
                unlockNetwork();
            }
#endif
        }

        // Check for OTA progress messages
        ota_progress_msg_t otaMsg;
        if (xQueueReceive(otaProgressQueue, &otaMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Forward OTA progress to UI
            ui_update_msg_t uiMsg;
            uiMsg.type = UI_UPDATE_OTA_PROGRESS;
            uiMsg.data.ota.progress = otaMsg.progress;
            uiMsg.data.ota.in_progress = otaMsg.in_progress;
            strncpy(uiMsg.data.ota.status, otaMsg.status, sizeof(uiMsg.data.ota.status) - 1);

            sendUIUpdate(&uiMsg);

            // Set appropriate system events
            if (otaMsg.in_progress) {
                setSystemEvent(EVENT_OTA_PROGRESS);
            }
            if (otaMsg.error) {
                setSystemEvent(EVENT_OTA_ERROR);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay for task yielding
    }
}

}  // namespace TaskManager
}  // namespace Hardware