#include "UiEventHandlers.h"
#include <ui/ui.h>
#include <esp32_smartdisplay.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include "../hardware/MqttManager.h"
#include "../hardware/DeviceManager.h"

static const char* TAG = "UIEventHandlers";

namespace Events {
namespace UI {

// Button click handler that publishes audio status request
void btnRequestDataClickedHandler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button clicked - requesting audio status");

        // Create JSON message
        JsonDocument doc;
        doc["messageType"] = "audio.status.request";
        doc["timestamp"] = String(Hardware::Device::getMillis());
        doc["messageId"] = String(Hardware::Device::getMillis());  // Simple ID based on millis

        // Serialize to string
        String jsonPayload;
        serializeJson(doc, jsonPayload);

        // Publish the message
        Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str());
    }
}

}  // namespace UI
}  // namespace Events