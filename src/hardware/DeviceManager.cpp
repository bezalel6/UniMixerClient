#include "DeviceManager.h"
#include <esp32_smartdisplay.h>
#include <esp_log.h>

namespace Hardware {
namespace Device {

static const char* TAG = "DeviceManager";
static bool serialInitialized = false;  // Track serial initialization state

bool init(void) {
#ifdef ARDUINO_USB_CDC_ON_BOOT
    delay(2000);  // Wait for Serial to be ready
#endif

    // Initialize Serial interface (if not already done)
    if (!initSerial()) {
        return false;
    }

    // Print system information
    printSystemInfo();

    return true;
}

bool initSerial(void) {
    // Check if we've already initialized Serial
    if (serialInitialized) {
        ESP_LOGI(TAG, "Serial already initialized - skipping duplicate initialization");
        return true;
    }

    // Check if Serial is already running (initialized by framework/logging)
    bool serialAlreadyRunning = Serial;

    if (serialAlreadyRunning) {
        ESP_LOGI(TAG, "Serial already initialized by system/logging - using existing configuration");
        ESP_LOGI(TAG, "Note: Using default buffer sizes (typically RX=256, TX=256) - cannot resize after initialization");
    } else {
        // Serial not running yet - we can configure it properly
        ESP_LOGI(TAG, "Initializing Serial with custom buffer configuration");

        // Set buffer sizes BEFORE calling begin()
        Serial.setRxBufferSize(1024);
        Serial.setTxBufferSize(512);

        // Now initialize with our baud rate
        Serial.begin(115200);

        ESP_LOGI(TAG, "Serial initialized with enhanced buffers (RX: 1024, TX: 512)");
    }

    // Set timeout for read operations (this works regardless of when Serial was initialized)
    Serial.setTimeout(100);  // 100ms timeout for read operations

    // Wait for interface to be ready
    delay(100);

    // Flush any stale data from hardware buffers on startup
    Serial.flush();                   // Flush TX buffer
    while (Serial.available() > 0) {  // Flush RX buffer
        Serial.read();
    }

    ESP_LOGI(TAG, "Serial interface ready and flushed");

    // Mark as initialized to prevent duplicate initialization
    serialInitialized = true;

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