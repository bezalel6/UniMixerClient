# BSOD System Guide

## Overview

The BSOD (Blue Screen of Death) system now supports dynamic configuration with optional sections and custom widgets. This allows for flexible error reporting with customizable content and styling.

## Basic Usage

### Simple BSOD (Legacy)
```cpp
// Simple error with automatic error code generation
BSODHandler::show("Memory allocation failed", __FILE__, __LINE__);
```

### Advanced BSOD with Configuration
```cpp
BSODConfig config;
config.title = "CRITICAL SYSTEM ERROR";
config.message = "Audio subsystem initialization failed";
config.errorCode = "AUDIO_001";
config.technicalDetails = "Failed to initialize I2S driver\nError: -1 (ENOMEM)";
config.showTechnicalDetails = true;
config.backgroundColor = lv_color_hex(0xFF0000);  // Red background
config.textColor = lv_color_white();
config.sadFace = "😱";  // Custom emoji

BSODHandler::show(config);
```

## Configuration Options

### Content Sections

#### Required Fields
- `message`: Main error message (default: "Unknown error")

#### Optional Text Fields
- `title`: Error title (default: "SYSTEM ERROR")
- `errorCode`: Custom error code (default: auto-generated)
- `technicalDetails`: Technical information (default: empty)
- `progressText`: Progress indicator text (default: "System halted")
- `buildInfo`: Build information (default: auto-generated)
- `restartInstruction`: Restart message (default: "Please restart your device")
- `sadFace`: Emoji/face display (default: ":(")

#### Section Visibility Flags
```cpp
config.showSadFace = true;           // Show sad face emoji
config.showTitle = true;             // Show error title
config.showErrorCode = true;         // Show error code
config.showMessage = true;           // Show main message
config.showTechnicalDetails = false; // Show technical details
config.showProgress = true;          // Show progress indicator
config.showBuildInfo = true;         // Show build information
config.showRestartInstruction = true; // Show restart instructions
```

### Styling Options

#### Colors
```cpp
config.backgroundColor = lv_color_hex(0x0078D7);  // Background color
config.textColor = lv_color_white();              // Text color
config.errorCodeColor = lv_color_hex(0xFFD700);   // Error code color
```

#### Effects
```cpp
config.useGlassStyle = true;        // Enable glass effect
config.useShadow = true;            // Enable shadow
config.shadowWidth = 20;            // Shadow width
config.shadowColor = lv_color_hex(0x000000);  // Shadow color
config.shadowOpacity = 60;          // Shadow opacity
config.padding = 24;                // Container padding
```

### Custom Widgets

You can provide custom widgets for any section instead of using the default labels:

```cpp
// Create a custom progress widget
auto customProgress = new UI::Wrapper::CircularProgress("custom_progress");
customProgress->setRange(0, 100)
    .setValue(75)
    .setArcColor(lv_color_hex(0xFF0000));

config.customProgressWidget = customProgress;
config.showProgress = true;

BSODHandler::show(config);
```

## Examples

### Minimal BSOD
```cpp
BSODConfig config;
config.message = "Critical error occurred";
config.showSadFace = false;
config.showTitle = false;
config.showErrorCode = false;
config.showTechnicalDetails = false;
config.showProgress = false;
config.showBuildInfo = false;
config.showRestartInstruction = false;

BSODHandler::show(config);
```

### Technical BSOD
```cpp
BSODConfig config;
config.title = "SYSTEM CRASH";
config.message = "Kernel panic detected";
config.errorCode = "KERNEL_001";
config.technicalDetails = "Stack trace:\n"
                         "0x40000000: main()\n"
                         "0x40001000: init_system()\n"
                         "0x40002000: setup_hardware()";
config.showTechnicalDetails = true;
config.backgroundColor = lv_color_hex(0x8B0000);  // Dark red
config.errorCodeColor = lv_color_hex(0x00FF00);   // Green error code

BSODHandler::show(config);
```

### Custom Styled BSOD
```cpp
BSODConfig config;
config.title = "CUSTOM ERROR";
config.message = "This is a custom styled BSOD";
config.sadFace = "💥";
config.backgroundColor = lv_color_hex(0x4B0082);  // Purple
config.textColor = lv_color_hex(0xFFFF00);       // Yellow text
config.errorCodeColor = lv_color_hex(0x00FFFF);   // Cyan error code
config.useGlassStyle = false;
config.useShadow = false;
config.padding = 32;

BSODHandler::show(config);
```

### BSOD with Custom Widgets
```cpp
// Create custom widgets
auto customTitle = new UI::Wrapper::Label("custom_title", "🚨 CUSTOM ERROR 🚨");
customTitle->setHeadingStyle();

auto customProgress = new UI::Wrapper::ProgressBar("custom_progress");
customProgress->setRange(0, 100)
    .setValue(50)
    .setBarColor(lv_color_hex(0xFF0000))
    .setSize(300, 20);

BSODConfig config;
config.message = "Error with custom widgets";
config.customTitleWidget = customTitle;
config.customProgressWidget = customProgress;
config.showProgress = true;

BSODHandler::show(config);
```

## Macros

The existing macros still work for backward compatibility:

```cpp
// Critical failure
CRITICAL_FAILURE("Something went wrong");

// Assertion with message
ASSERT_CRITICAL(ptr != nullptr, "Pointer is null");

// Critical initialization
INIT_CRITICAL(init_system(), "System initialization failed");
```

## Best Practices

1. **Use meaningful error codes** for easier debugging
2. **Include technical details** when available for better error diagnosis
3. **Customize colors** to match your application's theme
4. **Keep messages concise** but informative
5. **Use custom widgets** for complex error displays
6. **Test BSOD scenarios** during development

## Integration

The BSOD system integrates with:
- **Core Logging Filter**: Automatically disables during BSOD
- **Boot Progress Screen**: Cleans up if visible
- **Task Watchdog**: Disables to prevent reboots during BSOD
- **LVGL Wrapper System**: Uses the new widget wrapper system
