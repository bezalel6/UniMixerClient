#pragma once

#include "AudioState.h"
#include "AudioTypes.h"
#include <functional>
#include <vector>

namespace Application {
namespace Audio {

/**
 * Core audio state manager
 * Handles business logic and state transitions without UI concerns
 */
class AudioStateManager {
   public:
    // Event callback types
    using StateChangeCallback = std::function<void(const AudioStateChangeEvent&)>;

    // Singleton access
    static AudioStateManager& getInstance();

    // Core lifecycle
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // State access
    const AudioState& getState() const { return state; }

    // Event subscription
    void subscribeToStateChanges(StateChangeCallback callback);

    // State modification operations
    void updateAudioStatus(const AudioStatus& newStatus);
    void updateDeviceVolume(const String& processName, int volume);
    void selectDevice(const String& deviceName);
    void selectBalanceDevices(const String& device1, const String& device2);
    void setTab(Events::UI::TabState tab);
    void setVolumeForCurrentDevice(int volume);
    void muteCurrentDevice();
    void unmuteCurrentDevice();

    // UI state control
    void setSuppressArcEvents(bool suppress) { state.suppressArcEvents = suppress; }
    void setSuppressDropdownEvents(bool suppress) { state.suppressDropdownEvents = suppress; }
    bool isSuppressingArcEvents() const { return state.suppressArcEvents; }
    bool isSuppressingDropdownEvents() const { return state.suppressDropdownEvents; }

    // Data queries
    std::vector<AudioLevel> getAllDevices() const;
    AudioLevel* getDevice(const String& processName);
    String getCurrentDevice() const;
    int getCurrentVolume() const;
    bool isCurrentDeviceMuted() const;

    // Publishing
    void publishStatusUpdate();
    void publishStatusRequest(bool delayed = false);

   private:
    AudioStateManager() = default;
    ~AudioStateManager() = default;
    AudioStateManager(const AudioStateManager&) = delete;
    AudioStateManager& operator=(const AudioStateManager&) = delete;

    // Internal state
    AudioState state;
    bool initialized = false;
    std::vector<StateChangeCallback> callbacks;

    // Internal methods
    void notifyStateChange(const AudioStateChangeEvent& event);
    void autoSelectDeviceIfNeeded();
    void markDevicesAsStale();
    void updateDeviceFromStatus(const AudioLevel& levelData);
    String findBestDeviceToSelect() const;
};

}  // namespace Audio
}  // namespace Application