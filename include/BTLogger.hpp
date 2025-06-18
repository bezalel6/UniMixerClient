#pragma once

/*
 * BTLoggerSender_ESPLog - Enhanced ESP_LOG integration for BTLogger
 *
 * This version hooks into the ESP-IDF logging system to automatically capture
 * all ESP_LOG* calls without requiring any code changes to existing projects.
 *
 * Features:
 * - Zero code changes needed - just include and initialize
 * - Automatically captures ESP_LOGI, ESP_LOGW, ESP_LOGE, ESP_LOGD, ESP_LOGV
 * - Preserves normal serial output while sending to BTLogger
 * - Parses log level and tag automatically
 * - Backward compatible with manual BT_LOG_* calls
 *
 * Usage:
 * 1. Include this file instead of BTLoggerSender.hpp
 * 2. Call BTLoggerSender::begin() in setup() - that's it!
 * 3. All your existing ESP_LOG* calls will automatically be sent to BTLogger
 *
 * Example:
 * BTLoggerSender::begin("MyProject");
 * ESP_LOGI("WIFI", "Connected to %s", ssid);  // ← Automatically sent to BTLogger!
 * ESP_LOGW("SENSOR", "Temperature high: %.1f", temp);  // ← Also sent!
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>

// Log levels (match both ESP_LOG and BTLogger)
enum BTLogLevel {
    BT_DEBUG = 0,  // ESP_LOG_DEBUG
    BT_INFO = 1,   // ESP_LOG_INFO
    BT_WARN = 2,   // ESP_LOG_WARN
    BT_ERROR = 3   // ESP_LOG_ERROR
};

// Default UUIDs (must match BTLogger's BluetoothManager)
#define BTLOGGER_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define BTLOGGER_LOG_CHAR_UUID "87654321-4321-4321-4321-cba987654321"

class BTLoggerSender {
   public:
    // Set BTLogger-specific log level (independent of global ESP_LOG_LEVEL)
    static void setBTLogLevel(BTLogLevel level) {
        _btLogLevel = level;
        ESP_LOGI("BTLOGGER", "BTLogger log level set to: %s", levelToString(level).c_str());
    }

    // Get current BTLogger log level
    static BTLogLevel getBTLogLevel() {
        return _btLogLevel;
    }

    // Initialize with ESP_LOG integration
    static bool begin(const String& deviceName = "ESP32_Dev", bool hookESPLog = true, BTLogLevel btLogLevel = BT_INFO) {
        if (_initialized) return true;

        Serial.println("Initializing BTLogger Sender with ESP_LOG integration...");

        // Set BTLogger log level
        _btLogLevel = btLogLevel;

        // Store original vprintf function
        if (hookESPLog && !_originalVprintf) {
            _originalVprintf = esp_log_set_vprintf(customVprintf);
        }

        // Initialize BLE (same as before)
        BLEDevice::init(deviceName.c_str());

        _server = BLEDevice::createServer();
        _server->setCallbacks(new ServerCallbacks());

        BLEService* service = _server->createService(BTLOGGER_SERVICE_UUID);

        _logCharacteristic = service->createCharacteristic(
            BTLOGGER_LOG_CHAR_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_WRITE |
                BLECharacteristic::PROPERTY_NOTIFY);

        _logCharacteristic->addDescriptor(new BLE2902());
        service->start();

        BLEAdvertising* advertising = BLEDevice::getAdvertising();
        advertising->addServiceUUID(BTLOGGER_SERVICE_UUID);
        advertising->setScanResponse(false);
        advertising->setMinPreferred(0x0);
        BLEDevice::startAdvertising();

        _initialized = true;
        Serial.println("BTLogger Sender initialized with ESP_LOG hook - Device: " + deviceName);

        // Test the integration
        ESP_LOGI("BTLOGGER", "ESP_LOG integration active - BTLogger level: %s", levelToString(_btLogLevel).c_str());
        ESP_LOGI("BTLOGGER", "Note: BTLogger log level is independent of ESP_LOG_LEVEL");

        return true;
    }

    // Disable ESP_LOG hooking (back to manual mode)
    static void disableESPLogHook() {
        if (_originalVprintf) {
            esp_log_set_vprintf(_originalVprintf);
            _originalVprintf = nullptr;
            ESP_LOGI("BTLOGGER", "ESP_LOG hook disabled - switched to manual mode");
        }
    }

    // Manual logging (still available)
    static void log(BTLogLevel level, const String& tag, const String& message) {
        if (!_initialized || !_logCharacteristic) return;

        String logEntry = "[" + String(millis()) + "] [" + levelToString(level) + "] [" + tag + "] " + message;

        _logCharacteristic->setValue(logEntry.c_str());
        _logCharacteristic->notify();
    }

    // Convenience functions (still available)
    static void debug(const String& tag, const String& message) { log(BT_DEBUG, tag, message); }
    static void info(const String& tag, const String& message) { log(BT_INFO, tag, message); }
    static void warn(const String& tag, const String& message) { log(BT_WARN, tag, message); }
    static void error(const String& tag, const String& message) { log(BT_ERROR, tag, message); }

    // Check connection status
    static bool isConnected() {
        return _server && _server->getConnectedCount() > 0;
    }

    // Get statistics
    static uint32_t getLogCount() { return _logCount; }
    static uint32_t getESPLogCount() { return _espLogCount; }

    // Convenience methods for common BTLogger log level scenarios
    static void setDebugMode() { setBTLogLevel(BT_DEBUG); }      // Send everything including debug
    static void setInfoMode() { setBTLogLevel(BT_INFO); }        // Send INFO, WARN, ERROR (recommended)
    static void setWarningMode() { setBTLogLevel(BT_WARN); }     // Send only WARN, ERROR
    static void setErrorOnlyMode() { setBTLogLevel(BT_ERROR); }  // Send only ERROR messages

    // Get human-readable status
    static String getStatus() {
        String status = "BTLogger Status:\n";
        status += "- Connected: " + String(isConnected() ? "Yes" : "No") + "\n";
        status += "- Log Level: " + levelToString(_btLogLevel) + "\n";
        status += "- ESP_LOG messages sent: " + String(_espLogCount) + "\n";
        status += "- Manual logs sent: " + String(_logCount);
        return status;
    }

   private:
    static bool _initialized;
    static BLEServer* _server;
    static BLECharacteristic* _logCharacteristic;
    static vprintf_like_t _originalVprintf;
    static uint32_t _logCount;
    static uint32_t _espLogCount;
    static BTLogLevel _btLogLevel;

    // Custom vprintf function that hooks ESP_LOG output
    static int customVprintf(const char* format, va_list args) {
        // Always call original vprintf first to maintain serial output
        int result = 0;
        if (_originalVprintf) {
            result = _originalVprintf(format, args);
        }

        // Parse and send to BTLogger if initialized
        if (_initialized && _logCharacteristic) {
            parseAndSendESPLog(format, args);
        }

        return result;
    }

    // Parse ESP_LOG formatted string and extract level, tag, message
    static void parseAndSendESPLog(const char* format, va_list args) {
        // Create formatted string
        char buffer[512];
        vsnprintf(buffer, sizeof(buffer), format, args);

        // ESP_LOG format is typically: "LEVEL (timestamp) TAG: message"
        // Example: "I (12345) WIFI: Connected to network"
        String logStr = String(buffer);

        // Parse log level from first character
        BTLogLevel level = BT_INFO;  // default
        if (logStr.length() > 0) {
            switch (logStr[0]) {
                case 'D':
                    level = BT_DEBUG;
                    break;
                case 'I':
                    level = BT_INFO;
                    break;
                case 'W':
                    level = BT_WARN;
                    break;
                case 'E':
                    level = BT_ERROR;
                    break;
                case 'V':
                    level = BT_DEBUG;
                    break;  // Verbose -> Debug
            }
        }

        // Extract tag and message
        String tag = "ESP_LOG";
        String message = logStr;

        // Try to parse "LEVEL (timestamp) TAG: message" format
        int colonPos = logStr.indexOf(": ");
        if (colonPos > 0) {
            // Find the tag (between last space before colon and colon)
            int tagStart = logStr.lastIndexOf(' ', colonPos - 1);
            if (tagStart > 0) {
                tag = logStr.substring(tagStart + 1, colonPos);
                message = logStr.substring(colonPos + 2);
            }
        }

        // Remove any trailing whitespace/newlines
        message.trim();

        // Apply BTLogger-specific filtering (independent of ESP_LOG_LEVEL)
        if (!message.isEmpty() && level >= _btLogLevel) {
            String logEntry = "[" + String(millis()) + "] [" + levelToString(level) + "] [" + tag + "] " + message;
            _logCharacteristic->setValue(logEntry.c_str());
            _logCharacteristic->notify();
            _espLogCount++;
        }
    }

    static String levelToString(BTLogLevel level) {
        switch (level) {
            case BT_DEBUG:
                return "DEBUG";
            case BT_INFO:
                return "INFO";
            case BT_WARN:
                return "WARN";
            case BT_ERROR:
                return "ERROR";
            default:
                return "UNKN";
        }
    }

    // BLE Server callbacks
    class ServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer* server) override {
            Serial.println("BTLogger connected!");
            ESP_LOGI("BTLOGGER", "BTLogger device connected via BLE");
        }

        void onDisconnect(BLEServer* server) override {
            Serial.println("BTLogger disconnected - Restarting advertising...");
            ESP_LOGW("BTLOGGER", "BTLogger device disconnected - restarting advertising");
            BLEDevice::startAdvertising();
        }
    };
};

// Static member initialization
bool BTLoggerSender::_initialized = false;
BLEServer* BTLoggerSender::_server = nullptr;
BLECharacteristic* BTLoggerSender::_logCharacteristic = nullptr;
vprintf_like_t BTLoggerSender::_originalVprintf = nullptr;
uint32_t BTLoggerSender::_logCount = 0;
uint32_t BTLoggerSender::_espLogCount = 0;
BTLogLevel BTLoggerSender::_btLogLevel = BT_INFO;

// Convenience macros (still available for manual use)
#define BT_LOG_DEBUG(tag, msg) BTLoggerSender::debug(tag, msg)
#define BT_LOG_INFO(tag, msg) BTLoggerSender::info(tag, msg)
#define BT_LOG_WARN(tag, msg) BTLoggerSender::warn(tag, msg)
#define BT_LOG_ERROR(tag, msg) BTLoggerSender::error(tag, msg)