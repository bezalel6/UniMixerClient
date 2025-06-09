#include "DeviceManager.h"
#include <esp32_smartdisplay.h>
#include <esp_log.h>

namespace Hardware {
namespace Device {

// Dual USB Serial interface instances
// Note: Using existing 'Serial' for CDC0 (debug), creating DataSerial for CDC1 (protocol)
USBCDC DataSerial;  // CDC1 - Clean messaging protocol

static const char* TAG = "DeviceManager";
static bool dataSerialInitialized = false;

bool init(void) {
#ifdef ARDUINO_USB_CDC_ON_BOOT
    delay(2000);  // Reduced delay since we have dual interfaces
#endif

    // Initialize dual USB serial interfaces
    if (!initDualSerial()) {
        return false;
    }

    // Redirect ESP logging to debug interface
    redirectLogsToDebugSerial();

    // Print system information on debug interface
    printSystemInfo();

    // Test dual serial interfaces
    testDualSerial();

    return true;
}

bool initDualSerial(void) {
    // Initialize USB subsystem
    // USB.begin();

    // Initialize existing Serial (CDC0) for debug/logs
    // Serial.begin(115200);
    Serial.setDebugOutput(true);

    // Initialize additional CDC interface for clean messaging
    DataSerial.begin(115200);      // CDC1 - Clean messaging interface
    dataSerialInitialized = true;  // Mark as initialized

    // Wait for interfaces to be ready
    delay(100);

    ESP_LOGI(TAG, "Dual USB Serial interfaces initialized");
    ESP_LOGI(TAG, "Debug interface: CDC0 (Serial) - logs and debug output");
    ESP_LOGI(TAG, "Data interface: CDC1 (DataSerial) - messaging protocol");

    return true;
}

void redirectLogsToDebugSerial(void) {
    // ESP_LOG already goes to Serial by default, no redirection needed
    ESP_LOGI(TAG, "ESP logging using default Serial interface (CDC0)");
}

USBCDC& getDataSerial(void) {
    return DataSerial;  // Return our clean messaging interface (CDC1)
}

bool isDataSerialAvailable(void) {
    // Return true if DataSerial was properly initialized
    // This allows messaging to work even if no host is connected to CDC1
    return dataSerialInitialized;
}

void testDualSerial(void) {
    ESP_LOGI(TAG, "Testing dual serial interfaces...");

    // Test debug interface (Serial - CDC0)
    Serial.println("DEBUG: This message appears on CDC0 (Serial debug interface)");

    // Test data interface (DataSerial - CDC1)
    DataSerial.printf("%s:%s\n",
                      "homeassistant/smartdisplay/test",
                      "{\"messageType\":\"system.test\",\"status\":\"dual_serial_working\"}");
    DataSerial.flush();

    ESP_LOGI(TAG, "Dual serial test completed");
    ESP_LOGI(TAG, "Check CDC0 (Serial) for debug, CDC1 (DataSerial) for protocol");
}

void deinit(void) {
    // Cleanup if needed
}

void printSystemInfo(void) {
    log_i("Board: %s", BOARD_NAME);
    log_i("CPU: %s rev%d, CPU Freq: %d Mhz, %d core(s)",
          ESP.getChipModel(), ESP.getChipRevision(),
          getCpuFrequencyMhz(), ESP.getChipCores());
    log_i("Free heap: %d bytes", ESP.getFreeHeap());
    log_i("Free PSRAM: %d bytes", ESP.getPsramSize());
    log_i("SDK version: %s", ESP.getSdkVersion());
}

uint32_t getFreeHeap(void) {
    return ESP.getFreeHeap();
}

uint32_t getPsramSize(void) {
    return ESP.getPsramSize();
}

const char* getChipModel(void) {
    return ESP.getChipModel();
}

uint32_t getCpuFrequency(void) {
    return getCpuFrequencyMhz();
}

#ifdef BOARD_HAS_RGB_LED
void ledSetRgb(bool red, bool green, bool blue) {
    smartdisplay_led_set_rgb(red, green, blue);
}

void ledCycleColors(void) {
    static unsigned long last_change = 0;
    unsigned long now = millis();

    if (now - last_change >= 2000) {
        auto rgb = ((now / 2000) % 8);
        ledSetRgb(rgb & 0x01, rgb & 0x02, rgb & 0x04);
        last_change = now;
    }
}
#endif

#ifdef BOARD_HAS_CDS
uint32_t readLightSensorMv(void) {
    return analogReadMilliVolts(CDS);
}
#endif

unsigned long getMillis(void) {
    return millis();
}

void delay(unsigned long ms) {
    ::delay(ms);
}

}  // namespace Device
}  // namespace Hardware