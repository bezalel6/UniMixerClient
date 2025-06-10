#include "MessageBus.h"
#include "../hardware/DeviceManager.h"
#include "MessagingConfig.h"
#include <esp_log.h>
#include <vector>
#include <ArduinoJson.h>

// LVGL includes must come before UI includes
#include <lvgl.h>

// Forward declaration of UI element - will be null if UI not available
extern lv_obj_t* ui_txtAreaDebugLog;

#include "DebugUtils.h"

static const char* TAG = "SerialTransport";

// Runtime debug mode control
bool runtime_debug_mode_enabled = false;

namespace Messaging::Transports {

// Handler storage for serial message routing
static std::vector<Handler> serialHandlers;
static bool serialInitialized = false;
static unsigned long lastSerialCheck = 0;
static String incomingBuffer = "";

// Helper functions
static void ProcessIncomingSerial();
static void ParseSerialMessage(const String& message);
static void LogMessageToUI(const String& message);
static Handler* FindSerialHandler(const String& messageType);
static bool IsSerialAvailable();

// Serial protocol implementation - updated for new CMD/STATUS/RESULT format
static Transport SerialTransport = {
    .Publish = [](const char* messageType, const char* payload) -> bool {
        if (!IsSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for publishing");
            return false;
        }

        // Simplified format: just send raw JSON
        String message = String(payload) + String(Protocol::SERIAL_TERMINATOR);

        Hardware::Device::getDataSerial().print(message);
        Hardware::Device::getDataSerial().flush();

        LOG_SERIAL_TX(payload);

#if MESSAGING_DESERIALIZATION_DEBUG_MODE
        // Log outgoing messages to UI in debug mode
        LOG_TO_UI(ui_txtAreaDebugLog, String("TX: ") + String(payload));
#endif
        return true;
    },

    .PublishDelayed = [](const char* messageType, const char* payload) -> bool {
        // For serial, immediate publishing is fine (no connection state to worry about)
        return SerialTransport.Publish(messageType, payload);
    },

    .IsConnected = []() -> bool {
        return IsSerialAvailable();
    },

    .RegisterHandler = [](const Handler& handler) -> bool {
        ESP_LOGI(TAG, "Registering serial handler: %s for type: %s",
                 handler.Identifier.c_str(), handler.SubscribeTopic.c_str());

        // Check if handler already exists
        for (const auto& existingHandler : serialHandlers) {
            if (existingHandler.Identifier == handler.Identifier) {
                ESP_LOGW(TAG, "Handler %s already registered", handler.Identifier.c_str());
                return false;
            }
        }

        // Add to our handler list
        serialHandlers.push_back(handler);
        ESP_LOGI(TAG, "Successfully registered serial handler: %s", handler.Identifier.c_str());
        return true;
    },

    .UnregisterHandler = [](const String& identifier) -> bool {
        ESP_LOGI(TAG, "Unregistering serial handler: %s", identifier.c_str());

        for (auto it = serialHandlers.begin(); it != serialHandlers.end(); ++it) {
            if (it->Identifier == identifier) {
                serialHandlers.erase(it);
                ESP_LOGI(TAG, "Successfully unregistered serial handler: %s", identifier.c_str());
                return true;
            }
        }

        ESP_LOGW(TAG, "Handler not found: %s", identifier.c_str());
        return false;
    },

    .Update = []() -> void {
        if (!serialInitialized) {
            return;
        }

        // Process incoming serial messages
        ProcessIncomingSerial();
    },

    .GetStatus = []() -> ConnectionStatus {
        return IsSerialAvailable() ? ConnectionStatus::Connected : ConnectionStatus::Disconnected;
    },

    .GetStatusString = []() -> const char* {
        return IsSerialAvailable() ? "Connected" : "Disconnected";
    },

    .Init = []() -> void {
        ESP_LOGI(TAG, "Initializing Serial transport");

        // Serial is already initialized by DeviceManager, so we just set our flag
        serialInitialized = true;
        lastSerialCheck = millis();
        incomingBuffer = "";
        serialHandlers.clear();

        // Check if data serial is available
        bool dataAvailable = IsSerialAvailable();
        ESP_LOGI(TAG, "Serial transport initialized - Data serial available: %s",
                 dataAvailable ? "true" : "false");
    },

    .Deinit = []() -> void {
        ESP_LOGI(TAG, "Deinitializing Serial transport");

        serialInitialized = false;
        serialHandlers.clear();
        incomingBuffer = "";

        ESP_LOGI(TAG, "Serial transport deinitialized");
    }};

// Helper function implementations
static bool IsSerialAvailable() {
    return Hardware::Device::isDataSerialAvailable();
}

static void ProcessIncomingSerial() {
    unsigned long now = millis();

    // Check for incoming data on the serial interface
    HardwareSerial& dataSerial = Hardware::Device::getDataSerial();
    while (dataSerial.available() > 0) {
        char c = dataSerial.read();

        if (c == Protocol::SERIAL_TERMINATOR) {
            // Complete message received
            if (incomingBuffer.length() > 0) {
                ParseSerialMessage(incomingBuffer);
                incomingBuffer = "";
            }
        } else if (c != '\r') {  // Ignore carriage returns
            incomingBuffer += c;

            // Prevent buffer overflow
            if (incomingBuffer.length() > Protocol::MAX_PAYLOAD_LENGTH + 20) {
                ESP_LOGW(TAG, "Serial buffer overflow, clearing");
                incomingBuffer = "";
            }
        }

        lastSerialCheck = now;
    }

    // // Clear stale buffer data
    // if (incomingBuffer.length() > 0 && (now - lastSerialCheck) > Protocol::SERIAL_TIMEOUT_MS) {
    //     ESP_LOGW(TAG, "Serial buffer timeout, clearing stale data");
    //     incomingBuffer = "";
    // }
}

static void LogMessageToUI(const String& message) {
    LOG_TO_UI(ui_txtAreaDebugLog, String("RX: ") + message);
}

static void ParseSerialMessage(const String& message) {
    LOG_SERIAL_RX(message.c_str());

    // Check both compile-time and runtime debug mode flags
    if (IsDebugModeEnabled()) {
        // Debug mode: Just log to UI and provide detailed analysis
        ESP_LOGI(TAG, "[DEBUG MODE] Message received - length: %d", message.length());

        // Log to UI
        LogMessageToUI(message);

        // Try to parse JSON for structure analysis
        ArduinoJson::JsonDocument doc;
        ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, message);

        String analysisLog;
        if (error) {
            analysisLog = String("[") + String(millis()) + String("] JSON Parse Error: ") + String(error.c_str()) + "\n";
            ESP_LOGW(TAG, "[DEBUG MODE] JSON deserialization failed: %s", error.c_str());
        } else {
            analysisLog = String("[") + String(millis()) + String("] JSON Parse OK - Keys: ");

            // List all JSON keys
            for (ArduinoJson::JsonPair kv : doc.as<ArduinoJson::JsonObject>()) {
                analysisLog += String(kv.key().c_str()) + ", ";
            }
            analysisLog += "\n";

            ESP_LOGI(TAG, "[DEBUG MODE] JSON parsed successfully - %d keys", doc.as<ArduinoJson::JsonObject>().size());
        }

        // Add analysis to UI
        if (ui_txtAreaDebugLog != nullptr) {
            lv_textarea_add_text(ui_txtAreaDebugLog, analysisLog.c_str());
        }

        ESP_LOGI(TAG, "[DEBUG MODE] Message processing complete - not forwarded to handlers");
        return;  // Don't process further in debug mode

    } else {
        // Normal processing mode

        // Simplified: all messages from server are status messages
        String messageType = "STATUS";
        String payload = message;

        if (payload.length() == 0) {
            ESP_LOGW(TAG, "Empty payload in message");
            return;
        }

        ESP_LOGI(TAG, "Parsed message - Type: %s, Payload length: %d", messageType.c_str(), payload.length());

#if MESSAGING_LOG_ALL_MESSAGES
        ESP_LOGI(TAG, "Full payload: %s", payload.c_str());
#endif

        // Try to parse JSON for validation and enhanced logging
        ArduinoJson::JsonDocument doc;
        ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, payload);

