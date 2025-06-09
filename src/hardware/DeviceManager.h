#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>

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

// Dual USB Serial Interface support
// Note: Use existing 'Serial' for CDC0 (debug), create DataSerial for CDC1 (protocol)
extern USBCDC DataSerial;  // CDC1 - Clean messaging protocol

// Serial interface management
bool initDualSerial(void);
void redirectLogsToDebugSerial(void);
// HWCDC& getDebugSerial(void);  // Returns existing Serial (CDC0)
USBCDC& getDataSerial(void);  // Returns DataSerial (CDC1)
bool isDataSerialAvailable(void);
void testDualSerial(void);  // Test both interfaces

}  // namespace Device
}  // namespace Hardware

#endif  // DEVICE_MANAGER_H