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
static String incomingBuffer = "";

// Flag to indicate new data is available (set by callback, processed by Update)
static volatile bool newDataAvailable = false;

// Rate limiting for JSON processing to prevent stack overflow during data bursts
static unsigned long lastJsonProcessTime = 0;
static const unsigned long MIN_JSON_PROCESS_INTERVAL = 50;  // Minimum 50ms between JSON processing

// Serial receive callback function (lightweight)
static void onSerialReceive();

// Process incoming serial data (heavy processing)
static void ProcessIncomingSerial();

// Helper functions
static void ParseSerialMessage(const String& message);
static Handler* FindSerialHandler(const String& messageType);
static bool IsSerialAvailable();

// Buffer management functions
static void FlushSerialRxBuffer();
static void FlushSerialBuffers();
static int GetAvailableDataCount();

// Serial protocol implementation - updated for new CMD/STATUS/RESULT format
static Transport SerialTransport = {
    .Publish = [](const char* messageType, const char* payload) -> bool {
        if (!IsSerialAvailable()) {
            ESP_LOGW(TAG, "Serial not available for publishing");
            LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL TX FAIL: Not available"));
            return false;
        }

        // Simplified format: just send raw JSON
        String message = String(payload) + String(Protocol::SERIAL_TERMINATOR);

        Hardware::Device::getDataSerial().print(message);
        Hardware::Device::getDataSerial().flush();

        // Always log TX to UI for debugging, minimal ESP_LOG
        ESP_LOGI(TAG, "TX: %d chars", String(payload).length());
        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL TX: ") + String(payload).length() + String(" chars"));
        return true;
    },

    .PublishDelayed = [](const char* messageType, const char* payload) -> bool {
        // For serial, immediate publishing is fine (no connection state to worry about)
        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL DELAYED TX: ") + String(payload).length() + String(" chars"));
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
                LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER DUPLICATE: ") + handler.Identifier);
                return false;
            }
        }

        // Add to our handler list
        serialHandlers.push_back(handler);
        ESP_LOGI(TAG, "Successfully registered serial handler: %s", handler.Identifier.c_str());
        LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER REGISTERED: ") + handler.Identifier + String(" -> ") + handler.SubscribeTopic);
        return true;
    },

    .UnregisterHandler = [](const String& identifier) -> bool {
        ESP_LOGI(TAG, "Unregistering serial handler: %s", identifier.c_str());

        for (auto it = serialHandlers.begin(); it != serialHandlers.end(); ++it) {
            if (it->Identifier == identifier) {
                serialHandlers.erase(it);
                ESP_LOGI(TAG, "Successfully unregistered serial handler: %s", identifier.c_str());
                LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER REMOVED: ") + identifier);
                return true;
            }
        }

        ESP_LOGW(TAG, "Handler not found: %s", identifier.c_str());
        LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER NOT FOUND: ") + identifier);
        return false;
    },

    .Update = []() -> void {
        if (!serialInitialized) {
            return;
        }

        // Process any pending serial data flagged by the callback
        if (newDataAvailable) {
            newDataAvailable = false;
            ProcessIncomingSerial();
        }
    },

    .GetStatus = []() -> ConnectionStatus {
        return IsSerialAvailable() ? ConnectionStatus::Connected : ConnectionStatus::Disconnected;
    },

    .GetStatusString = []() -> const char* {
        return IsSerialAvailable() ? "Connected" : "Disconnected";
    },

    .Init = []() -> void {
        ESP_LOGI(TAG, "Initializing Serial transport");
        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL INIT: Starting..."));

        // Serial is already initialized by DeviceManager, so we just set our flag
        serialInitialized = true;
        incomingBuffer = "";
        serialHandlers.clear();
        newDataAvailable = false;

        // Flush any stale data from buffers on initialization
        FlushSerialBuffers();

        // Register the serial receive callback using ESP32's native onReceive method
        HardwareSerial& dataSerial = Hardware::Device::getDataSerial();
        dataSerial.onReceive(onSerialReceive);

        // Log configuration
        ESP_LOGI(TAG, "Configuration: Baud=%d, Buffer=%d, Timeout=%dms, PayloadMax=%d",
                 MESSAGING_SERIAL_BAUD_RATE, MESSAGING_SERIAL_BUFFER_SIZE,
                 MESSAGING_SERIAL_TIMEOUT_MS, MESSAGING_MAX_PAYLOAD_LENGTH);

        // Check if data serial is available
        bool dataAvailable = IsSerialAvailable();
        ESP_LOGI(TAG, "Serial transport initialized with lightweight callback - Data serial available: %s",
                 dataAvailable ? "true" : "false");

        if (dataAvailable) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL INIT: SUCCESS - Connected"));
        } else {
            LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL INIT: WARNING - Not connected"));
        }

        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL CONFIG: ") + String(MESSAGING_SERIAL_BAUD_RATE) + String(" baud"));
    },

    .Deinit = []() -> void {
        ESP_LOGI(TAG, "Deinitializing Serial transport");
        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL DEINIT: Shutting down..."));

        // Unregister the serial receive callback
        if (serialInitialized) {
            HardwareSerial& dataSerial = Hardware::Device::getDataSerial();
            dataSerial.onReceive(nullptr);
        }

        serialInitialized = false;
        serialHandlers.clear();
        incomingBuffer = "";

        ESP_LOGI(TAG, "Serial transport deinitialized");
        LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL DEINIT: Complete"));
    }};

