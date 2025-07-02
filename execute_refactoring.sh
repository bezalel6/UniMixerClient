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

# Move to src/core/
echo "📁 Moving files to src/core/..."
mv src/main.cpp src/core/
mv src/boot/BootManager.cpp src/core/ 2>/dev/null || echo "⚠️  BootManager.cpp not found in boot/"
mv src/boot/BootManager.h src/core/ 2>/dev/null || echo "⚠️  BootManager.h not found in boot/"
mv src/logging/CoreLoggingFilter.cpp src/core/
mv src/events/UiEventHandlers.cpp src/core/
mv src/events/UiEventHandlers.h src/core/
mv src/application/core/AppController.cpp src/core/
mv src/application/core/AppController.h src/core/
mv src/application/core/TaskManager.cpp src/core/
mv src/application/core/TaskManager.h src/core/

# Consolidate OTA (handle potential duplicates)
echo "📁 Consolidating OTA files..."
if [ -f "src/boot/OTAManager.cpp" ] && [ -f "src/hardware/OTAManager.cpp" ]; then
    echo "⚠️  Found OTA files in both boot/ and hardware/ - need manual consolidation"
    echo "   Keeping hardware version, renaming boot version"
    mv src/boot/OTAManager.cpp src/ota/OTAManager_boot.cpp
    mv src/boot/OTAManager.h src/ota/OTAManager_boot.h
fi
mv src/hardware/OTAManager.cpp src/ota/ 2>/dev/null
mv src/hardware/OTAManager.h src/ota/ 2>/dev/null

# Move logo services
echo "📁 Moving logo services to src/logo/..."
mv src/application/services/LogoSupplier.cpp src/logo/
mv src/application/services/LogoSupplier.h src/logo/
mv src/application/services/MessageBusLogoSupplier.cpp src/logo/
mv src/application/services/MessageBusLogoSupplier.h src/logo/

echo "✅ File moves completed"

echo ""
echo "🎯 PHASE 3: Cleaning up empty directories..."

# Remove empty directories
rmdir src/boot 2>/dev/null || echo "📁 src/boot/ not empty or already removed"
rmdir src/events 2>/dev/null || echo "📁 src/events/ not empty or already removed"  
rmdir src/logging 2>/dev/null || echo "📁 src/logging/ not empty or already removed"
rmdir src/application/core 2>/dev/null || echo "📁 src/application/core/ not empty or already removed"
rmdir src/application/services 2>/dev/null || echo "📁 src/application/services/ not empty or already removed"

echo "✅ Empty directories cleaned up"

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