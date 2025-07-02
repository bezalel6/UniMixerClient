#!/bin/bash

# Include Path Verification Script
# Checks for any remaining old include paths after refactoring

echo "🔍 Verifying include path updates..."

# Function to check for old include patterns
check_old_includes() {
    local pattern="$1"
    local description="$2"
    local count=$(grep -r "#include.*$pattern" src/ --include="*.cpp" --include="*.h" | wc -l)
    
    if [ $count -gt 0 ]; then
        echo "❌ Found $count old includes: $description"
        grep -r "#include.*$pattern" src/ --include="*.cpp" --include="*.h"
        echo ""
    else
        echo "✅ No old includes found: $description"
    fi
}

echo "Checking for old include patterns that should have been updated..."

# Check for old patterns that should no longer exist
check_old_includes "application/core/" "Application core files"
check_old_includes "events/" "Events files" 
check_old_includes "logging/" "Logging files"
check_old_includes "boot/" "Boot files"
check_old_includes "hardware/OTAManager" "OTA Manager files"
check_old_includes "application/services/" "Logo service files"

echo ""
echo "Checking for broken includes (files that don't exist)..."

# Find all include statements and check if the referenced files exist
while IFS= read -r line; do
    file=$(echo "$line" | cut -d: -f1)
    include_line=$(echo "$line" | cut -d: -f2-)
    include_path=$(echo "$include_line" | sed -n 's/.*#include *[<"]\([^>"]*\)[>"].*/\1/p')
    
    if [ ! -z "$include_path" ] && [[ "$include_path" != *".h"* ]]; then
        continue  # Skip non-header includes
    fi
    
    # Calculate full path relative to the file's directory
    file_dir=$(dirname "$file")
    if [[ "$include_path" == ../* ]]; then
        # Relative path
        full_path="$file_dir/$include_path"
    elif [[ "$include_path" == /* ]]; then
        # Absolute path (shouldn't happen in our case)
        full_path="$include_path"
    else
        # Path relative to src/ 
        full_path="src/$include_path"
    fi
    
    # Normalize the path
    full_path=$(realpath -m "$full_path" 2>/dev/null)
    
    if [ ! -f "$full_path" ] && [[ "$include_path" != *"Arduino"* ]] && [[ "$include_path" != *"esp_"* ]] && [[ "$include_path" != *"freertos"* ]] && [[ "$include_path" != *"lvgl"* ]]; then
        echo "❌ Broken include: $file includes '$include_path' (resolved to: $full_path)"
    fi
    
done < <(grep -r "#include" src/ --include="*.cpp" --include="*.h" | head -20)  # Limit check for performance

echo ""
echo "🎯 Include verification complete!"
echo "📋 If any issues found above, fix them before compilation."