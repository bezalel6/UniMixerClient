#include "SDCardOperations.h"
#include "../../hardware/SDManager.h"
#include "../ui/dialogs/UniversalDialog.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Application {
namespace System {

static const char* TAG = "SDCardOperations";

SDCardOperations& SDCardOperations::getInstance() {
    static SDCardOperations instance;
    return instance;
}

void SDCardOperations::requestFormat() {
    ESP_LOGI(TAG, "SD Format: Showing confirmation dialog");
    
    // Don't show confirmation if already formatting
    if (formatting) {
        ESP_LOGW(TAG, "SD Format: Already in formatting process, ignoring request");
        return;
    }
    
    // Use the Universal Dialog system for SD format confirmation
    UI::Dialog::UniversalDialog::showWarning(
        "FORMAT SD CARD",
        "*** WARNING ***\n\n"
        "This will PERMANENTLY ERASE\n"
        "ALL DATA on the SD card!\n\n"
        "This action CANNOT be undone.\n"
        "Are you absolutely sure?",
        [this]() {
            // Confirmed - start format
            ESP_LOGI(TAG, "SD Format: Confirmed by user");
            confirmFormat();
        },
        []() {
            // Cancelled
            ESP_LOGI(TAG, "SD Format: Cancelled by user");
        },
        UI::Dialog::DialogSize::MEDIUM);
}

void SDCardOperations::confirmFormat() {
    ESP_LOGI(TAG, "SD Format: Starting format process");
    
    if (formatting) {
        ESP_LOGW(TAG, "SD Format: Already formatting");
        return;
    }
    
    // Check if a dialog is already open
    if (UI::Dialog::UniversalDialog::isDialogOpen()) {
        ESP_LOGW(TAG, "SD Format: Dialog already open, closing it first");
        UI::Dialog::UniversalDialog::closeDialog();
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to ensure dialog is closed
    }
    
    formatting = true;
    currentProgress = 0;
    
    // Use Universal Dialog system for progress dialog
    UI::Dialog::ProgressConfig progressConfig;
    progressConfig.title = "FORMATTING SD CARD";
    progressConfig.message = "Initializing format...";
    progressConfig.value = 0;
    progressConfig.max = 100;
    progressConfig.indeterminate = false;
    progressConfig.cancellable = false;  // Don't allow cancellation during format
    
    UI::Dialog::UniversalDialog::showProgress(progressConfig, UI::Dialog::DialogSize::MEDIUM);
    
    ESP_LOGI(TAG, "SD Format: Progress dialog created, starting format task");
    
    // Immediately show some progress to indicate activity
    vTaskDelay(pdMS_TO_TICKS(50)); // Small delay to ensure dialog is rendered
    updateProgress(1, "Starting format task...");
    
    // Start the actual SD format process in a separate task with higher priority
    // Increased stack size to 8192 and priority to 5 for better responsiveness
    BaseType_t taskCreated = xTaskCreate(formatTask, "SDFormatTask", 8192, this, 5, &formatTaskHandle);
    
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "SD Format: Failed to create format task!");
        formatting = false;
        UI::Dialog::UniversalDialog::closeDialog();
        UI::Dialog::UniversalDialog::showError(
            "Format Error",
            "Failed to start format operation",
            nullptr,
            UI::Dialog::DialogSize::MEDIUM);
        return;
    }
    
    // Note: Initial progress update is now handled in performFormat() to avoid duplicate updates
}

void SDCardOperations::cancelFormat() {
    ESP_LOGI(TAG, "SD Format: Cancelling format operation");
    
    if (formatTaskHandle) {
        vTaskDelete(formatTaskHandle);
        formatTaskHandle = nullptr;
    }
    
    formatting = false;
    UI::Dialog::UniversalDialog::closeDialog();
}

void SDCardOperations::updateProgress(uint8_t progress, const char* message) {
    ESP_LOGI(TAG, "SD Format: Progress update - %d%% - %s", progress, message);
    
    currentProgress = progress;
    if (message) {
        strncpy(currentMessage, message, sizeof(currentMessage) - 1);
        currentMessage[sizeof(currentMessage) - 1] = '\0';
    }
    
    // Update progress dialog
    UI::Dialog::UniversalDialog::updateProgress(progress, message);
    
    // Call progress callback if set
    if (progressCallback) {
        progressCallback(progress, message);
    }
}

