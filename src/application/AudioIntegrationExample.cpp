/*
 * AudioIntegrationExample.cpp
 *
 * Example showing how to use the new simplified audio architecture
 * This file demonstrates the usage patterns and will be deleted after migration
 */

#include "AudioManager.h"
#include "AudioUI.h"
#include "AudioData.h"

namespace Application {
namespace Audio {

void AudioIntegrationExample() {
    // =========================================================================
    // NEW ARCHITECTURE USAGE EXAMPLE
    // =========================================================================

    // 1. SIMPLE INITIALIZATION
    AudioManager& manager = AudioManager::getInstance();
    AudioUI& ui = AudioUI::getInstance();

    if (!manager.init()) {
        ESP_LOGE("Example", "Failed to initialize AudioManager");
        return;
    }

    if (!ui.init()) {
        ESP_LOGE("Example", "Failed to initialize AudioUI");
        return;
    }

    // 2. EXTERNAL DATA INPUT (replaces old AudioController::onAudioStatusReceived)
    AudioStatus status;
    // ... populate status from external source
    manager.onAudioStatusReceived(status);

    // 3. USER ACTIONS (simple, clear interface)
    manager.selectDevice("MyAudioApp");
    manager.setVolumeForCurrentDevice(75);
    manager.muteCurrentDevice();

    // 4. TAB MANAGEMENT
    manager.setCurrentTab(Events::UI::TabState::BALANCE);
    manager.selectBalanceDevices("App1", "App2");

    // 5. UI EVENT HANDLING (clean separation)
    ui.onVolumeSliderChanged(50);
    ui.onDeviceDropdownChanged(nullptr, "NewDevice");  // UI events routed to UI layer

    // 6. STATE QUERIES (single source of truth)
    const auto& state = manager.getState();
    ESP_LOGI("Example", "Current tab: %s", manager.getTabName(state.currentTab));
    ESP_LOGI("Example", "Current device: %s", state.getCurrentSelectedDevice().c_str());
    ESP_LOGI("Example", "Current volume: %d", state.getCurrentSelectedVolume());

    // 7. DEVICE MANAGEMENT
    auto devices = manager.getAllDevices();
    ESP_LOGI("Example", "Found %d audio devices", devices.size());

    // 8. EXTERNAL COMMUNICATION
    manager.publishStatusRequest();
    manager.publishStatusUpdate();
}

// =========================================================================
// COMPARISON: OLD vs NEW USAGE
// =========================================================================

void ComparisonExample() {
    ESP_LOGI("Example", "=== OLD ARCHITECTURE (Complex) ===");
    /*
    // OLD WAY - Multiple classes to coordinate:
    AudioController& controller = AudioController::getInstance();
    AudioStateManager& stateManager = AudioStateManager::getInstance();
    auto deviceSelector = std::make_unique<DeviceSelectorManager>();

    // Confusing responsibilities:
    controller.onVolumeSliderChanged(50);         // UI event in controller?
    stateManager.selectDevice("MyDevice");        // Business logic in separate manager?
    controller.publishStatusUpdate();            // Communication back in controller?

    // Unclear data access:
    AudioStatus status1 = controller.getCurrentAudioStatus();        // From controller
    const AudioState& state = stateManager.getState();             // From state manager
    AudioLevel* device = stateManager.getDevice("MyDevice");       // From state manager again
    */

    ESP_LOGI("Example", "=== NEW ARCHITECTURE (Simple) ===");

    // NEW WAY - Clear separation of concerns:
    AudioManager& manager = AudioManager::getInstance();  // Business logic
    AudioUI& ui = AudioUI::getInstance();                 // UI interactions

    // Intuitive usage:
    ui.onVolumeSliderChanged(50);      // UI events go to UI layer
    manager.selectDevice("MyDevice");  // Business logic goes to manager
    manager.publishStatusUpdate();     // External communication from manager

    // Single source of truth:
    const auto& state = manager.getState();              // All state from manager
    AudioLevel* device = manager.getDevice("MyDevice");  // All data from manager
}

void MigrationBenefitsExample() {
    ESP_LOGI("Example", "=== BENEFITS DEMONSTRATION ===");

    // 1. FEWER CONCEPTS TO UNDERSTAND
    //    OLD: AudioController + AudioStateManager + AudioState + AudioTypes + DeviceSelectorManager
    //    NEW: AudioManager + AudioUI + AudioData

    // 2. CLEAR DEPENDENCY HIERARCHY
    //    AudioUI -> AudioManager -> AudioData
    //    (UI depends on Manager, Manager depends on Data)

    // 3. SINGLE ENTRY POINTS
    AudioManager& manager = AudioManager::getInstance();  // All business logic
    AudioUI& ui = AudioUI::getInstance();                 // All UI interactions

    // 4. TYPE SAFETY AND CONSISTENCY
    //    All audio devices are AudioLevel (compatible with messaging)
    //    All audio status uses AudioStatus (compatible with existing code)

    // 5. EASY TESTING
    //    AudioManager can be tested without UI dependencies
    //    AudioData is pure data structures (easy to mock)
    //    Clear interfaces between layers

    ESP_LOGI("Example", "Architecture successfully simplified!");
}

}  // namespace Audio
}  // namespace Application