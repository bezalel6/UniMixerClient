#pragma once

#include "../events/UiEventHandlers.h"
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
 */
struct AudioStatus {
    std::vector<AudioLevel> audioLevels;
    AudioDevice defaultDevice;
    unsigned long timestamp = 0;
    bool hasDefaultDevice = false;

    // Helper methods
    void clear() {
        audioLevels.clear();
        defaultDevice = AudioDevice();
        timestamp = 0;
        hasDefaultDevice = false;
    }

    bool isEmpty() const {
        return audioLevels.empty();
    }

    AudioLevel* findDevice(const String& processName) {
        for (auto& device : audioLevels) {
            if (device.processName == processName) {
                return &device;
            }
        }
        return nullptr;
    }

    const AudioLevel* findDevice(const String& processName) const {
        for (const auto& device : audioLevels) {
            if (device.processName == processName) {
                return &device;
            }
        }
        return nullptr;
    }
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

    // Device selections for different tabs
    String selectedMainDevice;
    String selectedDevice1;  // For balance tab
    String selectedDevice2;  // For balance tab

    // UI interaction flags
    bool suppressArcEvents = false;
    bool suppressDropdownEvents = false;

    // Timing
    unsigned long lastUpdateTime = 0;

    // Helper methods
    void clear() {
        currentStatus.clear();
        currentTab = Events::UI::TabState::MASTER;
        selectedMainDevice = "";
        selectedDevice1 = "";
        selectedDevice2 = "";
        suppressArcEvents = false;
        suppressDropdownEvents = false;
        lastUpdateTime = 0;
    }

    bool hasDevices() const {
        return !currentStatus.audioLevels.empty();
    }

    AudioLevel* findDevice(const String& processName) {
        return currentStatus.findDevice(processName);
    }

    const AudioLevel* findDevice(const String& processName) const {
        return currentStatus.findDevice(processName);
    }

    String getCurrentSelectedDevice() const {
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

    int getCurrentSelectedVolume() const {
        const AudioLevel* device = findDevice(getCurrentSelectedDevice());
        return device ? device->volume : 0;
    }

    bool isCurrentDeviceMuted() const {
        const AudioLevel* device = findDevice(getCurrentSelectedDevice());
        return device ? device->isMuted : false;
    }

    bool hasValidSelection() const {
        return !getCurrentSelectedDevice().isEmpty() && findDevice(getCurrentSelectedDevice()) != nullptr;
    }

    bool isInMasterTab() const { return currentTab == Events::UI::TabState::MASTER; }
    bool isInSingleTab() const { return currentTab == Events::UI::TabState::SINGLE; }
    bool isInBalanceTab() const { return currentTab == Events::UI::TabState::BALANCE; }

   private:
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