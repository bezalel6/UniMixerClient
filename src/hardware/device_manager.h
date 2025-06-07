#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Device manager initialization
bool device_manager_init(void);
void device_manager_deinit(void);

// System information
void device_print_system_info(void);
uint32_t device_get_free_heap(void);
uint32_t device_get_psram_size(void);
const char* device_get_chip_model(void);
uint32_t device_get_cpu_frequency(void);

// RGB LED control (if available)
#ifdef BOARD_HAS_RGB_LED
void device_led_set_rgb(bool red, bool green, bool blue);
void device_led_cycle_colors(void);
#endif

// CDS (Light sensor) control (if available)
#ifdef BOARD_HAS_CDS
uint32_t device_read_light_sensor_mv(void);
#endif

// Timing utilities
unsigned long device_get_millis(void);
void device_delay(unsigned long ms);

#ifdef __cplusplus
}
#endif

#endif  // DEVICE_MANAGER_H