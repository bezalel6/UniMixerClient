#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <lvgl.h>
#include "../messaging/MessageBus.h"
#include "../../include/MessageProtocol.h"
#include "../events/UiEventHandlers.h"

namespace Application {
namespace Audio {

// Structure to hold audio level data
struct AudioLevel {
    String processName;
    int volume;
    bool isMuted = false;
    unsigned long lastUpdate;
    bool stale = false;
};

struct AudioDevice {
    String friendlyName;
    float volume;
    bool isMuted;
    String state;
};
// Structure to hold complete audio status
struct AudioStatus {
    std::vector<AudioLevel> audioLevels;
    AudioDevice defaultDevice;
    unsigned long timestamp;
    bool hasDefaultDevice = false;
};

// Audio Status Manager class
class StatusManager {
   public:
    // Initialization and cleanup
    static bool init(void);
    static void deinit(void);

    // Audio level management
    static void updateAudioLevel(const String& processName, int volume);
    static std::vector<AudioLevel> getAllAudioLevels(void);
    static AudioLevel* getAudioLevel(const String& processName);
    static AudioStatus getCurrentAudioStatus(void);

    // UI Updates
    static void onAudioLevelsChangedUI(void);
    static void updateDropdownSelection(void);
    static String buildAudioDeviceOptionsString(void);
    static String getSelectedAudioDevice(lv_obj_t* dropdown);

    // Selected device state management
    static void setSelectedDevice(const String& deviceName);
    static String getSelectedDevice(void);
    static void updateVolumeArcFromSelectedDevice(void);
    static void updateVolumeArcLabel(int volume);

    // Volume control
    static void setSelectedDeviceVolume(int volume);
    static void muteSelectedDevice(void);
    static void unmuteSelectedDevice(void);
    static void publishStatusUpdate(void);
    static bool isSuppressingArcEvents(void);

    // Tab state management
    static Events::UI::TabState getCurrentTab(void);
    static void setCurrentTab(Events::UI::TabState tab);
    static const char* getTabName(Events::UI::TabState tab);

    // Status callback
    static void onAudioStatusReceived(const AudioStatus& status);

    // Command publishing methods
    static void publishAudioStatusRequest(bool delayed = false);

   private:
    // Helper functions
    static int getProcessIdForDevice(const String& deviceName);
    static void updateAllDropdownOptions(void);
    static void updateSingleDropdownSelection(lv_obj_t* dropdown);

    // Message handler management
    static void initializeMessageHandlers(void);
    static void audioStatusMessageHandler(const char* messageType, const char* payload);
    static void commandResultMessageHandler(const char* messageType, const char* payload);
    static AudioStatus parseAudioStatusJson(const char* jsonPayload);

    // Internal state
    static AudioStatus currentAudioStatus;
    static Messaging::Handler audioStatusHandler;
    static Messaging::Handler commandResultHandler;
    static unsigned long lastUpdateTime;
    static bool initialized;

    // Selected device state
    static String selectedDevice;
    static bool suppressArcEvents;

    // Tab state
    static Events::UI::TabState currentTab;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_STATUS_MANAGER_H