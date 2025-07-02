#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>

namespace Application {

#define INIT_STEP(description, code_block)              \
    do {                                                \
        ESP_LOGI(TAG, "WDT Reset: " description "..."); \
        esp_task_wdt_reset();                           \
        code_block;                                     \
        esp_task_wdt_reset();                           \
    } while (0)

#define INIT_STEP_CRITICAL(description, code_block)     \
    do {                                                \
        ESP_LOGI(TAG, "WDT Reset: " description "..."); \
        esp_task_wdt_reset();                           \
        if (!(code_block)) {                            \
            ESP_LOGE(TAG, "Failed to " description);    \
            return false;                               \
        }                                               \
        esp_task_wdt_reset();                           \
    } while (0)

#define INIT_STEP_OPTIONAL(description, success_msg, warning_msg, code_block) \
    do {                                                                      \
        ESP_LOGI(TAG, "WDT Reset: " description "...");                       \
        esp_task_wdt_reset();                                                 \
        if (code_block) {                                                     \
            ESP_LOGI(TAG, success_msg);                                       \
        } else {                                                              \
            ESP_LOGW(TAG, warning_msg);                                       \
        }                                                                     \
        esp_task_wdt_reset();                                                 \
    } while (0)
// Application controller
bool init(void);
void deinit(void);
void run(void);

// Application state management
void setupUiComponents(void);

// Configuration
#define APP_UPDATE_INTERVAL_MS 500

}  // namespace Application

#endif  // APP_CONTROLLER_H