        if (!error) {
            ESP_LOGI(TAG, "JSON structure valid - %d keys detected", doc.as<ArduinoJson::JsonObject>().size());

            // Log key JSON fields if present
            if (doc["sessions"].is<ArduinoJson::JsonArray>()) {
                int sessionCount = doc["sessions"].size();
                ESP_LOGI(TAG, "Status message contains %d sessions", sessionCount);
            }
            if (doc["commandType"].is<const char*>()) {
                ESP_LOGI(TAG, "Command type: %s", doc["commandType"].as<const char*>());
            }
        } else {
            ESP_LOGW(TAG, "JSON parsing failed: %s", error.c_str());
        }

        // Find appropriate handler
        Handler* handler = FindSerialHandler(messageType);

        if (handler && handler->Callback) {
            ESP_LOGI(TAG, "Calling handler %s for message type %s", handler->Identifier.c_str(), messageType.c_str());
            handler->Callback(messageType.c_str(), payload.c_str());
            ESP_LOGI(TAG, "Handler %s completed successfully", handler->Identifier.c_str());
        } else {
            ESP_LOGW(TAG, "No handler found for message type: %s (available handlers: %d)", messageType.c_str(), serialHandlers.size());

            // List available handlers for debugging
            for (const auto& h : serialHandlers) {
                ESP_LOGD(TAG, "Available handler: %s (topic: %s, active: %s)",
                         h.Identifier.c_str(), h.SubscribeTopic.c_str(), h.Active ? "yes" : "no");
            }
        }

    }  // End of debug mode check
}

static Handler* FindSerialHandler(const String& messageType) {
    for (auto& handler : serialHandlers) {
        if (handler.Active && handler.SubscribeTopic == messageType) {
            return &handler;
        }
    }
    return nullptr;
}

// Transport getter
Transport* GetSerialTransport() {
    return &SerialTransport;
}

}  // namespace Messaging::Transports