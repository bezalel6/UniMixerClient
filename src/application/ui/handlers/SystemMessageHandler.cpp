#include "SystemMessageHandler.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Application {
namespace UI {
namespace Handlers {

using namespace Application::System;

static const char* TAG = "SystemMessageHandler";

void SystemMessageHandler::registerHandler() {
    // Set up callbacks for SystemStateOverlay
    auto& overlay = System::SystemStateOverlay::getInstance();
    
    overlay.setFormatSDCallback([]() {
        SDCardOperations::getInstance().requestFormat();
    });
    
    overlay.setRestartCallback([]() {
        ESP_LOGI(TAG, "RESTART button clicked - restarting in 2 seconds");
        System::SystemStateOverlay::getInstance().hide();
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    });
    
    overlay.setRefreshCallback([]() {
        ESP_LOGI(TAG, "REFRESH button clicked - updating overview");
        LVGLMessageHandler::updateStateOverview();
    });
    
    // Set up callbacks for SDCardOperations
    auto& sdOps = SDCardOperations::getInstance();
    
    sdOps.setProgressCallback([](uint8_t progress, const char* message) {
        LVGLMessageHandler::updateSDFormatProgress(progress, message);
    });
    
    sdOps.setCompleteCallback([](bool success, const char* message) {
        LVGLMessageHandler::completeSDFormat(success, message);
    });
}

void SystemMessageHandler::handleShowStateOverview(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "Settings: Showing comprehensive system overview");
    System::SystemStateOverlay::getInstance().show();
}

void SystemMessageHandler::handleUpdateStateOverview(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "Settings: Updating state overview with current system data");
    auto stateData = convertToStateData(msg);
    System::SystemStateOverlay::getInstance().updateStateData(stateData);
}

void SystemMessageHandler::handleHideStateOverview(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "Settings: Hiding state overview overlay");
    System::SystemStateOverlay::getInstance().hide();
}

void SystemMessageHandler::handleSDStatus(const LVGLMessage_t* msg) {
    const auto& data = msg->data.sd_status;
    ESP_LOGI(TAG, "SD Status update: %s, Mounted: %s, Total: %llu MB, Used: %llu MB",
             data.status, data.mounted ? "Yes" : "No", data.total_mb, data.used_mb);
    // TODO: Update SD status display when UI element is available
}

void SystemMessageHandler::handleFormatSDRequest(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "SD Format: Request received");
    SDCardOperations::getInstance().requestFormat();
}

void SystemMessageHandler::handleFormatSDConfirm(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "SD Format: Confirm received");
    SDCardOperations::getInstance().confirmFormat();
}

void SystemMessageHandler::handleFormatSDProgress(const LVGLMessage_t* msg) {
    const auto& data = msg->data.sd_format;
    ESP_LOGI(TAG, "SD Format: Progress update - %d%% - %s", data.progress, data.message);
    SDCardOperations::getInstance().updateProgress(data.progress, data.message);
}

void SystemMessageHandler::handleFormatSDComplete(const LVGLMessage_t* msg) {
    const auto& data = msg->data.sd_format;
    ESP_LOGI(TAG, "SD Format: Complete - Success: %s - %s", data.success ? "YES" : "NO", data.message);
    SDCardOperations::getInstance().completeFormat(data.success, data.message);
}

System::SystemStateOverlay::StateData SystemMessageHandler::convertToStateData(const LVGLMessage_t* msg) {
    System::SystemStateOverlay::StateData stateData;
    const auto& data = msg->data.state_overview;
    
    // Copy system information
    stateData.free_heap = data.free_heap;
    stateData.free_psram = data.free_psram;
    stateData.cpu_freq = data.cpu_freq;
    stateData.uptime_ms = data.uptime_ms;
    
    // Copy network information
    strncpy(stateData.wifi_status, data.wifi_status, sizeof(stateData.wifi_status) - 1);
    stateData.wifi_status[sizeof(stateData.wifi_status) - 1] = '\0';
    stateData.wifi_rssi = data.wifi_rssi;
    strncpy(stateData.ip_address, data.ip_address, sizeof(stateData.ip_address) - 1);
    stateData.ip_address[sizeof(stateData.ip_address) - 1] = '\0';
    
    // Copy audio state
    strncpy(stateData.current_tab, data.current_tab, sizeof(stateData.current_tab) - 1);
    stateData.current_tab[sizeof(stateData.current_tab) - 1] = '\0';
    
    strncpy(stateData.main_device, data.main_device, sizeof(stateData.main_device) - 1);
    stateData.main_device[sizeof(stateData.main_device) - 1] = '\0';
    stateData.main_device_volume = data.main_device_volume;
    stateData.main_device_muted = data.main_device_muted;
    
    strncpy(stateData.balance_device1, data.balance_device1, sizeof(stateData.balance_device1) - 1);
    stateData.balance_device1[sizeof(stateData.balance_device1) - 1] = '\0';
    stateData.balance_device1_volume = data.balance_device1_volume;
    stateData.balance_device1_muted = data.balance_device1_muted;
    
    strncpy(stateData.balance_device2, data.balance_device2, sizeof(stateData.balance_device2) - 1);
    stateData.balance_device2[sizeof(stateData.balance_device2) - 1] = '\0';
    stateData.balance_device2_volume = data.balance_device2_volume;
    stateData.balance_device2_muted = data.balance_device2_muted;
    
    return stateData;
}

} // namespace Handlers
} // namespace UI
} // namespace Application