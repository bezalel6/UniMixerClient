# LVGL Wrapper System

This directory contains a structured LVGL wrapper system that provides a clean, object-oriented interface to LVGL widgets.

## Structure

```
src/ui/wrapper/
├── base/                    # Base classes and utilities
│   ├── WidgetBase.h        # Base widget class with common functionality
│   └── WidgetBase.cpp      # Base widget implementation
├── containers/             # Container widgets
│   ├── Container.h         # Container and ScrollContainer classes
│   └── Container.cpp       # Container implementations
├── widgets/               # Text widgets
│   ├── TextWidgets.h      # Label and RichText classes
│   └── TextWidgets.cpp    # Text widget implementations
├── inputs/                # Input widgets
│   ├── InputWidgets.h     # NumberInput and ToggleButton classes
│   └── InputWidgets.cpp   # Input widget implementations
├── progress/              # Progress widgets
│   ├── ProgressWidgets.h  # ProgressBar and CircularProgress classes
│   └── ProgressWidgets.cpp # Progress widget implementations
├── controls/              # Control widgets
│   ├── ControlWidgets.h   # Slider and List classes
│   └── ControlWidgets.cpp # Control widget implementations
├── dialogs/               # Dialog system
│   ├── Dialog.h           # Dialog class
│   └── Dialog.cpp         # Dialog implementations
├── LVGLWrapper.h          # Main header that includes all widgets
└── README.md              # This file
```

## Features

### Base System
- **WidgetBase**: Common functionality for all widgets
- **Property**: Type-safe property system
- **EventHandler**: Event callback management
- **StyleManager**: Property-based styling

### Container Widgets
- **Container**: Flexible container with modern styling
- **ScrollContainer**: Scrollable container with snap points

### Text Widgets
- **Label**: Simple text display with style presets
- **RichText**: Rich text with formatting support

### Input Widgets
- **NumberInput**: Numeric input with validation
- **ToggleButton**: Toggle button with state management

### Progress Widgets
- **ProgressBar**: Linear progress indicator
- **CircularProgress**: Circular progress indicator

### Control Widgets
- **Slider**: Value slider with callbacks
- **List**: Interactive list with selection

### Dialog System
- **Dialog**: Modal dialog with buttons
- Static methods for common dialogs (info, warning, error, confirm)

## Usage

### Basic Usage

```cpp
#include "src/ui/wrapper/LVGLWrapper.h"

using namespace UI::Wrapper;

// Create a container
Container container("main_container");
container.init()
    .setCardStyle(true)
    .setSize(320, 240);

// Create a label
Label label("title_label", "Hello World");
label.init(container.getWidget())
    .setHeadingStyle()
    .setPosition(10, 10);

// Create a button
ToggleButton button("my_button", "Click Me");
button.init(container.getWidget())
    .setPosition(10, 50)
    .setOnToggle([](bool toggled) {
        printf("Button toggled: %s\n", toggled ? "ON" : "OFF");
    });
```

### Using Convenience Templates

```cpp
// Create widgets with automatic initialization
auto container = createContainer<Container>("main", lv_scr_act());
auto label = createWidget<Label>("title", container.getWidget());
auto slider = createWidget<Slider>("volume", container.getWidget());
```

### Dialog Usage

```cpp
// Show a simple info dialog
Dialog::showInfo("Success", "Operation completed successfully");

// Show a confirmation dialog
Dialog::showConfirm("Delete", "Are you sure?", [](bool confirmed) {
    if (confirmed) {
        // Handle deletion
    }
});
```

## Benefits

1. **Modular Design**: Each widget type is in its own file
2. **Type Safety**: Strong typing with validation
3. **Fluent Interface**: Method chaining for easy configuration
4. **Event Handling**: Clean callback system
5. **Style Presets**: Pre-configured styles for common use cases
6. **Error Handling**: Proper validation and logging
7. **Memory Management**: Automatic cleanup and lifecycle management

## Migration from Old System

The old monolithic `LVGLWrapper.h` and `LVGLWrapper.cpp` files have been split into this structured system. To migrate:

1. Replace `#include "LVGLWrapper.h"` with `#include "src/ui/wrapper/LVGLWrapper.h"`
2. Update any direct class references to use the new namespace structure
3. The API remains largely the same, with improved organization

## Future Enhancements

- Additional widget types (charts, tables, etc.)
- Theme system for consistent styling
- Animation system for smooth transitions
- Layout managers for automatic positioning
- Data binding for reactive UIs
