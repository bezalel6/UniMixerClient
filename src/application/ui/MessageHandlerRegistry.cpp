#include "MessageHandlerRegistry.h"
#include "handlers/VolumeMessageHandler.h"
#include "handlers/DeviceMessageHandler.h"
#include "handlers/SystemMessageHandler.h"
#include "handlers/UIMessageHandler.h"
#include <esp_log.h>

namespace Application {
namespace UI {

using namespace Application::LVGLMessageHandler;

static const char* TAG = "MessageHandlerRegistry";

// Message type names for debugging
const char* MessageHandlerRegistry::messageTypeNames[] = {
    "FPS_DISPLAY",           // MSG_UPDATE_FPS_DISPLAY = 0
    "BUILD_TIME_DISPLAY",    // MSG_UPDATE_BUILD_TIME_DISPLAY = 1
    "SCREEN_CHANGE",         // MSG_SCREEN_CHANGE = 2
    "REQUEST_DATA",          // MSG_REQUEST_DATA = 3
    "MASTER_VOLUME",         // MSG_UPDATE_MASTER_VOLUME = 4
    "SINGLE_VOLUME",         // MSG_UPDATE_SINGLE_VOLUME = 5
    "BALANCE_VOLUME",        // MSG_UPDATE_BALANCE_VOLUME = 6
    "MASTER_DEVICE",         // MSG_UPDATE_MASTER_DEVICE = 7
    "SINGLE_DEVICE",         // MSG_UPDATE_SINGLE_DEVICE = 8
    "BALANCE_DEVICES",       // MSG_UPDATE_BALANCE_DEVICES = 9
    "SHOW_STATE_OVERVIEW",   // MSG_SHOW_STATE_OVERVIEW = 10
    "UPDATE_STATE_OVERVIEW", // MSG_UPDATE_STATE_OVERVIEW = 11
    "HIDE_STATE_OVERVIEW",   // MSG_HIDE_STATE_OVERVIEW = 12
    "SD_STATUS",             // MSG_UPDATE_SD_STATUS = 13
    "FORMAT_SD_REQUEST",     // MSG_FORMAT_SD_REQUEST = 14
    "FORMAT_SD_CONFIRM",     // MSG_FORMAT_SD_CONFIRM = 15
    "FORMAT_SD_PROGRESS",    // MSG_FORMAT_SD_PROGRESS = 16
    "FORMAT_SD_COMPLETE",    // MSG_FORMAT_SD_COMPLETE = 17
};

MessageHandlerRegistry& MessageHandlerRegistry::getInstance() {
    static MessageHandlerRegistry instance;
    return instance;
}

void MessageHandlerRegistry::registerHandler(LVGLMessageType_t type, MessageHandler handler) {
    handlers[type] = handler;
    ESP_LOGD(TAG, "Registered handler for message type: %s", getMessageTypeName(type));
}

void MessageHandlerRegistry::registerHandlers(const std::vector<std::pair<LVGLMessageType_t, MessageHandler>>& handlerList) {
    for (const auto& [type, handler] : handlerList) {
        registerHandler(type, handler);
    }
}

bool MessageHandlerRegistry::dispatch(const LVGLMessage_t* message) {
    if (!message) {
        ESP_LOGW(TAG, "Null message received");
        return false;
    }
    
    auto handler = handlers.find(message->type);
    if (handler != handlers.end()) {
        handler->second(message);
        return true;
    } else {
        ESP_LOGD(TAG, "No handler registered for message type: %d (%s)", 
                 message->type, getMessageTypeName(message->type));
        return false;
    }
}

void MessageHandlerRegistry::initializeAllHandlers() {
    if (initialized) {
        ESP_LOGW(TAG, "Registry already initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing message handler registry");
    
    // Register UI handlers
    registerHandlers({
        {MSG_UPDATE_FPS_DISPLAY, Handlers::UIMessageHandler::handleFpsDisplay},
        {MSG_UPDATE_BUILD_TIME_DISPLAY, Handlers::UIMessageHandler::handleBuildTimeDisplay},
        {MSG_SCREEN_CHANGE, Handlers::UIMessageHandler::handleScreenChange},
        {MSG_REQUEST_DATA, Handlers::UIMessageHandler::handleRequestData}
    });
    
    // Register volume handlers
    registerHandlers({
        {MSG_UPDATE_MASTER_VOLUME, Handlers::VolumeMessageHandler::handleMasterVolume},
        {MSG_UPDATE_SINGLE_VOLUME, Handlers::VolumeMessageHandler::handleSingleVolume},
        {MSG_UPDATE_BALANCE_VOLUME, Handlers::VolumeMessageHandler::handleBalanceVolume}
    });
    
    // Register device handlers
    registerHandlers({
        {MSG_UPDATE_MASTER_DEVICE, Handlers::DeviceMessageHandler::handleMasterDevice},
        {MSG_UPDATE_SINGLE_DEVICE, Handlers::DeviceMessageHandler::handleSingleDevice},
        {MSG_UPDATE_BALANCE_DEVICES, Handlers::DeviceMessageHandler::handleBalanceDevices}
    });
    
    // Register system handlers
    registerHandlers({
        {MSG_SHOW_STATE_OVERVIEW, Handlers::SystemMessageHandler::handleShowStateOverview},
        {MSG_UPDATE_STATE_OVERVIEW, Handlers::SystemMessageHandler::handleUpdateStateOverview},
        {MSG_HIDE_STATE_OVERVIEW, Handlers::SystemMessageHandler::handleHideStateOverview},
        {MSG_UPDATE_SD_STATUS, Handlers::SystemMessageHandler::handleSDStatus},
        {MSG_FORMAT_SD_REQUEST, Handlers::SystemMessageHandler::handleFormatSDRequest},
        {MSG_FORMAT_SD_CONFIRM, Handlers::SystemMessageHandler::handleFormatSDConfirm},
        {MSG_FORMAT_SD_PROGRESS, Handlers::SystemMessageHandler::handleFormatSDProgress},
        {MSG_FORMAT_SD_COMPLETE, Handlers::SystemMessageHandler::handleFormatSDComplete}
    });
    
    // Initialize individual handlers
    Handlers::VolumeMessageHandler::registerHandler();
    Handlers::DeviceMessageHandler::registerHandler();
    Handlers::SystemMessageHandler::registerHandler();
    Handlers::UIMessageHandler::registerHandler();
    
    initialized = true;
    ESP_LOGI(TAG, "Message handler registry initialized with %d handlers", handlers.size());
}

bool MessageHandlerRegistry::hasHandler(LVGLMessageType_t type) const {
    return handlers.find(type) != handlers.end();
}

const char* MessageHandlerRegistry::getMessageTypeName(int messageType) {
    if (messageType >= 0 && 
        messageType < (sizeof(messageTypeNames) / sizeof(messageTypeNames[0]))) {
        return messageTypeNames[messageType];
    }
    return "UNKNOWN";
}

} // namespace UI
} // namespace Application