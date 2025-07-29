#ifndef VOLUME_MESSAGE_HANDLER_H
#define VOLUME_MESSAGE_HANDLER_H

#include "../LVGLMessageHandler.h"
#include <lvgl.h>
#include <functional>
#include <unordered_map>

namespace Application {
namespace UI {
namespace Handlers {

using Application::LVGLMessageHandler::LVGLMessage_t;

/**
 * VolumeMessageHandler - Handles all volume-related UI messages
 * 
 * This handler manages volume slider updates for all tabs (Master, Single, Balance).
 * Provides optimized volume update handling with O(1) lookup performance.
 */
class VolumeMessageHandler {
public:
    // Register this handler with the message system
    static void registerHandler();
    
    // Message handlers
    static void handleMasterVolume(const LVGLMessage_t* msg);
    static void handleSingleVolume(const LVGLMessage_t* msg);
    static void handleBalanceVolume(const LVGLMessage_t* msg);
    
    // Helper for updating current tab volume
    static bool updateCurrentTabVolume(int volume);
    
private:
    // Volume extraction functions
    using VolumeExtractor = int (*)(const LVGLMessage_t*);
    static int extractMasterVolume(const LVGLMessage_t* msg);
    static int extractSingleVolume(const LVGLMessage_t* msg);
    static int extractBalanceVolume(const LVGLMessage_t* msg);
    
    // Fast volume update
    static void updateVolumeSlider(lv_obj_t* slider, const LVGLMessage_t* msg);
    
    // Tab volume update functions
    using TabVolumeUpdater = bool (*)(int);
    static std::unordered_map<uint32_t, TabVolumeUpdater> tabVolumeUpdaters;
    static std::unordered_map<int, VolumeExtractor> volumeExtractors;
    
    // Initialize static maps
    static void initializeMaps();
    static bool mapsInitialized;
};

} // namespace Handlers
} // namespace UI
} // namespace Application

#endif // VOLUME_MESSAGE_HANDLER_H