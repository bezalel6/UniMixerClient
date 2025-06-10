# Deserialization Debug System

This system provides comprehensive debugging capabilities for serial message processing with toggleable modes.

## Configuration

### Compile-Time Toggle

In `include/MessagingConfig.h`:

```cpp
#define MESSAGING_DESERIALIZATION_DEBUG_MODE 0  // 0 = Normal, 1 = Debug UI mode
```

### Runtime Toggle

```cpp
#include "DebugUtils.h"

// Enable debug mode at runtime
EnableDebugMode();

// Disable debug mode at runtime
DisableDebugMode();

// Check if debug mode is active
if (IsDebugModeEnabled()) {
    // Debug mode is active
}
```

## Debug Mode Features

### When Debug Mode is ENABLED:

1. **No Message Processing**: Messages are not forwarded to handlers
2. **UI Logging**: All messages are logged to `txtAreaDebugLog` UI component
3. **JSON Analysis**: Detailed JSON parsing analysis with error reporting
4. **Enhanced Logging**: Comprehensive ESP_LOG output with debug tags
5. **TX/RX Tracking**: Both incoming and outgoing messages logged to UI

### When Debug Mode is DISABLED (Normal Mode):

1. **Full Processing**: Messages processed normally and forwarded to handlers
2. **Enhanced Logging**: JSON validation and detailed handler information
3. **Performance Optimized**: Minimal overhead for production use

## UI Debug Log Format

```
[timestamp] RX: {"sessions":[...]}
[timestamp] JSON Parse OK - Keys: sessions, deviceId, timestamp,
[timestamp] TX: {"commandType":"SetVolume","volume":0.5}
[timestamp] JSON Parse Error: InvalidInput
```

## Usage Examples

### Enable Debug Mode for Troubleshooting

1. Set `MESSAGING_DESERIALIZATION_DEBUG_MODE 1` in MessagingConfig.h
2. Build and flash firmware
3. Navigate to Debug screen in UI
4. Watch real-time message analysis in txtAreaDebugLog

### Runtime Toggle During Operation

```cpp
// In your event handler or button callback
if (troubleshooting_needed) {
    EnableDebugMode();
    ESP_LOGI("USER", "Debug mode enabled - check UI for message logs");
} else {
    DisableDebugMode();
    ESP_LOGI("USER", "Normal processing resumed");
}
```

## Logging Macros Available

```cpp
LOG_SERIAL_RX(message)     // Log incoming serial data
LOG_SERIAL_TX(message)     // Log outgoing serial data
LOG_JSON_PARSE_OK(keys)    // Log successful JSON parsing
LOG_JSON_PARSE_ERROR(err)  // Log JSON parsing errors
LOG_TO_UI(element, msg)    // Log to any UI text area
DEBUG_LOG(message)         // Debug mode specific logging
DEBUG_LOG_F(fmt, args...)  // Debug mode formatted logging
```

## Memory Management

- UI debug log automatically limits to 2000 characters
- Truncates older entries when limit reached
- JSON parsing uses 1024 byte buffer for analysis
- No memory leaks in debug mode operation

## Integration Notes

- Compatible with existing message handlers
- No impact on MQTT transport
- Works with both compile-time and runtime toggles
- UI elements safely checked for null before use
