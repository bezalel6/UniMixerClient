#include "AudioState.h"
#include "../hardware/DeviceManager.h"
#include <esp_log.h>

static const char* TAG = "AudioState";

namespace Application {
namespace Audio {

void AudioState::clear() {
    status.audioLevels.clear();
    status.hasDefaultDevice = false;
    status.timestamp = 0;

    selectedMainDevice = "";
    selectedDevice1 = "";
    selectedDevice2 = "";

    suppressArcEvents = false;
    suppressDropdownEvents = false;

    updateTimestamp();
}

bool AudioState::hasDevices() const {
    return !status.audioLevels.empty();
}

AudioLevel* AudioState::findDevice(const String& processName) {
    for (auto& level : status.audioLevels) {
        if (level.processName == processName) {
            return &level;
        }
    }
    return nullptr;
}

const AudioLevel* AudioState::findDevice(const String& processName) const {
    for (const auto& level : status.audioLevels) {
        if (level.processName == processName) {
            return &level;
        }
    }
    return nullptr;
}

String AudioState::getCurrentSelectedDevice() const {
    switch (currentTab) {
        case Events::UI::TabState::MASTER:
            return status.hasDefaultDevice ? status.defaultDevice.friendlyName : "";
        case Events::UI::TabState::SINGLE:
            return selectedMainDevice;
        case Events::UI::TabState::BALANCE:
            // For balance, we could return both devices or the first one
            return selectedDevice1;
        default:
            return "";
    }
}

int AudioState::getCurrentSelectedVolume() const {
    if (isInMasterTab() && status.hasDefaultDevice) {
        return (int)(status.defaultDevice.volume * 100.0f);
    }

    String deviceName = getCurrentSelectedDevice();
    if (!deviceName.isEmpty()) {
        const AudioLevel* level = findDevice(deviceName);
        if (level) {
            return level->volume;
        }
    }

    return 0;
}

bool AudioState::isCurrentDeviceMuted() const {
    if (isInMasterTab() && status.hasDefaultDevice) {
        return status.defaultDevice.isMuted;
    }

    String deviceName = getCurrentSelectedDevice();
    if (!deviceName.isEmpty()) {
        const AudioLevel* level = findDevice(deviceName);
        if (level) {
            return level->isMuted;
        }
    }

    return false;
}

bool AudioState::hasValidSelection() const {
    String selected = getCurrentSelectedDevice();
    return !selected.isEmpty();
}

void AudioState::updateTimestamp() {
    lastUpdateTime = Hardware::Device::getMillis();
}

}  // namespace Audio
}  // namespace Application