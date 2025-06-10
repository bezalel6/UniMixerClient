#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <lvgl.h>
#include "../messaging/MessageBus.h"
#include "../../include/MessageProtocol.h"

namespace Application {
namespace Audio {

// Structure to hold audio level data
struct AudioLevel {
    String processName;
    int volume;
    unsigned long lastUpdate;
    bool stale = false;
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

    // Statistics
    static int getActiveProcessCount(void);
    static int getTotalVolume(void);
    static AudioLevel getHighestVolumeProcess(void);

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
    static void publishVolumeChangeCommand(const String& deviceName, int volume);
    static void publishMuteCommand(const String& deviceName);
    static void publishUnmuteCommand(const String& deviceName);
    static bool isSuppressingArcEvents(void);

    // Status callback
    static void onAudioStatusReceived(const std::vector<AudioLevel>& levels);

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
    static std::vector<AudioLevel> parseAudioStatusJson(const char* jsonPayload);

    // Internal state
    static std::vector<AudioLevel> audioLevels;
    static Messaging::Handler audioStatusHandler;
    static Messaging::Handler commandResultHandler;
    static unsigned long lastUpdateTime;
    static bool initialized;

    // Selected device state
    static String selectedDevice;
    static bool suppressArcEvents;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_STATUS_MANAGER_H