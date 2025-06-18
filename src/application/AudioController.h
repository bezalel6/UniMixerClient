#pragma once

#include "AudioStateManager.h"
#include "LVGLMessageHandler.h"
#include "../components/DeviceSelectorManager.h"
#include <memory>

namespace Application {
namespace Audio {

/**
 * Main audio system controller
 * Handles UI interactions, state management coordination, and external interfaces
 * Combines the responsibilities of the former AudioStatusManager and AudioUIController
 */
class AudioController {
   public:
    // Singleton access
    static AudioController& getInstance();

    // Lifecycle
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // === External Interface Methods (from AudioStatusManager) ===

    // Message handling
    void onAudioStatusReceived(const AudioStatus& status);

    // Publishing operations
    void publishStatusUpdate();
    void publishAudioStatusRequest(bool delayed = false);

    // State queries
    String getSelectedDevice();
    AudioLevel* getAudioLevel(const String& processName);
    std::vector<AudioLevel> getAllAudioLevels();
    AudioStatus getCurrentAudioStatus();

    // Tab management
    Events::UI::TabState getCurrentTab();
    void setCurrentTab(Events::UI::TabState tab);
    const char* getTabName(Events::UI::TabState tab);

    // UI state control
    bool isSuppressingArcEvents() const;
    bool isSuppressingDropdownEvents() const;

    // === UI Event Handlers ===
    void onVolumeSliderChanged(int volume);
    void onDeviceDropdownChanged(lv_obj_t* dropdown, const String& deviceName);
    void onTabChanged(Events::UI::TabState newTab);
    void onMuteButtonPressed();
    void onUnmuteButtonPressed();

    // === Convenience methods for external callers ===
    void setSelectedDeviceVolume(int volume) { onVolumeSliderChanged(volume); }
    void muteSelectedDevice() { onMuteButtonPressed(); }
    void unmuteSelectedDevice() { onUnmuteButtonPressed(); }
    void setDropdownSelection(lv_obj_t* dropdown, const String& deviceName) {
        onDeviceDropdownChanged(dropdown, deviceName);
    }
    String getDropdownSelection(lv_obj_t* dropdown);

    // UI update triggers
    void updateVolumeArcFromSelectedDevice();
    void updateVolumeArcLabel(int volume);  // Deprecated - handled automatically
    lv_obj_t* getCurrentVolumeSlider() const;
    void onAudioLevelsChangedUI();  // Called by TaskManager

   private:
    AudioController() = default;
    ~AudioController() = default;
    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;

    // Internal state
    bool initialized = false;
    std::unique_ptr<UI::Components::DeviceSelectorManager> deviceSelectorManager;

    // State change handler
    void onAudioStateChanged(const AudioStateChangeEvent& event);

    // UI update methods
    void updateVolumeDisplay();
    void updateDeviceSelectors();
    void updateDefaultDeviceLabel();
    void updateMuteButtons();
    void updateAllUI();

    // Device selector management
    void setupDeviceSelectorCallbacks();
    void updateDropdownOptions(const std::vector<AudioLevel>& devices);
    void updateDropdownSelections();

    // Utility methods
    String getCurrentTabName() const;
};

}  // namespace Audio
}  // namespace Application