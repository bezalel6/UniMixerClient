#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "../hardware/MqttManager.h"
#include "../display/DisplayManager.h"

namespace Application {
namespace Audio {

// Structure to hold audio level data
struct AudioLevel {
    String processName;
    int volume;
    unsigned long lastUpdate;
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
    static void updateAudioDeviceDropdowns(void);
    static String buildAudioDeviceOptionsString(void);
    static String getSelectedAudioDevice(lv_obj_t* dropdown);

    // Status callback
    static void onAudioStatusReceived(const std::vector<AudioLevel>& levels);

   private:
    // MQTT handler management
    static void initializeAudioStatusHandler(void);
    static void audioStatusMessageHandler(const char* topic, const char* payload);
    static std::vector<AudioLevel> parseAudioStatusJson(const char* jsonPayload);

    // Internal state
    static std::vector<AudioLevel> audioLevels;
    static Hardware::Mqtt::Handler audioStatusHandler;
    static unsigned long lastUpdateTime;
    static bool initialized;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_STATUS_MANAGER_H