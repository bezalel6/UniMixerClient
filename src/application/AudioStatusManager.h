#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

// Refactored AudioStatusManager - now includes the new simplified facade
#include "AudioStateManager.h"
#include "AudioUIController.h"
#include "AudioTypes.h"
#include "../events/UiEventHandlers.h"
#include <lvgl.h>

namespace Application {
namespace Audio {

/**
 * Simplified AudioStatusManager facade
 * Provides backward compatibility while using the new state-driven architecture
 */
class StatusManager {
   public:
    // Lifecycle
    static bool init();
    static void deinit();

    // Message handling (for compatibility with existing message routing)
    static void onAudioStatusReceived(const AudioStatus& status);

    // UI event handlers (called from existing LVGL event handlers)
    static void setSelectedDeviceVolume(int volume);
    static void muteSelectedDevice();
    static void unmuteSelectedDevice();
    static void setDropdownSelection(lv_obj_t* dropdown, const String& deviceName);
    static String getDropdownSelection(lv_obj_t* dropdown);

    // State queries (for backward compatibility)
    static String getSelectedDevice();
    static AudioLevel* getAudioLevel(const String& processName);
    static std::vector<AudioLevel> getAllAudioLevels();
    static AudioStatus getCurrentAudioStatus();

    // Tab management
    static Events::UI::TabState getCurrentTab();
    static void setCurrentTab(Events::UI::TabState tab);
    static const char* getTabName(Events::UI::TabState tab);

    // UI state control
    static bool isSuppressingArcEvents();
    static bool isSuppressingDropdownEvents();

    // Publishing
    static void publishStatusUpdate();
    static void publishAudioStatusRequest(bool delayed = false);

    // Internal helpers (used by event handlers)
    static void updateVolumeArcFromSelectedDevice();
    static void updateVolumeArcLabel(int volume);
    static lv_obj_t* getCurrentVolumeSlider();

    // UI update trigger (used by TaskManager)
    static void onAudioLevelsChangedUI();

   private:
    StatusManager() = delete;  // Static class

    static bool initialized;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_STATUS_MANAGER_H