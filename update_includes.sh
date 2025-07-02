#!/bin/bash

# Systematic Include Path Update Script for Project Refactoring
# This script updates all include paths after the directory restructuring

echo "🔧 Starting systematic include path updates..."

# Function to update includes in all source files
update_includes() {
    echo "Updating: $1 → $2"
    # Escape special characters for sed
    local old_escaped=$(echo "$1" | sed 's/[[\.*^$()+?{|]/\\&/g')
    local new_escaped=$(echo "$2" | sed 's/[[\.*^$()+?{|]/\\&/g')
    
    # Update in all .cpp and .h files
    find src/ -name "*.cpp" -o -name "*.h" | xargs sed -i "s|#include \"$old_escaped\"|#include \"$new_escaped\"|g"
    find src/ -name "*.cpp" -o -name "*.h" | xargs sed -i "s|#include <$old_escaped>|#include <$new_escaped>|g"
}

# FILES MOVING TO src/core/
echo "📁 Updating paths for files moving to src/core/..."
update_includes "application/core/AppController.h" "core/AppController.h"
update_includes "application/core/TaskManager.h" "core/TaskManager.h"
update_includes "../application/core/AppController.h" "../core/AppController.h"
update_includes "../application/core/TaskManager.h" "../core/TaskManager.h"
update_includes "../../application/core/AppController.h" "../../core/AppController.h"
update_includes "../../application/core/TaskManager.h" "../../core/TaskManager.h"

# EVENTS FILES MOVING TO src/core/
echo "📁 Updating paths for events files moving to src/core/..."
update_includes "../events/UiEventHandlers.h" "../core/UiEventHandlers.h"
update_includes "../../events/UiEventHandlers.h" "../../core/UiEventHandlers.h"
update_includes "events/UiEventHandlers.h" "core/UiEventHandlers.h"

# LOGGING FILES MOVING TO src/core/
echo "📁 Updating paths for logging files moving to src/core/..."
update_includes "../logging/CoreLoggingFilter.h" "../core/CoreLoggingFilter.h"
update_includes "../../logging/CoreLoggingFilter.h" "../../core/CoreLoggingFilter.h"
update_includes "logging/CoreLoggingFilter.h" "core/CoreLoggingFilter.h"

# BOOT FILES MOVING TO src/core/
echo "📁 Updating paths for boot files moving to src/core/..."
update_includes "../boot/BootManager.h" "../core/BootManager.h"
update_includes "../../boot/BootManager.h" "../../core/BootManager.h"
update_includes "boot/BootManager.h" "core/BootManager.h"

# OTA FILES MOVING TO src/ota/
echo "📁 Updating paths for OTA files moving to src/ota/..."
update_includes "../hardware/OTAManager.h" "../ota/OTAManager.h"
update_includes "../../hardware/OTAManager.h" "../../ota/OTAManager.h"
update_includes "hardware/OTAManager.h" "ota/OTAManager.h"
update_includes "../boot/OTAManager.h" "../ota/OTAManager.h"

# LOGO SERVICES MOVING TO src/logo/
echo "📁 Updating paths for logo services moving to src/logo/..."
update_includes "../application/services/LogoSupplier.h" "../logo/LogoSupplier.h"
update_includes "../../application/services/LogoSupplier.h" "../../logo/LogoSupplier.h"
update_includes "../application/services/MessageBusLogoSupplier.h" "../logo/MessageBusLogoSupplier.h"
update_includes "../../application/services/MessageBusLogoSupplier.h" "../../logo/MessageBusLogoSupplier.h"
update_includes "application/services/LogoSupplier.h" "logo/LogoSupplier.h"
update_includes "application/services/MessageBusLogoSupplier.h" "logo/MessageBusLogoSupplier.h"

# INTERNAL CROSS-REFERENCES (within moved files)
echo "📁 Updating internal cross-references..."
# These will be handled after files are moved

# AUDIO SYSTEM PATH FIXES (files in deeper directory need ../../ paths)
echo "📁 Fixing audio system include paths..."
# Files in src/application/audio/ need ../../ to reach src/ subdirectories
find src/application/audio/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's|#include "../messaging/|#include "../../messaging/|g' {} \; 2>/dev/null || true
find src/application/audio/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's|#include "../logo/|#include "../../logo/|g' {} \; 2>/dev/null || true
find src/application/audio/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's|#include "../services/|#include "../../logo/|g' {} \; 2>/dev/null || true

echo "✅ Include path updates complete!"
echo "📋 Next steps:"
echo "   1. Run this script after moving files"
echo "   2. Fix any remaining internal includes manually"
echo "   3. Test compilation"