// Helper function implementations
static bool IsSerialAvailable() {
    return Hardware::Device::isDataSerialAvailable();
}

// Lightweight serial receive callback - just signals that data is available
static void onSerialReceive() {
    if (serialInitialized) {
        newDataAvailable = true;
    }
}

// Process incoming serial data - moved from callback to avoid stack overflow
static void ProcessIncomingSerial() {
    if (!IsSerialAvailable()) {
        return;  // Reduced logging to prevent stack usage
    }

    HardwareSerial& dataSerial = Hardware::Device::getDataSerial();

    // Limit how much data we process per call to prevent UART overflow
    const int MAX_CHARS_PER_CALL = 1024;
    int charsProcessed = 0;

    // Read available characters with limits
    while (dataSerial.available() > 0 && charsProcessed < MAX_CHARS_PER_CALL) {
        char c = dataSerial.read();
        charsProcessed++;

        if (c == Protocol::SERIAL_TERMINATOR) {
            // Complete message received - process it
            if (incomingBuffer.length() > 0) {
                if (IsDebugModeEnabled()) {
                    LOG_TO_UI(ui_txtAreaDebugLog, String("SERIAL RX: ") + String(incomingBuffer.length()) + String(" chars"));
                }
                ParseSerialMessage(incomingBuffer);
                incomingBuffer = "";
            }
        } else if (c != '\r') {  // Ignore carriage returns
            incomingBuffer += c;

            // Prevent buffer overflow - use configured buffer size with margin
            const int maxBufferSize = MESSAGING_SERIAL_BUFFER_SIZE;
            if (incomingBuffer.length() > maxBufferSize) {
                ESP_LOGW(TAG, "Serial buffer overflow, clearing (limit: %d)", maxBufferSize);
                if (IsDebugModeEnabled()) {
                    LOG_TO_UI(ui_txtAreaDebugLog, String("BUFFER OVERFLOW: Cleared"));
                }
                incomingBuffer = "";

                // Flush RX buffer to prevent further overflow and recover
                FlushSerialRxBuffer();

                // Continue processing to recover from overflow
            }
        }
    }

    // If we hit the processing limit and there's still data, flag for next update
    if (dataSerial.available() > 0 && charsProcessed >= MAX_CHARS_PER_CALL) {
        newDataAvailable = true;  // Process remaining data in next cycle
    }
}

