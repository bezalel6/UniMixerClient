#include "AppController.h"
#include "AudioStatusManager.h"
#include "../display/DisplayManager.h"
#include "../hardware/DeviceManager.h"
#include "../hardware/NetworkManager.h"
#include "../hardware/OTAManager.h"
#include "../hardware/TaskManager.h"
#include "../messaging/MessageBus.h"
#include "../events/UiEventHandlers.h"
#include "../../include/MessagingConfig.h"
#include "../../include/OTAConfig.h"
#include <ui/ui.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Private variables
static const char* TAG = "AppController";

namespace Application {

bool init(void) {
    ESP_LOGI(TAG, "Initializing Application Controller");

    // Initialize hardware/device manager
    if (!Hardware::Device::init()) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
        return false;
    }

    // Initialize display manager
    if (!Display::init()) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return false;
    }

    // Initialize Task Manager first (this initializes MessageBus)
    if (!Hardware::TaskManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize task manager");
        return false;
    }

    // Initialize audio status manager (requires MessageBus to be initialized)
    if (!Application::Audio::StatusManager::init()) {
        ESP_LOGE(TAG, "Failed to initialize audio status manager");
        return false;
    }

    // Setup UI components
    setupUiComponents();

    // Start all tasks for multi-threaded operation
    if (!Hardware::TaskManager::startAllTasks()) {
        ESP_LOGE(TAG, "Failed to start tasks");
        return false;
    }

    ESP_LOGI(TAG, "Application Controller initialized successfully with multi-threading");
    return true;
}

void deinit(void) {
    ESP_LOGI(TAG, "Deinitializing Application Controller");

    // Stop all tasks first (this also handles messaging and network cleanup)
    Hardware::TaskManager::deinit();

    Application::Audio::StatusManager::deinit();
    Display::deinit();
    Hardware::Device::deinit();
}

void run(void) {
    // In multi-threaded mode, the main loop only handles basic LED updates
    // All other functionality is handled by dedicated tasks

#ifdef BOARD_HAS_RGB_LED
    // Update LED colors
    Hardware::Device::ledCycleColors();
#endif

    // Update display FPS calculation (non-blocking)
    Display::update();

    // Small delay to prevent watchdog timeout and allow task switching
    vTaskDelay(pdMS_TO_TICKS(10));
}

void setupUiComponents(void) {
    // Set display to 180 degrees rotation
    Display::setRotation(Display::ROTATION_180);

    // Register button click event handler
    lv_obj_add_event_cb(ui_btnRequestData, Events::UI::btnRequestDataClickedHandler, LV_EVENT_CLICKED, NULL);

    // Register audio device dropdown event handlers
    lv_obj_add_event_cb(ui_selectAudioDevice, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice1, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_selectAudioDevice2, Events::UI::audioDeviceDropdownChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);

    // Register volume arc event handler
    lv_obj_add_event_cb(ui_volumeSlider, Events::UI::volumeArcChangedHandler, LV_EVENT_VALUE_CHANGED, NULL);
}

// Old update functions removed - now handled by dedicated tasks

}  // namespace Application