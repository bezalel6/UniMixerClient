#!/bin/bash

# Automatic Refactoring Test Script - No User Interaction Required
# Commits current state, creates test environment, and executes refactoring

set -e  # Exit on any error

echo "🧪 Starting Automated Refactoring Test..."
echo "📋 This will automatically:"
echo "   1. Commit current refactoring scripts"
echo "   2. Create a test branch"  
echo "   3. Clone to a temporary folder"
echo "   4. Execute the refactoring in isolation"
echo "   5. Verify the results"
echo ""

# Get current branch name
current_branch=$(git rev-parse --abbrev-ref HEAD)
echo "📍 Current branch: $current_branch"

echo ""
echo "🎯 PHASE 1: Committing refactoring scripts..."

# Add all refactoring scripts to git
git add execute_refactoring.sh
git add update_includes.sh  
git add verify_includes.sh
git add REFACTORING_AUTOMATION.md
git add test_refactoring.sh
git add test_refactoring_auto.sh

# Commit the scripts
git commit -m "feat: Add comprehensive automated refactoring system

- execute_refactoring.sh: Master orchestrator for directory reorganization
- update_includes.sh: Systematic include path updates  
- verify_includes.sh: Validation and broken include detection
- REFACTORING_AUTOMATION.md: Complete documentation
- test_refactoring.sh: Interactive testing in isolated environment
- test_refactoring_auto.sh: Automated testing without user input

Scripts handle:
- File movement with verification
- OTA consolidation (handling duplicates)  
- Include path mapping for all relative path variations
- Safety checks and rollback options
- Comprehensive verification"

echo "✅ Refactoring scripts committed"

echo ""
echo "🎯 PHASE 2: Creating test branch..."

# Create test branch
test_branch="refactoring-test-$(date +%Y%m%d-%H%M%S)"
git checkout -b "$test_branch"
echo "✅ Created test branch: $test_branch"

echo ""
echo "🎯 PHASE 3: Setting up isolated test environment..."

# Create temporary directory
temp_dir="/tmp/project-refactoring-test-$(date +%Y%m%d-%H%M%S)"
echo "📁 Creating test directory: $temp_dir"
mkdir -p "$temp_dir"

# Clone current repository to temp directory (local clone)
echo "📋 Cloning current repository state..."
git clone "$(pwd)" "$temp_dir/project"
cd "$temp_dir/project"
git checkout "$test_branch"

echo "✅ Test environment ready at: $temp_dir/project"

echo ""
echo "🎯 PHASE 4: Executing refactoring in test environment..."

# Make scripts executable
chmod +x execute_refactoring.sh
chmod +x update_includes.sh  
chmod +x verify_includes.sh

# Show initial structure
echo "📊 BEFORE - Directory structure:"
find src/ -type d | sort | sed 's/^/  /'

echo ""
echo "📊 BEFORE - File count:"
find src/ -name "*.cpp" -o -name "*.h" | wc -l | sed 's/^/  Total source files: /'

echo ""
echo "🚀 Executing refactoring (automatically confirming)..."

# Execute refactoring with automatic confirmation
echo "y" | ./execute_refactoring.sh

echo ""
echo "🎯 PHASE 5: Analyzing results..."

# Show final structure
echo "📊 AFTER - Directory structure:"
find src/ -type d | sort | sed 's/^/  /'

echo ""
echo "📊 AFTER - File count by directory:"
for dir in $(find src/ -type d | sort); do
    count=$(find "$dir" -maxdepth 1 -type f -name "*.cpp" -o -name "*.h" | wc -l)
    if [ $count -gt 0 ]; then
        echo "  $dir: $count files"
    fi
done

echo ""
echo "🎯 PHASE 6: Verification checks..."

# Run additional verification
echo "📋 Checking for broken includes..."
if ./verify_includes.sh | grep -q "❌"; then
    echo "  ⚠️  Some include issues found - check details above"
    verification_passed=false
else
    echo "  ✅ All includes verified successfully"
    verification_passed=true
fi

echo ""
echo "📋 Verifying critical files exist..."

critical_files=(
    "src/core/main.cpp"
    "src/core/AppController.h"
    "src/core/TaskManager.h"
    "src/ota/OTAManager.h"
    "src/logo/LogoSupplier.h"
    "src/application/audio/AudioManager.h"
)

all_critical_exist=true
for file in "${critical_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✅ Found: $file"
    else
        echo "  ❌ Missing: $file"
        all_critical_exist=false
    fi
done

echo ""
echo "📋 Testing file syntax (basic checks)..."
# Check for common syntax issues
syntax_issues=0

# Check for common include problems
broken_includes=$(grep -r "#include.*\.\." src/ 2>/dev/null | wc -l || echo "0")
missing_headers=$(find src/ -name "*.cpp" -exec grep -l "#include.*/" {} \; | wc -l || echo "0")

if [ "$broken_includes" -gt 0 ]; then
    echo "  ⚠️  Found $broken_includes potentially problematic relative includes"
    syntax_issues=$((syntax_issues + broken_includes))
fi

echo "  📊 Basic syntax check: $syntax_issues potential issues found"

echo ""
echo "🎯 PHASE 7: Test Results Summary..."

echo ""
echo "📊 FINAL TEST RESULTS:"
echo "======================"

# Calculate overall success
overall_success=true

if [ "$all_critical_exist" = true ]; then
    echo "✅ All critical files found"
else
    echo "❌ Some critical files missing"
    overall_success=false
fi

if [ "$verification_passed" = true ]; then
    echo "✅ Include verification passed"
else
    echo "❌ Include verification failed"
    overall_success=false
fi

if [ $syntax_issues -eq 0 ]; then
    echo "✅ No obvious syntax issues detected"
else
    echo "⚠️  $syntax_issues potential syntax issues found"
fi

echo ""
echo "📁 Test environment: $temp_dir/project"
echo "🔄 Original branch: $current_branch"
echo "🧪 Test branch: $test_branch"

echo ""
if [ "$overall_success" = true ]; then
    echo "🎉 REFACTORING TEST SUCCESSFUL!"
    echo ""
    echo "📋 Ready to apply to main codebase:"
    echo "   cd $(pwd | sed "s|$temp_dir/project|/workspace|")"
    echo "   git checkout $current_branch"
    echo "   git merge $test_branch"
    echo "   ./execute_refactoring.sh"
    echo ""
    echo "🧹 Clean up when done:"
    echo "   rm -rf $temp_dir"
else
    echo "⚠️  REFACTORING TEST HAD ISSUES!"
    echo ""
    echo "📋 Check the test environment:"
    echo "   cd $temp_dir/project"
    echo "   find src/ -type f | sort"
fi

echo ""
echo "🔍 Test completed at: $(date)"
echo "📊 Test environment preserved for inspection: $temp_dir/project"