# UniMixer Client Cleanup Plan

## Executive Summary
This cleanup plan identifies dead code, unused imports, and deprecated references found in the codebase. Items are categorized by risk level and include specific action items.

## Findings

### 1. Safe Removals (Low Risk)

#### Already Commented Code
- **src/core/BSODHandler.cpp:6**: Already commented wrapper include
  ```cpp
  // #include "ui/wrapper/LVGLWrapper.h" // REMOVED - using direct LVGL
  ```
  **Action**: Delete this line entirely

#### Deprecated Method Comments
- **src/logo/SimpleLogoManager.cpp:633**: Comment about removed method
  ```cpp
  // REMOVED - sanitizeProcessName is completely deprecated
  ```
  **Action**: Delete these comment lines

- **src/logo/SimpleLogoManager.h:79**: Comment about removed method
  ```cpp
  // sanitizeProcessName REMOVED - use extractProcessCore()
  ```
  **Action**: Delete this comment line

### 2. Medium Risk Removals

#### Large Commented Code Blocks in UiEventHandlers.cpp
- **src/core/UiEventHandlers.cpp:110-145**: Large block of commented AudioManager calls
  - Lines reference removed AudioManager and AudioUI functionality
  - Appears to be old UI event handling code
  **Action**: Review and remove if confirmed obsolete

#### Commented Code in TaskManager.cpp
- **src/core/TaskManager.cpp:303-304**: Commented AudioUI refresh calls
  ```cpp
  // if (Application::Audio::AudioUI::getInstance().isInitialized()) {
  //     Application::Audio::AudioUI::getInstance().refreshAllUI();
  ```
  **Action**: Remove if AudioUI is deprecated

### 3. Potential Issues (Requires Investigation)

#### SD Card Status Enum
- **SD_STATUS_CARD_REMOVED** is still actively used in SDManager.cpp (lines 116, 121, 208)
- This is NOT dead code - it's part of the active SD card management system
- **Action**: Keep this code - it's necessary for SD card state management

### 4. Already Completed Cleanups

#### LVGL Wrapper
- The deprecated wrapper directory (`src/ui/wrapper/`) has already been removed
- No active includes of wrapper files found
- **Status**: ✅ Already cleaned up

#### Network-Related Code
- No WiFi or MQTT includes found in the codebase
- **Status**: ✅ Already cleaned up

## Recommended Actions

### Phase 1: Safe Cleanup (Immediate)
1. Remove commented include in BSODHandler.cpp
2. Remove deprecated method comments in SimpleLogoManager files
3. Remove empty comment lines and cleanup formatting

### Phase 2: Medium Risk Cleanup (After Review)
1. Review and remove commented AudioManager code in UiEventHandlers.cpp
2. Review and remove commented AudioUI code in TaskManager.cpp
3. Verify these components are truly deprecated before removal

### Phase 3: Code Quality Improvements
1. Update CLAUDE.md to remove references to deprecated wrapper
2. Consider adding a deprecation notice system for future removals
3. Add automated checks to prevent reintroduction of deprecated code

## Risk Assessment

### Low Risk Items
- Removing already-commented includes: No functional impact
- Removing comment-only lines: No functional impact
- Total lines affected: ~5 lines

### Medium Risk Items
- Removing larger commented code blocks: Requires verification that functionality has been replaced
- Total lines affected: ~40 lines

### Items NOT to Remove
- SD_STATUS_CARD_REMOVED: Active code, not dead
- Any code in auto-generated UI files
- Normal code comments that provide documentation

## Estimated Impact
- Code reduction: ~50 lines
- Improved clarity: Removal of confusing deprecated references
- No functional changes expected
- Build/upload process remains unchanged

## Next Steps
1. Execute Phase 1 immediately
2. Get approval for Phase 2 removals
3. Test after each phase to ensure no regressions