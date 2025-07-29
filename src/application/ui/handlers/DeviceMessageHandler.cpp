#include "DeviceMessageHandler.h"
#include <esp_log.h>
#include <ui/ui.h>

namespace Application {
namespace UI {
namespace Handlers {

static const char* TAG = "DeviceMessageHandler";

void DeviceMessageHandler::registerHandler() {
    // Registration will be handled by MessageHandlerRegistry
}

void DeviceMessageHandler::handleMasterDevice(const LVGLMessage_t* msg) {
    if (ui_lblPrimaryAudioDeviceValue) {
        lv_label_set_text(ui_lblPrimaryAudioDeviceValue, msg->data.master_device.device_name);
    }
}

void DeviceMessageHandler::handleSingleDevice(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "Single device update requested: %s", msg->data.single_device.device_name);
    // TODO: Update single device dropdown when UI element is available
    // Currently placeholder - will be implemented when UI supports it
}

void DeviceMessageHandler::handleBalanceDevices(const LVGLMessage_t* msg) {
    const auto& data = msg->data.balance_devices;
    ESP_LOGI(TAG, "Balance devices update requested: %s, %s", data.device1_name, data.device2_name);
    // TODO: Update balance device dropdowns when UI elements are available
    // Currently placeholder - will be implemented when UI supports it
}

void DeviceMessageHandler::safeStringCopy(char* dest, const char* src, size_t destSize) {
    if (dest && src && destSize > 0) {
        strncpy(dest, src, destSize - 1);
        dest[destSize - 1] = '\0';
    } else if (dest && destSize > 0) {
        dest[0] = '\0';
    }
}

} // namespace Handlers
} // namespace UI
} // namespace Application