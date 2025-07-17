#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>

namespace Hardware {
namespace Device {

// Device manager initialization
bool init(void);
void deinit(void);

// System information
void printSystemInfo(void);
uint32_t getFreeHeap(void);
uint32_t getPsramSize(void);
const char* getChipModel(void);
uint32_t getCpuFrequency(void);

// RGB LED control (if available)
#ifdef BOARD_HAS_RGB_LED
void ledSetRgb(bool red, bool green, bool blue);
void ledCycleColors(void);
#endif

// CDS (Light sensor) control (if available)
#ifdef BOARD_HAS_CDS
uint32_t readLightSensorMv(void);
#endif

// Timing utilities
unsigned long getMillis(void);
void delay(unsigned long ms);

// NOTE: Serial/UART interface is now managed by SimplifiedSerialEngine
// to avoid driver conflicts between Arduino Serial and ESP-IDF UART drivers

}  // namespace Device
}  // namespace Hardware

#endif  // DEVICE_MANAGER_H
