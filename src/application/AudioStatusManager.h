#ifndef AUDIO_STATUS_MANAGER_H
#define AUDIO_STATUS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "../hardware/MqttManager.h"
#include "../display/DisplayManager.h"

// Macros to reduce repetition in device actions and MQTT publishing
#define AUDIO_DEVICE_ACTION_PROLOGUE(action_name)                        \
    if (!initialized) {                                                  \
        ESP_LOGW(TAG, "AudioStatusManager not initialized");             \
        return;                                                          \
    }                                                                    \
    if (selectedDevice.isEmpty()) {                                      \
        ESP_LOGW(TAG, "No device selected for " action_name " control"); \
        return;                                                          \
    }

#define AUDIO_MQTT_COMMAND_BASE(mqtt_action)                         \
    if (!Hardware::Mqtt::isConnected()) {                            \
        ESP_LOGW(TAG, "Cannot publish command: MQTT not connected"); \
        return;                                                      \
    }                                                                \
    JsonDocument doc;                                                \
    doc["messageType"] = "audio.mix.update";                         \
    doc["timestamp"] = String(Hardware::Device::getMillis());        \
    doc["messageId"] = String(Hardware::Device::getMillis());        \
    JsonArray updates = doc.createNestedArray("updates");            \
    JsonObject update = updates.createNestedObject();                \
    update["processName"] = deviceName;                              \
    update["action"] = mqtt_action;

#define AUDIO_PUBLISH_COMMAND_FINISH(action_name)                                                         \
    String jsonPayload;                                                                                   \
    serializeJson(doc, jsonPayload);                                                                      \
    bool published = Hardware::Mqtt::publish("homeassistant/unimix/audio/requests", jsonPayload.c_str()); \
    if (published) {                                                                                      \
        ESP_LOGI(TAG, "Published " action_name " command for %s", deviceName.c_str());                    \
    } else {                                                                                              \
        ESP_LOGE(TAG, "Failed to publish " action_name " command");                                       \
    }

#define AUDIO_PUBLISH_SIMPLE_COMMAND(method_name, mqtt_action, action_name) \
    static void method_name(const String& deviceName) {                     \
        AUDIO_MQTT_COMMAND_BASE(mqtt_action)                                \
        AUDIO_PUBLISH_COMMAND_FINISH(action_name)                           \
    }

#define AUDIO_DEVICE_ACTION_SIMPLE(method_name, action_name, mqtt_action)  \
    static void method_name(void) {                                        \
        AUDIO_DEVICE_ACTION_PROLOGUE(action_name)                          \
        publish##mqtt_action##Command(selectedDevice);                     \
        ESP_LOGI(TAG, action_name "d device: %s", selectedDevice.c_str()); \
    }

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

    // Selected device state management
    static void setSelectedDevice(const String& deviceName);
    static String getSelectedDevice(void);
    static void syncVolumeSliderWithSelectedDevice(void);
    static void updateVolumeSliderLabel(int volume);

    // Volume control
    static void setSelectedDeviceVolume(int volume);
    AUDIO_DEVICE_ACTION_SIMPLE(muteSelectedDevice, "mute", Mute)
    AUDIO_DEVICE_ACTION_SIMPLE(unmuteSelectedDevice, "unmute", Unmute)
    static void publishVolumeChangeCommand(const String& deviceName, int volume);
    AUDIO_PUBLISH_SIMPLE_COMMAND(publishMuteCommand, "Mute", "mute")
    AUDIO_PUBLISH_SIMPLE_COMMAND(publishUnmuteCommand, "Unmute", "unmute")
    static bool isSuppressingSliderEvents(void);

    // Status callback
    static void onAudioStatusReceived(const std::vector<AudioLevel>& levels);

    // MQTT publishing methods
    static void publishAudioStatusRequest(void);

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

    // Selected device state
    static String selectedDevice;
    static bool suppressSliderEvents;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_STATUS_MANAGER_H