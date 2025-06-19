#pragma once

#include "../events/UiEventHandlers.h"
#include <map>
#include <vector>
#include <Arduino.h>

namespace Application {
namespace Audio {

// =============================================================================
// BASIC DATA STRUCTURES
// =============================================================================

/**
 * Represents an audio device/process with its current state
 * (Compatible with existing AudioLevel for messaging layer)
 */
struct AudioLevel {
    String processName;
    String friendlyName;
    int volume = 0;
    bool isMuted = false;
    unsigned long lastUpdate = 0;
    bool stale = false;
    String state;  // For system default device state
};

// Alias for clarity in some contexts
using AudioDevice = AudioLevel;

/**
 * Complete audio system status from external source
 * Uses hash map for efficient device access by name
 */
struct AudioStatus {
    std::map<String, AudioLevel> audioDevices;  // Key: processName, Value: AudioLevel
    AudioDevice defaultDevice;
    unsigned long timestamp = 0;
    bool hasDefaultDevice = false;

    // Helper methods
    void clear() {
        audioDevices.clear();
        defaultDevice = AudioDevice();
        timestamp = 0;
        hasDefaultDevice = false;
    }

    bool isEmpty() const {
        return audioDevices.empty();
    }

    AudioLevel* findDevice(const String& processName) {
        auto it = audioDevices.find(processName);
        return (it != audioDevices.end()) ? &it->second : nullptr;
    }

    const AudioLevel* findDevice(const String& processName) const {
        auto it = audioDevices.find(processName);
        return (it != audioDevices.end()) ? &it->second : nullptr;
    }

    bool hasDevice(const String& processName) const {
        return audioDevices.find(processName) != audioDevices.end();
    }

    void addOrUpdateDevice(const AudioLevel& device) {
        audioDevices[device.processName] = device;
    }

    void removeDevice(const String& processName) {
        audioDevices.erase(processName);
    }

    size_t getDeviceCount() const {
        return audioDevices.size();
    }

    // Compatibility methods for existing code that expects vectors
    std::vector<AudioLevel> getAudioLevels() const {
        std::vector<AudioLevel> levels;
        levels.reserve(audioDevices.size());
        for (const auto& pair : audioDevices) {
            levels.push_back(pair.second);
        }
        return levels;
    }

    void setAudioLevels(const std::vector<AudioLevel>& levels) {
        audioDevices.clear();
        for (const auto& level : levels) {
            audioDevices[level.processName] = level;
        }
    }

    // Iterator access for range-based loops
    auto begin() { return audioDevices.begin(); }
    auto end() { return audioDevices.end(); }
    auto begin() const { return audioDevices.begin(); }
    auto end() const { return audioDevices.end(); }
};

// =============================================================================
// APPLICATION STATE
// =============================================================================

/**
 * Current application state and user selections
 */
struct AudioAppState {
    // Current audio data
    AudioStatus currentStatus;

    // UI state
    Events::UI::TabState currentTab = Events::UI::TabState::MASTER;

    // Device selections for different tabs (direct pointers to AudioLevel objects in hash map)
    AudioLevel* selectedMainDevice = nullptr;
    AudioLevel* selectedDevice1 = nullptr;  // For balance tab
    AudioLevel* selectedDevice2 = nullptr;  // For balance tab

    // UI interaction flags

    // Timing
    unsigned long lastUpdateTime = 0;

    // Helper methods
    void clear() {
        currentStatus.clear();
        currentTab = Events::UI::TabState::MASTER;
        selectedMainDevice = nullptr;
        selectedDevice1 = nullptr;
        selectedDevice2 = nullptr;
        lastUpdateTime = 0;
    }

    bool hasDevices() const {
        return !currentStatus.audioDevices.empty();
    }

    AudioLevel* findDevice(const String& processName) {
        return currentStatus.findDevice(processName);
    }

    const AudioLevel* findDevice(const String& processName) const {
        return currentStatus.findDevice(processName);
    }

    const AudioLevel* getCurrentSelectedDevice() const {
        switch (currentTab) {
            case Events::UI::TabState::MASTER:
            case Events::UI::TabState::SINGLE:
                return selectedMainDevice;
            case Events::UI::TabState::BALANCE:
                return selectedDevice1;  // Primary device for balance
            default:
                return selectedMainDevice;
        }
    }

