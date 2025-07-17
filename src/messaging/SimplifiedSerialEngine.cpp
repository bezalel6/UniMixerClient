#include "SimplifiedSerialEngine.h"
#include <esp_log.h>

static const char* TAG = "SerialEngine";

namespace Messaging {

// Static member definitions
SerialEngine* SerialEngine::instance = nullptr;
SemaphoreHandle_t SerialEngine::serialMutex = nullptr;

}  // namespace Messaging
