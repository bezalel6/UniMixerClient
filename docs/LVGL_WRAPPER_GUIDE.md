# LVGL Wrapper System Guide

## Overview

The LVGL Wrapper System provides beautiful, functional widgets as drop-in replacements for cumbersome LVGL patterns. This system offers:

- **Modern Styling**: Pre-built themes and styles for professional-looking UI
- **Simplified API**: Easy-to-use wrapper classes that hide LVGL complexity
- **Type Safety**: C++ classes with proper error handling and validation
- **Performance Optimized**: Efficient widget creation and management
- **Extensible**: Easy to add new widgets and styles

## Quick Start

### Basic Usage

```cpp
#include "src/ui/wrapper/LVGLWrapper.h"

using namespace UI::Wrapper;

// Create a modern card container
Container container("my_container");
container.init(lv_scr_act());
container.setSize(200, 150);
container.center();
container.setCardStyle(true);

// Create a label with modern styling
Label label("my_label", "Hello World");
label.init(container.getWidget());
label.setHeadingStyle();

// Create a toggle button
ToggleButton button("my_button", "Click Me");
button.init(container.getWidget());
button.setOnToggle([](bool toggled) {
    ESP_LOGI("UI", "Button toggled: %s", toggled ? "ON" : "OFF");
});

// Create a number input
NumberInput input("my_input", "0");
input.init(container.getWidget());
input.setRange(0, 100);
input.setOnChange([](int value) {
    ESP_LOGI("UI", "Input changed: %d", value);
});
```

### Container Widgets

#### Container
Flexible container with modern styling and flexbox support.

```cpp
Container container("my_container");
container.init(lv_scr_act());
container.setFlexFlow(LV_FLEX_FLOW_COLUMN);
container.setCardStyle(true);  // Modern card appearance
```

#### ScrollContainer
Scrollable container with smooth scrolling.

```cpp
ScrollContainer scroll("my_scroll");
scroll.init(lv_scr_act());
scroll.setScrollDir(LV_DIR_VER);
scroll.scrollTo(0, 100, true);  // Animated scroll
```

### Text Widgets

#### Label
Enhanced label with modern text styling.

```cpp
Label label("my_label", "Hello World");
label.init(lv_scr_act());
label.setHeadingStyle();  // Large, bold text
label.setText("Updated text");
label.appendText(" - appended");
```

#### RichText
Rich text display with formatting support.

```cpp
RichText richText("my_richtext");
richText.init(lv_scr_act());
richText.setContent("**Bold text** and *italic text*");
richText.addColoredText("Colored text", lv_color_hex(0xFF0000));
```

### Input Widgets

#### NumberInput
Number input with range validation and callbacks.

```cpp
NumberInput input("my_input", "0");
input.init(lv_scr_act());
input.setRange(0, 100);
input.setStep(5);
input.setValue(50);
input.setOnChange([](int value) {
    ESP_LOGI("UI", "Input: %d", value);
});
```

#### NumberInput
Number input with range validation.

```cpp
NumberInput numberInput("my_number", "0");
numberInput.init(lv_scr_act());
numberInput.setRange(0, 100);
numberInput.setStep(5);
numberInput.setValue(50);
numberInput.setOnChange([](int value) {
    ESP_LOGI("UI", "Number: %d", value);
});
```

### Button Widgets

#### ToggleButton
Toggle button with state management.

```cpp
ToggleButton button("my_button", "Click Me");
button.init(lv_scr_act());
button.setOnToggle([](bool toggled) {
    ESP_LOGI("UI", "Button toggled: %s", toggled ? "ON" : "OFF");
});
button.setToggled(true);  // Set initial state
```

#### ToggleButton
Toggle button with state management.

```cpp
ToggleButton toggle("my_toggle", "Toggle Me");
toggle.init(lv_scr_act());
toggle.setOnToggle([](bool state) {
    ESP_LOGI("UI", "Toggle: %s", state ? "ON" : "OFF");
});
toggle.setToggled(true);  // Set initial state
```

