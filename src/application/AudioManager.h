#pragma once

#include "AudioData.h"
#include <functional>
#include <vector>

namespace Application {
namespace Audio {

/**
 * Main audio system manager
 * Consolidates all audio business logic, state management, and external interfaces
 * Single entry point for all audio operations
 */
class AudioManager {
   public:
    // Event callback type
    using StateChangeCallback = std::function<void(const AudioStateChangeEvent&)>;

    // Singleton access
    static AudioManager& getInstance();

    // === LIFECYCLE ===
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // === STATE ACCESS ===
    const AudioAppState& getState() const { return state; }

    // Quick accessors
    Events::UI::TabState getCurrentTab() const { return state.currentTab; }
    String getCurrentDevice() const { return state.getCurrentSelectedDevice(); }
    int getCurrentVolume() const { return state.getCurrentSelectedVolume(); }
    bool isCurrentDeviceMuted() const { return state.isCurrentDeviceMuted(); }
    bool hasDevices() const { return state.hasDevices(); }

    // Device queries
    std::vector<AudioLevel> getAllDevices() const { return state.currentStatus.audioLevels; }
    AudioLevel* getDevice(const String& processName);
    const AudioLevel* getDevice(const String& processName) const;

    // === EXTERNAL DATA INPUT ===
    void onAudioStatusReceived(const AudioStatus& status);

    // === USER ACTIONS ===

    // Device selection
    void selectDevice(const String& deviceName);
    void selectBalanceDevices(const String& device1, const String& device2);

    // Volume control
    void setVolumeForCurrentDevice(int volume);
    void setDeviceVolume(const String& deviceName, int volume);

    // Mute control
    void muteCurrentDevice();
    void unmuteCurrentDevice();
    void muteDevice(const String& deviceName);
    void unmuteDevice(const String& deviceName);

    // Tab management
    void setCurrentTab(Events::UI::TabState tab);

    // === UI STATE CONTROL ===
    void setSuppressArcEvents(bool suppress) { state.suppressArcEvents = suppress; }
    void setSuppressDropdownEvents(bool suppress) { state.suppressDropdownEvents = suppress; }
    bool isSuppressingArcEvents() const { return state.suppressArcEvents; }
    bool isSuppressingDropdownEvents() const { return state.suppressDropdownEvents; }

    // === EVENT SUBSCRIPTION ===
    void subscribeToStateChanges(StateChangeCallback callback);

    // === EXTERNAL COMMUNICATION ===
    void publishStatusUpdate();
    void publishStatusRequest(bool delayed = false);

    // === UTILITY ===
    const char* getTabName(Events::UI::TabState tab) const;
    bool hasValidSelection() const { return state.hasValidSelection(); }

   private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Internal state
    AudioAppState state;
    bool initialized = false;
    std::vector<StateChangeCallback> callbacks;

    // Internal operations
    void notifyStateChange(const AudioStateChangeEvent& event);
    void autoSelectDeviceIfNeeded();
    void markDevicesAsStale();
    void updateDeviceFromStatus(const AudioLevel& device);
    String findBestDeviceToSelect() const;
    void updateTimestamp();

    // Device management helpers
    void ensureValidSelections();
    bool isDeviceAvailable(const String& deviceName) const;
};

}  // namespace Audio
}  // namespace Application