void SDCardOperations::completeFormat(bool success, const char* message) {
    ESP_LOGI(TAG, "SD Format: Complete - Success: %s - %s", success ? "YES" : "NO", message);
    
    formatting = false;
    currentProgress = success ? 100 : 0;
    formatTaskHandle = nullptr;
    
    // Close the progress dialog
    UI::Dialog::UniversalDialog::closeDialog();
    
    // Show completion dialog based on success/failure
    if (success) {
        UI::Dialog::UniversalDialog::showInfo(
            "Format Complete",
            message,
            nullptr,
            UI::Dialog::DialogSize::MEDIUM);
    } else {
        UI::Dialog::UniversalDialog::showError(
            "Format Failed",
            message,
            nullptr,
            UI::Dialog::DialogSize::MEDIUM);
    }
    
    // Call complete callback if set
    if (completeCallback) {
        completeCallback(success, message);
    }
}

void SDCardOperations::formatTask(void* parameter) {
    ESP_LOGI(TAG, "SD Format Task: Task started successfully");
    
    SDCardOperations* ops = static_cast<SDCardOperations*>(parameter);
    if (ops) {
        ops->performFormat();
    } else {
        ESP_LOGE(TAG, "SD Format Task: Invalid parameter passed to task");
    }
    
    vTaskDelete(NULL);
}

void SDCardOperations::performFormat() {
    ESP_LOGI(TAG, "SD Format Task: Starting SD card format operation");
    
    bool formatSuccess = false;
    
    // Immediately update progress to show task is running
    updateProgress(2, "Task started...");
    
    // Small delay to ensure dialog is ready
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Phase 1: Preparation (5-15%)
    updateProgress(5, "Starting format operation...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Reduced from 1000ms
    
    updateProgress(10, "Preparing for format...");
    vTaskDelay(pdMS_TO_TICKS(300)); // Reduced from 500ms
    
    // Check if SD card is available
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD Format Task: SD card not mounted, attempting to mount");
        updateProgress(15, "Mounting SD card...");
        
        if (!Hardware::SD::mount()) {
            ESP_LOGE(TAG, "SD Format Task: Failed to mount SD card");
            completeFormat(false, "ERROR: Cannot access SD card");
            return;
        }
    }
    
    // Phase 2: Pre-format checks (15-25%)
    updateProgress(20, "Verifying SD card...");
    vTaskDelay(pdMS_TO_TICKS(300));
    
    Hardware::SD::SDCardInfo cardInfo = Hardware::SD::getCardInfo();
    if (cardInfo.cardType == CARD_NONE) {
        ESP_LOGE(TAG, "SD Format Task: No SD card detected");
        completeFormat(false, "ERROR: No SD card found");
        return;
    }
    
    ESP_LOGI(TAG, "SD Format Task: Card detected - Type: %d, Size: %.2f MB",
             cardInfo.cardType, cardInfo.cardSize / (1024.0 * 1024.0));
    
    // Phase 3: Begin format operation (25-90%)
    updateProgress(25, "Starting format operation...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Update progress during format
    updateProgress(40, "Removing files and directories...");
    vTaskDelay(pdMS_TO_TICKS(300));
    
    updateProgress(60, "Cleaning file system...");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    updateProgress(75, "Finalizing format...");
    
    // Perform the actual format operation
    ESP_LOGI(TAG, "SD Format Task: Calling Hardware::SD::format()");
    formatSuccess = Hardware::SD::format();
    
    if (formatSuccess) {
        ESP_LOGI(TAG, "SD Format Task: Format completed successfully");
        updateProgress(90, "Format completed successfully");
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Phase 4: Post-format verification (90-100%)
        updateProgress(95, "Verifying format...");
        vTaskDelay(pdMS_TO_TICKS(300));
        
        // Check if card is still accessible after format
        if (Hardware::SD::isMounted()) {
            completeFormat(true, "SD card formatted successfully!");
        } else {
            ESP_LOGW(TAG, "SD Format Task: Format completed but card not accessible");
            completeFormat(true, "Format completed (remount required)");
        }
    } else {
        ESP_LOGE(TAG, "SD Format Task: Format operation failed");
        completeFormat(false, "Format operation failed");
    }
    
    ESP_LOGI(TAG, "SD Format Task: Task completed");
}

} // namespace System
} // namespace Application