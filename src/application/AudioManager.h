#pragma once

#include "AudioData.h"
#include "../messaging/protocol/MessageData.h"
#include <functional>
#include <vector>
#include <map>

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
    String getCurrentDevice() const { return state.getCurrentSelectedDeviceName(); }
    int getCurrentVolume() const { return state.getCurrentSelectedVolume(); }
    bool isCurrentDeviceMuted() const { return state.isCurrentDeviceMuted(); }
    bool hasDevices() const { return state.hasDevices(); }

    // Device queries
    std::vector<AudioLevel> getAllDevices() const { return state.currentStatus.getAudioLevels(); }
    AudioLevel* getDevice(const String& processName);
    const AudioLevel* getDevice(const String& processName) const;

    // === EXTERNAL DATA INPUT ===
    void onAudioStatusReceived(const AudioStatus& status);

    // === USER ACTIONS ===

    // Device selection
    void selectDevice(const String& deviceName);
    void selectDevice(AudioLevel* device);
    void selectBalanceDevices(const String& device1, const String& device2);

    // Volume control
    void setVolumeForCurrentDevice(int volume);
    void setDeviceVolume(const String& deviceName, int volume);

    // Balance-specific volume control (NEW)
    void setBalanceVolume(int volume, float balance_ratio = 0.0f);
    void setBalanceDeviceVolumes(int device1Volume, int device2Volume);

    // Mute control
    void muteCurrentDevice();
    void unmuteCurrentDevice();
    void muteDevice(const String& deviceName);
    void unmuteDevice(const String& deviceName);

    // Balance-specific mute control (NEW)
    void muteBalanceDevices();
    void unmuteBalanceDevices();

    // Tab management
    void setCurrentTab(Events::UI::TabState tab);

    // === UI STATE CONTROL ===

    // === EVENT SUBSCRIPTION ===
    void subscribeToStateChanges(StateChangeCallback callback);

    // === EXTERNAL COMMUNICATION ===
    void publishStatusUpdate();
    void publishStatusRequest(bool delayed = false);

    // === UTILITY ===
    const char* getTabName(Events::UI::TabState tab) const;
    bool hasValidSelection() const { return state.hasValidSelection(); }

    // === SMART BEHAVIOR ===
    void performSmartAutoSelection();  // Proactive auto-selection for current context

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
    void refreshDevicePointers(const String& primaryDeviceName, const String& singleDeviceName, const String& device1Name, const String& device2Name);
    void refreshDevicePointersIfNeeded(const String& deviceName);

    // Logo checking helpers (moved from MessageCore)
    void checkAndRequestLogosForAudioProcesses(const Messaging::AudioStatusData& statusData);
    void checkSingleProcessLogo(const char* processName);

    // Logo request debouncing
    std::map<String, unsigned long> lastLogoCheckTime;
    static const unsigned long LOGO_CHECK_DEBOUNCE_MS = 30000;  // 30 seconds
};

}  // namespace Audio
}  // namespace Application
