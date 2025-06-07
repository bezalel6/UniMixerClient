#include "device_manager.h"
#include <esp32_smartdisplay.h>

bool device_manager_init(void) {
#ifdef ARDUINO_USB_CDC_ON_BOOT
    delay(5000);
#endif

    Serial.begin(115200);
    Serial.setDebugOutput(true);

    // Print system information
    device_print_system_info();

    return true;
}

void device_manager_deinit(void) {
    // Cleanup if needed
}

void device_print_system_info(void) {
    log_i("Board: %s", BOARD_NAME);
    log_i("CPU: %s rev%d, CPU Freq: %d Mhz, %d core(s)",
          ESP.getChipModel(), ESP.getChipRevision(),
          getCpuFrequencyMhz(), ESP.getChipCores());
    log_i("Free heap: %d bytes", ESP.getFreeHeap());
    log_i("Free PSRAM: %d bytes", ESP.getPsramSize());
    log_i("SDK version: %s", ESP.getSdkVersion());
}

uint32_t device_get_free_heap(void) {
    return ESP.getFreeHeap();
}

uint32_t device_get_psram_size(void) {
    return ESP.getPsramSize();
}

const char* device_get_chip_model(void) {
    return ESP.getChipModel();
}

uint32_t device_get_cpu_frequency(void) {
    return getCpuFrequencyMhz();
}

#ifdef BOARD_HAS_RGB_LED
void device_led_set_rgb(bool red, bool green, bool blue) {
    smartdisplay_led_set_rgb(red, green, blue);
}

void device_led_cycle_colors(void) {
    static unsigned long last_change = 0;
    unsigned long now = millis();

    if (now - last_change >= 2000) {
        auto rgb = ((now / 2000) % 8);
        device_led_set_rgb(rgb & 0x01, rgb & 0x02, rgb & 0x04);
        last_change = now;
    }
}
#endif

#ifdef BOARD_HAS_CDS
uint32_t device_read_light_sensor_mv(void) {
    return analogReadMilliVolts(CDS);
}
#endif

unsigned long device_get_millis(void) {
    return millis();
}

void device_delay(unsigned long ms) {
    delay(ms);
}