### Progress Widgets

#### ProgressBar
Enhanced progress bar with multiple styles.

```cpp
ProgressBar progress("my_progress");
progress.init(lv_scr_act());
progress.setRange(0, 100);
progress.setValue(75);
```

#### CircularProgress
Circular progress indicator.

```cpp
CircularProgress circular("my_circular");
circular.init(lv_scr_act());
circular.setRange(0, 100);
circular.setValue(60);
```

### Slider Widgets

#### Slider
Modern slider with themes and callbacks.

```cpp
Slider slider("my_slider");
slider.init(lv_scr_act());
slider.setRange(0, 100);
slider.setValue(50);
slider.setOnChange([](int value) {
    ESP_LOGI("UI", "Slider: %d", value);
});
```

### List Widgets

#### List
Modern list with selection support.

```cpp
List list("my_list");
list.init(lv_scr_act());
list.addItems({"Item 1", "Item 2", "Item 3"});
list.setOnSelect([](int index) {
    ESP_LOGI("UI", "Selected index: %d", index);
});
list.setSelectedIndex(0);  // Select first item
```

### Dialog Widgets

#### Dialog
Modern dialog with overlay and buttons.

```cpp
Dialog dialog("my_dialog");
dialog.init(lv_scr_act());
dialog.setTitle("Confirmation");
dialog.setMessage("Are you sure you want to continue?");
dialog.setButtons({"Cancel", "OK"});
dialog.setOnButtonClick([](int buttonIndex) {
    if (buttonIndex == 1) {
        ESP_LOGI("UI", "User confirmed");
    }
});
```
dialog.init(lv_scr_act());
dialog.setTitle("Confirmation");
dialog.setMessage("Are you sure you want to continue?");
dialog.setButtons({"Yes", "No", "Cancel"});
dialog.setOnButtonClick([](int buttonIndex) {
    ESP_LOGI("UI", "Button clicked: %d", buttonIndex);
});
dialog.show();
```

#### Quick Dialogs
Pre-built dialog types for common use cases.

```cpp
// Info dialog
Dialog::showInfo("Information", "This is an informational message.");

// Warning dialog
Dialog::showWarning("Warning", "This action cannot be undone.");

// Error dialog
Dialog::showError("Error", "Something went wrong.");

// Confirmation dialog
Dialog::showConfirm("Confirm", "Are you sure?", [](bool confirmed) {
    if (confirmed) {
        ESP_LOGI("UI", "User confirmed");
    } else {
        ESP_LOGI("UI", "User cancelled");
    }
});
```

## Advanced Features

### Custom Styling

All widgets support custom styling through the base `WidgetBase` methods:

```cpp
ToggleButton button("my_button", "Custom Button");
button.init(lv_scr_act());

// Custom styling
button.setBackgroundColor(lv_color_hex(0xFF6B6B));
button.setTextColor(lv_color_hex(0xFFFFFF));
button.setRadius(20);
button.setPadding(16);
```

### Widget Lifecycle

```cpp
Container container("my_container");

// Create widget
if (container.init(lv_scr_act())) {
    ESP_LOGI("UI", "Container created successfully");
}

// Check if ready
if (container.isReady()) {
    ESP_LOGI("UI", "Container is ready for use");
}

// Show/hide
container.show();
container.hide();
container.setVisible(true);

// Cleanup
container.destroy();
```

### Event Handling

All interactive widgets support callback functions:

```cpp
ToggleButton button("my_button", "Interactive");
button.init(lv_scr_act());
button.setOnToggle([](bool toggled) {
    ESP_LOGI("UI", "Button toggled: %s", toggled ? "ON" : "OFF");
});

