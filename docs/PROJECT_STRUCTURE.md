# ESP32 Smart Display Project Structure

This project has been reorganized into a modular, scalable architecture that separates concerns and promotes code reusability.

## Project Structure

```
esp32-smartdisplay-demo/
├── src/
│   ├── main.cpp                    # Clean entry point
│   ├── application/                # Application layer
│   │   ├── app_controller.h        # Main application controller
│   │   └── app_controller.cpp      # Application logic and coordination
│   ├── display/                    # Display management layer
│   │   ├── display_manager.h       # Display abstraction interface
│   │   └── display_manager.cpp     # LVGL and display functionality
│   ├── hardware/                   # Hardware abstraction layer
│   │   ├── device_manager.h        # Device abstraction interface
│   │   └── device_manager.cpp      # ESP32 hardware functionality
│   ├── events/                     # UI event handling
│   │   ├── ui_event_handlers.h     # Event handler interface
│   │   └── ui_event_handlers.cpp   # UI event implementations
│   └── ui/                         # Generated SquareLine Studio code
│       └── [Generated files]       # DO NOT MODIFY - Auto-generated
├── docs/                           # Project documentation
└── [Other project files]
```

## Architecture Overview

### 1. Application Layer (`src/application/`)

- **Purpose**: High-level application logic and coordination
- **Key Functions**:
  - Initialize all subsystems
  - Coordinate between display, hardware, and UI layers
  - Manage application state and periodic updates
  - Configure application constants

### 2. Display Management Layer (`src/display/`)

- **Purpose**: Abstract display and LVGL functionality
- **Key Functions**:
  - Display initialization and management
  - LVGL tick handling and timer updates
  - Display rotation management
  - QR code creation and management
  - UI component update helpers
  - LVGL abstraction for easy use

### 3. Hardware Abstraction Layer (`src/hardware/`)

- **Purpose**: Abstract ESP32 hardware functionality
- **Key Functions**:
  - System initialization and information
  - RGB LED control (if available)
  - Light sensor reading (if available)
  - Memory and system status queries
  - Timing utilities

### 4. Event Handling Layer (`src/events/`)

- **Purpose**: Handle UI events separately from main logic
- **Key Functions**:
  - UI button and interaction callbacks
  - Event routing and handling
  - User interaction logic

### 5. UI Layer (`src/ui/`)

- **Purpose**: SquareLine Studio generated code
- **Important**: DO NOT MODIFY - This folder contains auto-generated code

## Benefits of This Structure

1. **Separation of Concerns**: Each layer has a specific responsibility
2. **Modularity**: Components can be tested and modified independently
3. **Reusability**: Display and hardware managers can be reused in other projects
4. **Maintainability**: Clean, organized code that's easy to understand
5. **Scalability**: Easy to add new features without bloating main.cpp
6. **Testability**: Each module can be unit tested independently

## Usage Examples

### Adding a New UI Component

```cpp
// In app_controller.cpp
void app_controller_setup_ui_components(void)
{
    // Create new components using display manager
    display_create_qr_code(ui_scrMain, "Your data", 150);

    // Set up initial states
    display_update_label_string(ui_lblStatus, "Ready");
}
```

### Adding New Hardware Functionality

```cpp
// In device_manager.h/.cpp
void device_control_new_sensor(void);
uint32_t device_read_new_sensor(void);
```

### Adding New Display Features

```cpp
// In display_manager.h/.cpp
void display_create_chart(lv_obj_t* parent, const char* title);
void display_update_chart_data(lv_obj_t* chart, int32_t* data, size_t count);
```

## Key Design Principles

1. **Interface Segregation**: Each header file defines a clear interface
2. **Single Responsibility**: Each module has one primary purpose
3. **Dependency Inversion**: Higher-level modules don't depend on low-level details
4. **Open/Closed Principle**: Easy to extend without modifying existing code

## Getting Started

The main.cpp file is now extremely simple:

1. Initialize the application controller
2. Run the main application loop

All complexity is properly abstracted into dedicated managers that can be easily maintained and extended.
