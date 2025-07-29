#include "UIMessageHandler.h"
#include "../../../../include/BuildInfo.h"
#include <esp_log.h>
#include <ui/ui.h>

namespace Application {
namespace UI {
namespace Handlers {

static const char* TAG = "UIMessageHandler";

void UIMessageHandler::registerHandler() {
    // Registration will be handled by MessageHandlerRegistry
}

void UIMessageHandler::handleFpsDisplay(const LVGLMessage_t* msg) {
    if (ui_lblFPS) {
        // PERFORMANCE: Use static buffer to avoid stack allocation overhead
        static char fpsText[64];
        float actualFps = msg->data.fps_display.fps;  // Temporary fallback
        snprintf(fpsText, sizeof(fpsText), "FPS: %.1f/%.1f",
                 actualFps, msg->data.fps_display.fps);
        lv_label_set_text(ui_lblFPS, fpsText);
    }
}

void UIMessageHandler::handleBuildTimeDisplay(const LVGLMessage_t* msg) {
    if (ui_lblBuildTimeValue) {
        lv_label_set_text(ui_lblBuildTimeValue, getBuildTimeAndDate());
    }
}

void UIMessageHandler::handleScreenChange(const LVGLMessage_t* msg) {
    const auto& data = msg->data.screen_change;
    if (data.screen) {
        lv_screen_load_anim_t anim = static_cast<lv_screen_load_anim_t>(data.anim_type);
        _ui_screen_change((lv_obj_t**)&data.screen, anim, data.time, data.delay, NULL);
    }
}

void UIMessageHandler::handleRequestData(const LVGLMessage_t* msg) {
    ESP_LOGI(TAG, "Data request triggered from UI");
    // TODO: Implement data request handling when needed
}

const char* UIMessageHandler::getBuildTimeAndDate() {
    return ::getBuildTimeAndDate();  // Call global function from BuildInfo.h
}

} // namespace Handlers
} // namespace UI
} // namespace Application