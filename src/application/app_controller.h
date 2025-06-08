#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Application controller
bool app_controller_init(void);
void app_controller_deinit(void);
void app_controller_run(void);

// Application state management
void app_controller_setup_ui_components(void);
void app_controller_update_periodic_data(void);
void app_controller_update_network_status(void);

// Configuration
#define APP_UPDATE_INTERVAL_MS 500

#ifdef __cplusplus
}
#endif

#endif  // APP_CONTROLLER_H