static void ParseSerialMessage(const String& message) {
    // Rate limiting to prevent stack overflow during data bursts
    unsigned long currentTime = millis();
    if (currentTime - lastJsonProcessTime < MIN_JSON_PROCESS_INTERVAL) {
        // Skip processing if too soon since last JSON parse
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("RATE LIMITED: Skipping JSON parse"));
        }
        return;
    }
    lastJsonProcessTime = currentTime;

    LOG_SERIAL_RX(message.c_str());

    // Message is already clean JSON content - no delimiter processing needed
    String jsonContent = message;
    jsonContent.trim();

    if (jsonContent.length() == 0) {
        ESP_LOGW(TAG, "Empty JSON content");
        return;
    }

    // Use static allocation to reduce stack usage - moved from stack to static memory
    static ArduinoJson::StaticJsonDocument<MESSAGING_MAX_PAYLOAD_LENGTH> doc;
    doc.clear();  // Clear previous content

    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, jsonContent);

    if (error) {
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("JSON FAIL: ") + String(error.c_str()));
        }
        ESP_LOGW(TAG, "JSON parse fail: %s", error.c_str());

        // On JSON parse error, check if we have excessive data in RX buffer
        // This might indicate a data stream corruption, so flush to recover
        int availableData = GetAvailableDataCount();
        if (availableData > 512) {  // If more than 512 bytes pending, likely corrupted stream
            ESP_LOGW(TAG, "Excessive data (%d bytes) after JSON error, flushing RX buffer", availableData);
            FlushSerialRxBuffer();
            if (IsDebugModeEnabled()) {
                LOG_TO_UI(ui_txtAreaDebugLog, String("JSON ERROR RECOVERY: Flushed RX buffer"));
            }
        }

        return;
    }

    // Minimal processing - only log essentials in debug mode
    if (IsDebugModeEnabled()) {
        int keyCount = doc.as<ArduinoJson::JsonObject>().size();
        LOG_TO_UI(ui_txtAreaDebugLog, String("JSON OK: ") + String(keyCount) + String(" keys"));

        // Simplified session info - only log count to reduce stack usage
        if (doc["sessions"].is<ArduinoJson::JsonArray>()) {
            int sessionCount = doc["sessions"].size();
            LOG_TO_UI(ui_txtAreaDebugLog, String("Sessions: ") + String(sessionCount));
        }
    }

    // Normal processing - minimal logging
    String messageType = "STATUS";
    String payload = jsonContent;

    // Find appropriate handler
    Handler* handler = FindSerialHandler(messageType);

    if (handler && handler->Callback) {
        ESP_LOGI(TAG, "Handler: %s", handler->Identifier.c_str());
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("HANDLER: ") + handler->Identifier);
        }
        handler->Callback(messageType.c_str(), payload.c_str());
    } else {
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("NO HANDLER for ") + messageType);
        }
        ESP_LOGW(TAG, "No handler for: %s", messageType.c_str());
    }
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

// Standalone utility function for manual buffer flushing (debugging/recovery)
void FlushSerialTransportBuffers() {
    if (serialInitialized) {
        ESP_LOGI(TAG, "Manual buffer flush requested");
        FlushSerialBuffers();
    } else {
        ESP_LOGW(TAG, "Cannot flush buffers - transport not initialized");
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("FLUSH FAILED: Transport not initialized"));
        }
    }
}

// Buffer management functions
static void FlushSerialRxBuffer() {
    if (!IsSerialAvailable()) {
        return;
    }

    HardwareSerial& dataSerial = Hardware::Device::getDataSerial();
    int discardedBytes = 0;

    // Read and discard all available data from RX buffer
    while (dataSerial.available() > 0) {
        dataSerial.read();
        discardedBytes++;

        // Safety check to prevent infinite loop
        if (discardedBytes > 2048) {
            ESP_LOGW(TAG, "FlushSerialRxBuffer: Too much data, breaking");
            break;
        }
    }

    if (discardedBytes > 0) {
        ESP_LOGI(TAG, "Flushed RX buffer: %d bytes discarded", discardedBytes);
        if (IsDebugModeEnabled()) {
            LOG_TO_UI(ui_txtAreaDebugLog, String("RX BUFFER FLUSHED: ") + String(discardedBytes) + String(" bytes"));
        }
    }
}

static void FlushSerialBuffers() {
    if (!IsSerialAvailable()) {
        return;
    }

    HardwareSerial& dataSerial = Hardware::Device::getDataSerial();

    // Flush TX buffer (wait for outgoing data to be sent)
    dataSerial.flush();

    // Flush RX buffer (discard incoming data)
    FlushSerialRxBuffer();

    // Clear our internal buffer as well
    incomingBuffer = "";

    ESP_LOGI(TAG, "All serial buffers flushed");
    if (IsDebugModeEnabled()) {
        LOG_TO_UI(ui_txtAreaDebugLog, String("ALL BUFFERS FLUSHED"));
    }
}

static int GetAvailableDataCount() {
    if (!IsSerialAvailable()) {
        return 0;
    }

    return Hardware::Device::getDataSerial().available();
}

}  // namespace Messaging::Transports