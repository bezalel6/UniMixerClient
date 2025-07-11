#include "SimplifiedSerialEngine.h"
#include <esp_log.h>

static const char* TAG = "SerialEngine";

namespace Messaging {

// Static member definition
SerialEngine *SerialEngine::instance = nullptr;

} // namespace Messaging