NumberInput input("my_input", "0");
input.init(lv_scr_act());
input.setOnChange([](int value) {
    ESP_LOGI("UI", "Input changed: %d", value);
});
```

### Performance Tips

1. **Reuse Widgets**: Don't create/destroy widgets frequently
2. **Batch Updates**: Update multiple properties at once
3. **Use Callbacks**: Avoid polling for state changes
4. **Memory Management**: Always call `destroy()` when done

```cpp
// Good: Reuse widget
ToggleButton button("my_button", "Click Me");
button.init(lv_scr_act());
button.setText("Updated Text");  // Reuse same widget

// Bad: Create new widget each time
// ToggleButton button("my_button", "Updated Text");
// button.init(lv_scr_act());
```

## Migration from Raw LVGL

### Before (Raw LVGL)
```cpp
// Create container
lv_obj_t* container = lv_obj_create(lv_scr_act());
lv_obj_set_size(container, 200, 150);
lv_obj_center(container);
lv_obj_set_style_bg_color(container, lv_color_hex(0xFFFFFF), 0);
lv_obj_set_style_radius(container, 12, 0);
lv_obj_set_style_shadow_width(container, 8, 0);
lv_obj_set_style_shadow_color(container, lv_color_hex(0x000000), 0);
lv_obj_set_style_shadow_opa(container, 30, 0);
lv_obj_set_style_pad_all(container, 16, 0);

// Create button
lv_obj_t* button = lv_btn_create(container);
lv_obj_set_size(button, 100, 40);
lv_obj_center(button);
lv_obj_set_style_bg_color(button, lv_color_hex(0x007BFF), 0);
lv_obj_set_style_text_color(button, lv_color_hex(0xFFFFFF), 0);
lv_obj_set_style_radius(button, 8, 0);

// Create label
lv_obj_t* label = lv_label_create(button);
lv_label_set_text(label, "Click Me");
lv_obj_center(label);

// Add event callback
lv_obj_add_event_cb(button, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI("UI", "Button clicked!");
    }
}, LV_EVENT_CLICKED, nullptr);
```

### After (LVGL Wrapper)
```cpp
// Create card container
Container container("my_container");
container.init(lv_scr_act());
container.setSize(200, 150);
container.center();
container.setCardStyle(true);

// Create button with modern styling
ToggleButton button("my_button", "Click Me");
button.init(container.getWidget());
button.setOnToggle([](bool toggled) {
    ESP_LOGI("UI", "Button toggled: %s", toggled ? "ON" : "OFF");
});
```

## Best Practices

1. **Use Descriptive IDs**: Always provide meaningful widget IDs for debugging
2. **Group Related Widgets**: Use containers to organize related UI elements
3. **Consistent Styling**: Use the provided style methods for consistency
4. **Error Handling**: Check return values from `create()` methods
5. **Memory Management**: Call `destroy()` when widgets are no longer needed

```cpp
// Good: Proper error handling and cleanup
Container container("my_container");
if (!container.init(lv_scr_act())) {
    ESP_LOGE("UI", "Failed to create container");
    return false;
}

// Use container...

// Cleanup when done
container.destroy();
```

## Troubleshooting

### Common Issues

1. **Widget not visible**: Check if parent is visible and properly sized
2. **Callback not firing**: Ensure callback is set before creating widget
3. **Memory leaks**: Always call `destroy()` on widgets
4. **Performance issues**: Avoid creating widgets in tight loops

### Debug Tips

```cpp
// Enable debug logging
#define LVGL_WRAPPER_DEBUG 1

// Check widget state
if (widget.isReady()) {
    ESP_LOGI("UI", "Widget %s is ready", widget.getId().c_str());
} else {
    ESP_LOGW("UI", "Widget %s is not ready", widget.getId().c_str());
}
```

## Conclusion

The LVGL Wrapper System provides a modern, type-safe, and performant way to create beautiful UI components. By abstracting away the complexity of raw LVGL calls, developers can focus on creating great user experiences rather than wrestling with low-level widget management.

For more examples and advanced usage patterns, see the test files and example applications in the project.
