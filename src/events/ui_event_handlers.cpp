#include "ui_event_handlers.h"
#include <ui/ui.h>
#include <esp32_smartdisplay.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include "../hardware/mqtt_manager.h"
#include "../hardware/device_manager.h"

static const char* TAG = "UIEventHandlers";

// Button click handler that publishes audio status request
void ui_btnRequestData_clicked_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button clicked - requesting audio status");

        // Create JSON message
        JsonDocument doc;
        doc["messageType"] = "audio.status.request";
        doc["timestamp"] = String(device_get_millis());
        doc["messageId"] = String(device_get_millis());  // Simple ID based on millis

        // Serialize to string
        String json_payload;
        serializeJson(doc, json_payload);

        // Publish the message
        mqtt_publish("homeassistant/unimix/audio/requests", json_payload.c_str());
    }
}