#!/bin/bash

# Comprehensive Refactoring Test Script
# Commits current state, creates test environment, and executes refactoring

set -e  # Exit on any error

echo "🧪 Starting Comprehensive Refactoring Test..."
echo "📋 This will:"
echo "   1. Commit current refactoring scripts"
echo "   2. Create a test branch"  
echo "   3. Clone to a temporary folder"
echo "   4. Execute the refactoring in isolation"
echo "   5. Verify the results"
echo ""

# Get current branch name
current_branch=$(git rev-parse --abbrev-ref HEAD)
echo "📍 Current branch: $current_branch"

# Safety check
read -p "⚠️  Proceed with refactoring test? This will create commits and test branches. (y/N): " confirm
if [[ $confirm != [yY] && $confirm != [yY][eE][sS] ]]; then
    echo "❌ Test cancelled."
    exit 0
fi

echo ""
echo "🎯 PHASE 1: Committing refactoring scripts..."

# Add all refactoring scripts to git
git add execute_refactoring.sh
git add update_includes.sh  
git add verify_includes.sh
git add REFACTORING_AUTOMATION.md
git add test_refactoring.sh

# Commit the scripts
git commit -m "feat: Add comprehensive automated refactoring system

- execute_refactoring.sh: Master orchestrator for directory reorganization
- update_includes.sh: Systematic include path updates  
- verify_includes.sh: Validation and broken include detection
- REFACTORING_AUTOMATION.md: Complete documentation
- test_refactoring.sh: Automated testing in isolated environment

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

# Clone current repository to temp directory
repo_url=$(git remote get-url origin 2>/dev/null || echo "$(pwd)")
if [[ "$repo_url" == "$(pwd)" ]]; then
    # Local repository - clone using file path
    echo "📋 Cloning local repository..."
    git clone "$(pwd)" "$temp_dir/project"
else
    # Remote repository  
    echo "📋 Cloning from remote: $repo_url"
    git clone "$repo_url" "$temp_dir/project"
    cd "$temp_dir/project"
    git checkout "$test_branch"
fi

cd "$temp_dir/project"
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
echo "🚀 Executing refactoring (auto-confirming)..."

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
./verify_includes.sh

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
echo "📋 Testing compilation (syntax check)..."
# Try a basic syntax check on a few key files
syntax_errors=0

check_syntax() {
    local file="$1"
    if [ -f "$file" ]; then
        # Basic C++ syntax check using gcc
        if gcc -fsyntax-only -x c++ "$file" -I src/ -I include/ 2>/dev/null; then
            echo "  ✅ Syntax OK: $file"
        else
            echo "  ❌ Syntax error: $file"
            syntax_errors=$((syntax_errors + 1))
        fi
    fi
}

check_syntax "src/core/AppController.cpp"
check_syntax "src/core/TaskManager.cpp"
check_syntax "src/application/audio/AudioManager.cpp"

echo ""
echo "🎯 PHASE 7: Test Results Summary..."

echo ""
echo "📊 TEST RESULTS:"
echo "================"

if [ "$all_critical_exist" = true ]; then
    echo "✅ All critical files found"
else
    echo "❌ Some critical files missing"
fi

if [ $syntax_errors -eq 0 ]; then
    echo "✅ No syntax errors detected"
else
    echo "❌ $syntax_errors syntax errors found"
fi

echo ""
echo "📁 Test environment location: $temp_dir/project"
echo "🔄 Original branch: $current_branch"
echo "🧪 Test branch: $test_branch"

echo ""
if [ "$all_critical_exist" = true ] && [ $syntax_errors -eq 0 ]; then
    echo "🎉 REFACTORING TEST SUCCESSFUL!"
    echo ""
    echo "📋 Next steps:"
    echo "   1. Review the test results above"
    echo "   2. Check the test environment: cd $temp_dir/project"
    echo "   3. If satisfied, apply to main codebase:"
    echo "      cd $(pwd)"
    echo "      git checkout $current_branch"
    echo "      git merge $test_branch"
    echo "      ./execute_refactoring.sh"
    echo "   4. Clean up test environment: rm -rf $temp_dir"
else
    echo "⚠️  REFACTORING TEST HAD ISSUES!"
    echo ""
    echo "📋 Investigate and fix:"
    echo "   1. Check test environment: cd $temp_dir/project"
    echo "   2. Review errors above"
    echo "   3. Update scripts as needed"
    echo "   4. Re-run test: ./test_refactoring.sh"
fi

echo ""
echo "🔍 To inspect the test environment:"
echo "   cd $temp_dir/project"
echo "   find src/ -type f | sort"
echo ""