#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <Arduino.h>
#include <vector>

namespace Application {
namespace Audio {

// Structure to hold audio level data
struct AudioLevel {
    String processName;
    int volume;
    bool isMuted = false;
    unsigned long lastUpdate;
    bool stale = false;
};

struct AudioDevice {
    String friendlyName;
    float volume;
    bool isMuted;
    String state;
};

// Structure to hold complete audio status
struct AudioStatus {
    std::vector<AudioLevel> audioLevels;
    AudioDevice defaultDevice;
    unsigned long timestamp;
    bool hasDefaultDevice = false;
};

}  // namespace Audio
}  // namespace Application

#endif  // AUDIO_TYPES_H