#pragma once

// =============================================================================
// LVGL WRAPPER SYSTEM - MAIN HEADER
// =============================================================================

// Base classes and utilities
#include "base/WidgetBase.h"

// Container widgets
#include "containers/Container.h"

// Text widgets
#include "widgets/TextWidgets.h"

// Input widgets
#include "inputs/InputWidgets.h"

// Progress widgets
#include "progress/ProgressWidgets.h"

// Control widgets
#include "controls/ControlWidgets.h"

// Dialog system
#include "dialogs/Dialog.h"

// =============================================================================
// CONVENIENCE TEMPLATES AND UTILITIES
// =============================================================================

namespace UI {
namespace Wrapper {

// Template for creating widgets with automatic initialization
template <typename WidgetType>
WidgetType createWidget(const std::string& id, lv_obj_t* parent = nullptr) {
    WidgetType widget(id);
    widget.init(parent);
    return widget;
}

// Template for creating containers with automatic initialization
template <typename ContainerType>
ContainerType createContainer(const std::string& id, lv_obj_t* parent = nullptr) {
    ContainerType container(id);
    container.init(parent);
    return container;
}

}  // namespace Wrapper
}  // namespace UI
