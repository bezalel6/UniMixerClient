#pragma once

#include "../../logo/LogoStorage.h"
#include "../ui/LVGLMessageHandler.h"
#include "AudioManager.h"
#include <lvgl.h>

namespace Application {
namespace Audio {

/**
 * UI interface layer for the audio system
 * Handles all LVGL interactions, visual updates, and UI event routing
 * Clean separation between business logic (AudioManager) and UI concerns
 */
class AudioUI {
public:
  // Singleton access
  static AudioUI &getInstance();

  // === LIFECYCLE ===
  bool init();
  void deinit();
  bool isInitialized() const { return initialized; }

  // === UI EVENT HANDLERS ===

  // Volume control events
  void onVolumeSliderChanged(int volume);
  void onVolumeSliderDragging(int volume); // Real-time updates during drag

  // Device selection events
  void onDeviceDropdownChanged(lv_obj_t *dropdown, const String &deviceName);

  // Tab change events
  void onTabChanged(Events::UI::TabState newTab);

  // Mute button events
  void onMuteButtonPressed();
  void onUnmuteButtonPressed();

  // === UI QUERIES ===
  String getDropdownSelection(lv_obj_t *dropdown) const;
  lv_obj_t *getCurrentVolumeSlider() const;

  // === UI UPDATE TRIGGERS ===
  void refreshAllUI(); // Full UI refresh
  void updateVolumeDisplay();
  void updateDeviceSelectors();
  void updateDefaultDeviceLabel();
  void updateMuteButtons();

private:
  AudioUI() = default;
  ~AudioUI() = default;
  AudioUI(const AudioUI &) = delete;
  AudioUI &operator=(const AudioUI &) = delete;

  // Internal state
  bool initialized = false;

  // State change handler from AudioManager
  void onAudioStateChanged(const AudioStateChangeEvent &event);

  // UI update methods
  void updateDropdownOptions(const std::vector<AudioLevel> &devices);
  void updateDropdownSelections();
  void updateVolumeSlider();
  void updateTabVisibility();

  // UI widget helpers
  lv_obj_t *getDropdownForTab(Events::UI::TabState tab) const;
  void setDropdownSelection(lv_obj_t *dropdown, const String &deviceName);
  int findDeviceIndexInDropdown(lv_obj_t *dropdown,
                                const String &deviceName) const;

  // Logo display for debugging
  void updateSingleTabLogo();

  // Utility methods
  String getCurrentTabName() const;
};

} // namespace Audio
} // namespace Application
