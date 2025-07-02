#!/bin/bash

# Master Project Refactoring Execution Script
# Orchestrates the complete directory reorganization and include path updates

set -e  # Exit on any error

echo "🚀 Starting Project Refactoring..."
echo "📋 This will reorganize the entire src/ directory structure"
echo ""

# Safety check
read -p "⚠️  Are you sure you want to proceed? This will move many files. (y/N): " confirm
if [[ $confirm != [yY] && $confirm != [yY][eE][sS] ]]; then
    echo "❌ Refactoring cancelled."
    exit 0
fi

echo ""
echo "🎯 PHASE 1: Creating new directory structure..."

# Create new directories
mkdir -p src/core
mkdir -p src/ota  # Already exists but ensure it's there

echo "✅ Directory structure created"

echo ""
echo "🎯 PHASE 2: Moving files to new locations..."

# Function to safely move files with verification
safe_move() {
    local src="$1"
    local dest="$2"
    local desc="$3"
    
    if [ -f "$src" ]; then
        echo "  ✅ Moving $desc: $src → $dest"
        mv "$src" "$dest"
    else
        echo "  ⚠️  File not found: $src (skipping $desc)"
    fi
}

# Function to safely move directory contents
move_directory_contents() {
    local src_dir="$1"
    local dest_dir="$2"
    local desc="$3"
    
    if [ -d "$src_dir" ] && [ "$(ls -A $src_dir 2>/dev/null)" ]; then
        echo "📁 Moving $desc from $src_dir/ to $dest_dir/..."
        for file in "$src_dir"/*; do
            if [ -f "$file" ]; then
                local filename=$(basename "$file")
                echo "  ✅ Moving: $filename"
                mv "$file" "$dest_dir/"
            fi
        done
    else
        echo "  ⚠️  Directory empty or not found: $src_dir (skipping $desc)"
    fi
}

# Move to src/core/
echo "📁 Moving core system files to src/core/..."
safe_move "src/main.cpp" "src/core/main.cpp" "main entry point"
safe_move "src/boot/BootManager.cpp" "src/core/BootManager.cpp" "boot manager implementation"
safe_move "src/boot/BootManager.h" "src/core/BootManager.h" "boot manager header"
safe_move "src/logging/CoreLoggingFilter.cpp" "src/core/CoreLoggingFilter.cpp" "core logging filter"
safe_move "src/events/UiEventHandlers.cpp" "src/core/UiEventHandlers.cpp" "UI event handlers"
safe_move "src/events/UiEventHandlers.h" "src/core/UiEventHandlers.h" "UI event handlers header"
safe_move "src/application/core/AppController.cpp" "src/core/AppController.cpp" "app controller"
safe_move "src/application/core/AppController.h" "src/core/AppController.h" "app controller header"
safe_move "src/application/core/TaskManager.cpp" "src/core/TaskManager.cpp" "task manager"
safe_move "src/application/core/TaskManager.h" "src/core/TaskManager.h" "task manager header"

# Consolidate OTA (handle potential duplicates intelligently)
echo "📁 Consolidating OTA system..."
ota_files_moved=0

# Check for boot OTA files
if [ -f "src/boot/OTAManager.cpp" ]; then
    echo "  📋 Found OTA files in boot/ directory"
    if [ -f "src/hardware/OTAManager.cpp" ]; then
        echo "  ⚠️  Duplicate OTA managers detected!"
        echo "     Creating consolidated version..."
        # Create a backup of boot version
        safe_move "src/boot/OTAManager.cpp" "src/ota/OTAManager_boot_backup.cpp" "boot OTA backup"
        safe_move "src/boot/OTAManager.h" "src/ota/OTAManager_boot_backup.h" "boot OTA header backup"
        echo "     Using hardware/ version as primary"
    else
        safe_move "src/boot/OTAManager.cpp" "src/ota/OTAManager.cpp" "boot OTA manager"
        safe_move "src/boot/OTAManager.h" "src/ota/OTAManager.h" "boot OTA header"
        ota_files_moved=1
    fi
fi

# Move hardware OTA files (primary version)
if [ -f "src/hardware/OTAManager.cpp" ]; then
    safe_move "src/hardware/OTAManager.cpp" "src/ota/OTAManager.cpp" "hardware OTA manager"
    safe_move "src/hardware/OTAManager.h" "src/ota/OTAManager.h" "hardware OTA header"
    ota_files_moved=1
fi

if [ $ota_files_moved -eq 0 ]; then
    echo "  ⚠️  No OTA manager files found to consolidate"
fi

# Move logo services
echo "📁 Moving logo services to src/logo/..."
safe_move "src/application/services/LogoSupplier.cpp" "src/logo/LogoSupplier.cpp" "logo supplier"
safe_move "src/application/services/LogoSupplier.h" "src/logo/LogoSupplier.h" "logo supplier header"
safe_move "src/application/services/MessageBusLogoSupplier.cpp" "src/logo/MessageBusLogoSupplier.cpp" "message bus logo supplier"
safe_move "src/application/services/MessageBusLogoSupplier.h" "src/logo/MessageBusLogoSupplier.h" "message bus logo supplier header"

echo "✅ File moves completed"

echo ""
echo "🎯 PHASE 3: Cleaning up empty directories..."

# Function to safely remove empty directories
safe_rmdir() {
    local dir="$1"
    local desc="$2"
    
    if [ -d "$dir" ]; then
        if [ "$(ls -A $dir 2>/dev/null)" ]; then
            echo "  📁 Directory not empty, keeping: $dir ($desc)"
            ls -la "$dir" | head -5
        else
            echo "  🗑️  Removing empty directory: $dir ($desc)"
            rmdir "$dir"
        fi
    else
        echo "  ✅ Directory already removed: $dir ($desc)"
    fi
}

# Remove empty directories in order (deepest first)
safe_rmdir "src/application/core" "application core directory"
safe_rmdir "src/application/services" "application services directory"
safe_rmdir "src/boot" "boot directory"
safe_rmdir "src/events" "events directory"
safe_rmdir "src/logging" "logging directory"

# Check if application directory can be removed (only if completely empty)
if [ -d "src/application" ]; then
    remaining=$(find src/application -type f | wc -l)
    if [ $remaining -eq 0 ]; then
        echo "  �️  Application directory is now empty, removing..."
        rmdir src/application 2>/dev/null || echo "  ⚠️  Could not remove src/application (may have subdirectories)"
    else
        echo "  📁 Application directory has $remaining files remaining, keeping it"
    fi
fi

echo "✅ Directory cleanup completed"

echo ""
echo "🎯 PHASE 4: Updating include paths..."

# Make update script executable and run it
chmod +x update_includes.sh
./update_includes.sh

echo "✅ Include paths updated"

echo ""
echo "🎯 PHASE 5: Verifying results..."

# Make verification script executable and run it
chmod +x verify_includes.sh
./verify_includes.sh

echo ""
echo "🎯 PHASE 6: Final directory structure..."
echo "New structure:"
find src/ -type d | sort | sed 's/^/  /'

echo ""
echo "📊 File count by directory:"
for dir in $(find src/ -type d | sort); do
    count=$(find "$dir" -maxdepth 1 -type f -name "*.cpp" -o -name "*.h" | wc -l)
    if [ $count -gt 0 ]; then
        echo "  $dir: $count files"
    fi
done

echo ""
echo "🎉 REFACTORING COMPLETE!"
echo ""
echo "📋 Next steps:"
echo "   1. Review any warnings or errors above"
echo "   2. Test compilation: 'platformio run'"
echo "   3. Fix any remaining include issues manually"
echo "   4. Commit changes: 'git add . && git commit -m \"Refactor: Consolidate project structure\"'"
echo ""
echo "✅ Project structure successfully reorganized!"