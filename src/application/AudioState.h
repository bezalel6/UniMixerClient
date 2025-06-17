#pragma once

#include "AudioTypes.h"
#include "../events/UiEventHandlers.h"
#include <vector>
#include <Arduino.h>

namespace Application {
namespace Audio {

/**
 * Pure data container for audio system state
 * No business logic, just data and accessors
 */
class AudioState {
   public:
    // Core audio data
    AudioStatus status;
    Events::UI::TabState currentTab = Events::UI::TabState::MASTER;

    // Device selection state
    String selectedMainDevice;
    String selectedDevice1;  // For balance
    String selectedDevice2;  // For balance

    // UI state flags
    bool suppressArcEvents = false;
    bool suppressDropdownEvents = false;

    // Timing
    unsigned long lastUpdateTime = 0;

    // Methods
    void clear();
    bool hasDevices() const;
    AudioLevel* findDevice(const String& processName);
    const AudioLevel* findDevice(const String& processName) const;
    String getCurrentSelectedDevice() const;
    int getCurrentSelectedVolume() const;
    bool isCurrentDeviceMuted() const;

    // State queries
    bool hasValidSelection() const;
    bool isInMasterTab() const { return currentTab == Events::UI::TabState::MASTER; }
    bool isInBalanceTab() const { return currentTab == Events::UI::TabState::BALANCE; }

   private:
    void updateTimestamp();
};

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
};

}  // namespace Audio
}  // namespace Application