    AudioLevel* getCurrentSelectedDevice() {
        switch (currentTab) {
            case Events::UI::TabState::MASTER:
            case Events::UI::TabState::SINGLE:
                return selectedMainDevice;
            case Events::UI::TabState::BALANCE:
                return selectedDevice1;  // Primary device for balance
            default:
                return selectedMainDevice;
        }
    }

    String getCurrentSelectedDeviceName() const {
        const AudioLevel* device = getCurrentSelectedDevice();
        if (device) {
            return device->processName;
        }

        // If no device selected and we're in Master tab, use default device
        if (currentTab == Events::UI::TabState::MASTER && currentStatus.hasDefaultDevice) {
            return currentStatus.defaultDevice.friendlyName.isEmpty() ? String("Default Device") : currentStatus.defaultDevice.friendlyName;
        }

        return String("");
    }

    int getCurrentSelectedVolume() const {
        const AudioLevel* device = getCurrentSelectedDevice();
        if (device) {
            ESP_LOGD("Audio Data", "Current device: %s, volume: %d", device->processName.c_str(), device->volume);
            return device->volume;
        }

        // If no device selected and we're in Master tab, use default device
        if (currentTab == Events::UI::TabState::MASTER && currentStatus.hasDefaultDevice) {
            ESP_LOGD("Audio Data", "Using default device volume: %d", currentStatus.defaultDevice.volume);
            return currentStatus.defaultDevice.volume;
        }

        ESP_LOGD("Audio Data", "No device selected for volume control");
        return 0;
    }

    bool isCurrentDeviceMuted() const {
        const AudioLevel* device = getCurrentSelectedDevice();
        if (device) {
            return device->isMuted;
        }

        // If no device selected and we're in Master tab, use default device
        if (currentTab == Events::UI::TabState::MASTER && currentStatus.hasDefaultDevice) {
            return currentStatus.defaultDevice.isMuted;
        }

        return false;
    }

    bool hasValidSelection() const {
        return getCurrentSelectedDevice() != nullptr;
    }

    // Helper to validate device pointers are still valid in current hash map
    void validateDeviceSelections() {
        // Check if selected device pointers still exist in our hash map
        selectedMainDevice = validateDevicePointer(selectedMainDevice);
        selectedDevice1 = validateDevicePointer(selectedDevice1);
        selectedDevice2 = validateDevicePointer(selectedDevice2);
    }

    // Tab state queries
    bool isInMasterTab() const { return currentTab == Events::UI::TabState::MASTER; }
    bool isInSingleTab() const { return currentTab == Events::UI::TabState::SINGLE; }
    bool isInBalanceTab() const { return currentTab == Events::UI::TabState::BALANCE; }

   private:
    // Helper to validate a device pointer still exists in the hash map
    AudioLevel* validateDevicePointer(AudioLevel* device) {
        if (!device) return nullptr;

        // Check if device still exists in hash map by looking up its name
        AudioLevel* found = currentStatus.findDevice(device->processName);
        return (found == device) ? device : nullptr;  // Only valid if same pointer
    }

    void updateTimestamp() {
        lastUpdateTime = millis();
    }
};

// =============================================================================
// EVENTS
// =============================================================================

/**
 * Event data for state changes
 */
struct AudioStateChangeEvent {
    enum Type {
        DEVICES_UPDATED,
        SELECTION_CHANGED,
        VOLUME_CHANGED,
        TAB_CHANGED,
        MUTE_CHANGED
    };

    Type type;
    String deviceName;
    int volume = 0;
    Events::UI::TabState tab = Events::UI::TabState::MASTER;

    // Factory methods for common events
    static AudioStateChangeEvent devicesUpdated() {
        return {DEVICES_UPDATED, "", 0, Events::UI::TabState::MASTER};
    }

    static AudioStateChangeEvent selectionChanged(const String& device) {
        return {SELECTION_CHANGED, device, 0, Events::UI::TabState::MASTER};
    }

    static AudioStateChangeEvent volumeChanged(const String& device, int volume) {
        return {VOLUME_CHANGED, device, volume, Events::UI::TabState::MASTER};
    }

    static AudioStateChangeEvent tabChanged(Events::UI::TabState tab) {
        return {TAB_CHANGED, "", 0, tab};
    }

    static AudioStateChangeEvent muteChanged(const String& device) {
        return {MUTE_CHANGED, device, 0, Events::UI::TabState::MASTER};
    }
};

}  // namespace Audio
}  // namespace Application
