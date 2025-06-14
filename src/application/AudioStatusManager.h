#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

#include "../components/DeviceSelectorManager.h"
#include "../events/UiEventHandlers.h"
#include "../messaging/MessageBus.h"
#include "AudioTypes.h"
#include "WString.h"
#include <Arduino.h>
#include <lvgl.h>
#include <memory>
#include <vector>

namespace Application {
namespace Audio {

// Forward declarations
struct AudioLevel;
struct AudioStatus;

// Audio Status Manager class
class StatusManager {
public:
  // Initialization and cleanup
  static bool init(void);
  static void deinit(void);

  // Audio level management
  static void updateAudioLevel(const String &processName, int volume);
  static std::vector<AudioLevel> getAllAudioLevels(void);
  static AudioLevel *getAudioLevel(const String &processName);
  static AudioStatus getCurrentAudioStatus(void);

  // UI Updates
  static void onAudioLevelsChangedUI(void);
  static void updateVolumeArcFromSelectedDevice(void);
  static void updateVolumeArcLabel(int volume);

  // Device selection management
  static void setDropdownSelection(lv_obj_t *dropdown,
                                   const String &deviceName);
  static String getDropdownSelection(lv_obj_t *dropdown);
  static String getSelectedDevice(void); // For backward compatibility

  // Volume control
  static void setSelectedDeviceVolume(int volume);
  static void muteSelectedDevice(void);
  static void unmuteSelectedDevice(void);
  static void publishStatusUpdate(void);
  static bool isSuppressingArcEvents(void);
  static bool isSuppressingDropdownEvents(void);

  static lv_obj_t *getCurrentVolumeSlider(void);

  // Tab state management
  static Events::UI::TabState getCurrentTab(void);
  static void setCurrentTab(Events::UI::TabState tab);
  static const char *getTabName(Events::UI::TabState tab);

  // Status callback
  static void onAudioStatusReceived(const AudioStatus &status);

  // Command publishing methods
  static void publishAudioStatusRequest(bool delayed = false);

private:
  // Helper functions
  static int getProcessIdForDevice(const String &deviceName);
  static void initializeBalanceDropdownSelections(void);

  // Message handler management
  static void initializeMessageHandlers(void);
  static void audioStatusMessageHandler(const char *messageType,
                                        const char *payload);
  static AudioStatus parseAudioStatusJson(const char *jsonPayload);

  // Internal state
  static AudioStatus currentAudioStatus;
  static Messaging::Handler audioStatusHandler;
  static unsigned long lastUpdateTime;
  static bool initialized;

  // UI state
  static bool suppressArcEvents;
  static bool suppressDropdownEvents;
  static Events::UI::TabState currentTab;

  // Device selector management
  static std::unique_ptr<UI::Components::DeviceSelectorManager>
      deviceSelectorManager;
};

} // namespace Audio
} // namespace Application

#endif // AUDIO_STATUS_MANAGER_H