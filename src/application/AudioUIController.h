#pragma once

#include "AudioStateManager.h"
#include "LVGLMessageHandler.h"
#include "../components/DeviceSelectorManager.h"
#include <memory>

namespace Application {
namespace Audio {

/**
 * Handles all UI-related logic for audio controls
 * Subscribes to state changes and updates UI accordingly
 */
class AudioUIController {
   public:
    // Singleton access
    static AudioUIController& getInstance();

    // Lifecycle
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // UI event handlers (called from LVGL event handlers)
    void onVolumeSliderChanged(int volume);
    void onDeviceDropdownChanged(lv_obj_t* dropdown, const String& deviceName);
    void onTabChanged(Events::UI::TabState newTab);
    void onMuteButtonPressed();
    void onUnmuteButtonPressed();

    // State queries for UI
    bool shouldSuppressArcEvents() const;
    bool shouldSuppressDropdownEvents() const;

   private:
    AudioUIController() = default;
    ~AudioUIController() = default;
    AudioUIController(const AudioUIController&) = delete;
    AudioUIController& operator=(const AudioUIController&) = delete;

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
    lv_obj_t* getCurrentVolumeSlider() const;
    String getCurrentTabName() const;
};

}  // namespace Audio
}  // namespace Application