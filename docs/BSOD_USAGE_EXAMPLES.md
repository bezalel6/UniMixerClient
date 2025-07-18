# Modern BSOD System Usage Examples

The redesigned BSOD system provides a clean, intuitive interface for displaying critical errors with better visual hierarchy and user experience.

## Visual Design Features

### Modern UI Elements
- **Card-based layout** with subtle shadows and borders
- **Gradient background** for depth
- **Color-coded elements** for quick understanding
- **Progress bars** for system status
- **Icon system** for visual clarity
- **Responsive layout** that adapts to content

### Color Palette
- **Background**: Deep blue-gray gradient (#1E3A5F → #0F1E2E)
- **Cards**: Lighter blue-gray (#2B4C7E)
- **Text**: White primary, light blue secondary
- **Accents**: Bright blue for headers, color-coded for status
- **Error indicators**: Soft red, yellow warnings, teal success

## Basic Usage

### Simple Error
```cpp
// Basic critical failure
CRITICAL_FAILURE("System component failed to respond");
```

### Using Templates

#### Memory Error
```cpp
// Out of memory error with details
if (esp_get_free_heap_size() < 1000) {
    MEMORY_CRITICAL("Heap exhausted during image processing");
}

// Or with custom config
auto config = BSODHandler::createMemoryErrorConfig();
config.message = "Failed to allocate buffer for audio processing";
config.technicalDetails = "Required: 64KB, Available: 2KB";
BSODHandler::show(config);
```

#### Hardware Failure
```cpp
// SD card failure
HARDWARE_CRITICAL("SD Card", "Failed to mount filesystem");

// Display failure with custom details
auto config = BSODHandler::createHardwareErrorConfig("Display");
config.technicalDetails = "SPI communication timeout\nNo response from display controller";
config.showCpuStatus = true;  // Show real-time diagnostics
BSODHandler::show(config);
```

#### Initialization Error
```cpp
// Component initialization failure
INIT_CRITICAL(Display::init(), "Display hardware could not be initialized");

// Or with more details
if (!WiFi.begin()) {
    auto config = BSODHandler::createInitErrorConfig("WiFi Module");
    config.technicalDetails = "SSID: MyNetwork\nError: WL_CONNECT_FAILED";
    config.restartInstruction = "Check WiFi credentials and try again";
    BSODHandler::show(config);
}
```

#### Task Failure
```cpp
// Task crash with auto-restart
TASK_CRITICAL("AudioProcessor", "Stack overflow detected");

// Custom task error
auto config = BSODHandler::createTaskErrorConfig("MessageHandler");
config.message = "The message processing task has stopped responding";
config.showProgress = true;  // Shows progress bar
config.restartInstruction = "System will restart in 10 seconds...";
BSODHandler::show(config);
```

#### Assertion Failure
```cpp
// Simple assertion
ASSERT_CRITICAL(buffer != nullptr, "Buffer allocation failed");

// Detailed assertion
ASSERT_CRITICAL(temperature < 85, "CPU temperature exceeds safe limits");
```

## Custom BSOD Configuration

### Full Custom Configuration
```cpp
BSODConfig config;
config.title = "CUSTOM ERROR TITLE";
config.message = "A detailed description of what went wrong";
config.errorCode = "ERR_CUSTOM_001";

// Visual customization
config.backgroundColor = lv_color_hex(0x1E3A5F);  // Default blue-gray
config.textColor = lv_color_hex(0xFFFFFF);        // White
config.errorCodeColor = lv_color_hex(0xFF6B6B);   // Soft red

// Content sections
config.showSadFace = false;           // Hide the sad face icon
config.showTitle = true;              // Show title
config.showMessage = true;            // Show main message
config.showErrorCode = true;          // Show error code chip
config.showTechnicalDetails = true;   // Show technical details card
config.showCpuStatus = true;          // Show real-time diagnostics
config.showProgress = false;          // No progress bar
config.showBuildInfo = true;          // Show build info in footer
config.showRestartInstruction = true; // Show restart instructions

// Technical details
config.technicalDetails = "Stack trace:\n  0x4008xxxx: function_a\n  0x4008xxxx: function_b";
config.restartInstruction = "Hold the power button for 5 seconds to restart";
config.buildInfo = getBuildInfo();  // Auto-populated if empty

BSODHandler::show(config, __FILE__, __LINE__);
```

### Minimal Error Screen
```cpp
BSODConfig config;
config.title = "OOPS!";
config.message = "Something went wrong. Please restart.";
config.showErrorCode = false;
config.showTechnicalDetails = false;
config.showCpuStatus = false;
config.showBuildInfo = false;
BSODHandler::show(config);
```

## Real-time Diagnostics

The BSOD system shows live system status when `showCpuStatus` is enabled:

- **CPU Usage Bar**: Visual representation of processor load
- **Memory Usage Bar**: Heap and PSRAM utilization
- **Live Metrics**: Updated every 2 seconds
  - Core information
  - CPU frequency
  - Free heap/PSRAM
  - Stack usage
  - Task status

## Visual Examples

### Memory Error
```
┌─────────────────────────────────────┐
│ ⚠ OUT OF MEMORY                     │
│   Occurred at 14:23:45              │
├─────────────────────────────────────┤
│ The system has run out of available │
│ memory and cannot continue.         │
│ [ERR_NO_MEMORY]                     │
├─────────────────────────────────────┤
│ Technical Details                   │
│ ┌─────────────────────────────────┐ │
│ │ Heap exhausted during image     │ │
│ │ processing                      │ │
│ └─────────────────────────────────┘ │
├─────────────────────────────────────┤
│ System Diagnostics                  │
│ CPU Usage    [████░░░░░░] 40%      │
│ Memory Usage [█████████░] 95%      │
│ ┌─────────────────────────────────┐ │
│ │ Core: 0 | CPU: 240 MHz         │ │
│ │ Heap: 2048 / 327680 bytes      │ │
│ └─────────────────────────────────┘ │
├─────────────────────────────────────┤
│  Please restart the device to free  │
│         up memory.                  │
└─────────────────────────────────────┘
```

### Hardware Failure
```
┌─────────────────────────────────────┐
│ ⚠ HARDWARE FAILURE                  │
│   Occurred at 09:15:32              │
├─────────────────────────────────────┤
│ Critical hardware failure:          │
│ SD Card                             │
│ [ERR_HARDWARE]                      │
├─────────────────────────────────────┤
│ Technical Details                   │
│ ┌─────────────────────────────────┐ │
│ │ Failed to mount filesystem      │ │
│ │ Error code: 0x10002             │ │
│ └─────────────────────────────────┘ │
├─────────────────────────────────────┤
│  Power cycle the device. If problem │
│    persists, contact support.       │
└─────────────────────────────────────┘
```

## Best Practices

1. **Use Templates**: Prefer predefined templates for consistency
2. **Provide Context**: Always include technical details when available
3. **User-Friendly Messages**: Keep main message simple and clear
4. **Technical Details**: Put debug info in the technical details section
5. **Actionable Instructions**: Tell users what they can do
6. **Error Codes**: Use consistent error code format (ERR_CATEGORY_SPECIFIC)

## Migration from Old BSOD

### Old Style
```cpp
BSODHandler::show("Display init failed", __FILE__, __LINE__);
```

### New Style
```cpp
// Option 1: Use template
auto config = BSODHandler::createInitErrorConfig("Display");
BSODHandler::show(config);

// Option 2: Use macro
INIT_CRITICAL(Display::init(), "Display initialization failed");

// Option 3: Full custom
BSODConfig config;
config.title = "DISPLAY ERROR";
config.message = "Failed to initialize display hardware";
config.errorCode = "ERR_DISPLAY_INIT";
config.showCpuStatus = true;
BSODHandler::show(config);
```

## Testing BSOD Screens

```cpp
// Test different error types
void testBSODScreens() {
    // Test 1: Memory error
    BSODHandler::testDualCoreBSOD();
    
    // Test 2: Hardware error
    auto hwConfig = BSODHandler::createHardwareErrorConfig("Test Component");
    hwConfig.message = "This is a test hardware failure";
    BSODHandler::show(hwConfig);
    
    // Test 3: Network error
    auto netConfig = BSODHandler::createNetworkErrorConfig();
    netConfig.technicalDetails = "Test network failure\nNo actual error";
    BSODHandler::show(netConfig);
}
```

The new BSOD system provides a much cleaner, more professional appearance while maintaining all the diagnostic capabilities of the original system.