#ifndef SYSTEM_STATE_OVERLAY_H
#define SYSTEM_STATE_OVERLAY_H

#include <Arduino.h>
#include <lvgl.h>
#include <functional>

namespace Application {
namespace UI {
namespace System {

/**
 * SystemStateOverlay - Manages the comprehensive system state overlay UI
 * 
 * This class handles the creation, updating, and management of the system
 * state overlay that displays current system, network, and audio information.
 * Extracted from LVGLMessageHandler to maintain single responsibility principle.
 */
class SystemStateOverlay {
public:
    // Singleton access
    static SystemStateOverlay& getInstance();

    // Overlay operations
    void show();
    void update();
    void hide();
    bool isVisible() const;

    // State update data structure
    struct StateData {
        // System information
        uint32_t free_heap;
        uint32_t free_psram;
        uint32_t cpu_freq;
        uint32_t uptime_ms;
        
        // Network information
        char wifi_status[32];
        int wifi_rssi;
        char ip_address[16];
        
        // Audio state
        char current_tab[16];
        char main_device[64];
        int main_device_volume;
        bool main_device_muted;
        char balance_device1[64];
        int balance_device1_volume;
        bool balance_device1_muted;
        char balance_device2[64];
        int balance_device2_volume;
        bool balance_device2_muted;
    };

    // Update the overlay with new state data
    void updateStateData(const StateData& data);

    // Action callbacks
    void setFormatSDCallback(std::function<void()> callback);
    void setRestartCallback(std::function<void()> callback);
    void setRefreshCallback(std::function<void()> callback);

private:
    SystemStateOverlay() = default;
    ~SystemStateOverlay() = default;
    SystemStateOverlay(const SystemStateOverlay&) = delete;
    SystemStateOverlay& operator=(const SystemStateOverlay&) = delete;

    // UI creation methods
    void createOverlay();
    void createSystemColumn(lv_obj_t* parent);
    void createNetworkColumn(lv_obj_t* parent);
    void createAudioColumn(lv_obj_t* parent);
    void createActionButtons(lv_obj_t* parent);
    
    // UI update methods
    void updateSystemInfo(const StateData& data);
    void updateNetworkInfo(const StateData& data);
    void updateAudioInfo(const StateData& data);

    // Cleanup
    void destroyOverlay();

    // UI elements
    lv_obj_t* overlay = nullptr;
    lv_obj_t* overlay_bg = nullptr;
    lv_obj_t* overlay_panel = nullptr;
    lv_obj_t* system_label = nullptr;
    lv_obj_t* network_label = nullptr;
    lv_obj_t* audio_label = nullptr;
    lv_obj_t* heap_bar = nullptr;
    lv_obj_t* wifi_bar = nullptr;

    // Action callbacks
    std::function<void()> formatSDCallback = nullptr;
    std::function<void()> restartCallback = nullptr;
    std::function<void()> refreshCallback = nullptr;

    // State
    bool visible = false;
    StateData currentState;
};

} // namespace System
} // namespace UI
} // namespace Application

#endif // SYSTEM_STATE_OVERLAY_H