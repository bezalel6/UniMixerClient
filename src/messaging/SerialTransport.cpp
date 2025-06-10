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

        // Always log TX to UI for debugging, minimal ESP_LOG
        LOG_TO_UI(ui_txtAreaDebugLog, String("TX: ") + String(payload));
        ESP_LOGI(TAG, "TX: %d chars", String(payload).length());
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

        // Log configuration
        ESP_LOGI(TAG, "Configuration: Baud=%d, Buffer=%d, Timeout=%dms, PayloadMax=%d",
                 MESSAGING_SERIAL_BAUD_RATE, MESSAGING_SERIAL_BUFFER_SIZE,
                 MESSAGING_SERIAL_TIMEOUT_MS, MESSAGING_MAX_PAYLOAD_LENGTH);

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

            // Prevent buffer overflow - use configured buffer size with margin
            const int maxBufferSize = MESSAGING_SERIAL_BUFFER_SIZE + 50;  // Buffer size + margin
            if (incomingBuffer.length() > maxBufferSize) {
                ESP_LOGW(TAG, "Serial buffer overflow, clearing (limit: %d)", maxBufferSize);
                incomingBuffer = "";
            }
        }

        lastSerialCheck = now;
    }
}

static void ParseSerialMessage(const String& message) {
    LOG_SERIAL_RX(message.c_str());

    // Message is already clean JSON content - no delimiter processing needed
    String jsonContent = message;
    jsonContent.trim();

    // Check both compile-time and runtime debug mode flags
    if (IsDebugModeEnabled()) {
        // Debug mode: Comprehensive UI logging with minimal ESP_LOG
        LOG_TO_UI(ui_txtAreaDebugLog, String("RX: ") + message);
        LOG_TO_UI(ui_txtAreaDebugLog, String("Length: ") + String(jsonContent.length()) + String(" chars"));

        // Try to parse JSON for structure analysis
        ArduinoJson::JsonDocument doc;
        ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, jsonContent);

        if (error) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("JSON ERROR: ") + String(error.c_str()));
            LOG_TO_UI(ui_txtAreaDebugLog, String("Raw JSON: ") + jsonContent);
        } else {
            String keysList = "Keys: ";
            int keyCount = 0;

            // List all JSON keys
            for (ArduinoJson::JsonPair kv : doc.as<ArduinoJson::JsonObject>()) {
                if (keyCount > 0) keysList += ", ";
                keysList += String(kv.key().c_str());
                keyCount++;
            }

            LOG_TO_UI(ui_txtAreaDebugLog, String("JSON OK: ") + String(keyCount) + String(" keys"));
            LOG_TO_UI(ui_txtAreaDebugLog, keysList);

            // Show specific field values for debugging
            if (doc["sessions"].is<ArduinoJson::JsonArray>()) {
                int sessionCount = doc["sessions"].size();
                LOG_TO_UI(ui_txtAreaDebugLog, String("Sessions: ") + String(sessionCount));

                // Show first few session details
                for (int i = 0; i < min(3, sessionCount); i++) {
                    auto session = doc["sessions"][i];
                    String sessionInfo = String("  [") + String(i) + String("] ");
                    if (session["processName"].is<const char*>()) {
                        sessionInfo += String(session["processName"].as<const char*>());
                    }
                    if (session["volume"].is<float>()) {
                        sessionInfo += String(" vol:") + String(session["volume"].as<float>(), 2);
                    }
                    LOG_TO_UI(ui_txtAreaDebugLog, sessionInfo);
                }
            }
        }

        // Only log critical info to ESP_LOG in debug mode
        ESP_LOGI(TAG, "Debug mode: Msg len=%d, JSON len=%d, Parse=%s", message.length(), jsonContent.length(), error ? "FAIL" : "OK");
        return;  // Don't process further in debug mode

    } else {
        // Normal processing mode - Log key info to UI, minimal ESP_LOG

        String messageType = "STATUS";
        String payload = jsonContent;

        if (payload.length() == 0) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("ERROR: Empty JSON content"));
            ESP_LOGW(TAG, "Empty JSON content");
            return;
        }

        // Log processing info to UI
        LOG_TO_UI(ui_txtAreaDebugLog, String("PROC: ") + String(payload.length()) + String(" chars"));

        // Try to parse JSON for validation
        ArduinoJson::JsonDocument doc;
        ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, payload);

        if (!error) {
            int keyCount = doc.as<ArduinoJson::JsonObject>().size();

            // Log key JSON fields to UI
            if (doc["sessions"].is<ArduinoJson::JsonArray>()) {
                int sessionCount = doc["sessions"].size();
                LOG_TO_UI(ui_txtAreaDebugLog, String("STATUS: ") + String(sessionCount) + String(" sessions"));
            }
            if (doc["commandType"].is<const char*>()) {
                LOG_TO_UI(ui_txtAreaDebugLog, String("CMD: ") + String(doc["commandType"].as<const char*>()));
            }

            // Minimal ESP_LOG for normal processing
            ESP_LOGI(TAG, "JSON OK, %d keys", keyCount);
        } else {
            LOG_TO_UI(ui_txtAreaDebugLog, String("JSON FAIL: ") + String(error.c_str()));
            ESP_LOGW(TAG, "JSON parse fail: %s", error.c_str());
        }

        // Find appropriate handler
        Handler* handler = FindSerialHandler(messageType);

        if (handler && handler->Callback) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER: ") + String(handler->Identifier.c_str()));
            ESP_LOGI(TAG, "Handler: %s", handler->Identifier.c_str());
            handler->Callback(messageType.c_str(), payload.c_str());
        } else {
            LOG_TO_UI(ui_txtAreaDebugLog, String("NO HANDLER for ") + messageType);
            ESP_LOGW(TAG, "No handler for: %s", messageType.c_str());
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