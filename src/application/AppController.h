#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>

namespace Application {

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