#include "DeviceManager.h"
#include <esp32_smartdisplay.h>
#include <esp_log.h>

namespace Hardware {
namespace Device {

static const char* TAG = "DeviceManager";

bool init(void) {
#ifdef ARDUINO_USB_CDC_ON_BOOT
    delay(2000);  // Wait for Serial to be ready
#endif

    // Initialize Serial interface
    if (!initSerial()) {
        return false;
    }

    // Print system information
    printSystemInfo();

    return true;
}

bool initSerial(void) {
    // Initialize Serial interface
    Serial.begin(115200);

    // Wait for interface to be ready
    delay(100);

    ESP_LOGI(TAG, "Serial interface initialized");

    return true;
}

HardwareSerial& getDataSerial(void) {
    return Serial;  // Return standard Serial interface
}

bool isDataSerialAvailable(void) {
    return Serial;  // Check if Serial is available
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