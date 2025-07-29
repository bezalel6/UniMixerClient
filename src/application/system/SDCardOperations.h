#ifndef SD_CARD_OPERATIONS_H
#define SD_CARD_OPERATIONS_H

#include <Arduino.h>
#include <functional>

namespace Application {
namespace System {

/**
 * SDCardOperations - Manages SD card formatting and related operations
 * 
 * This class handles SD card formatting, progress tracking, and UI dialog
 * coordination. Extracted from LVGLMessageHandler to maintain separation of concerns.
 */
class SDCardOperations {
public:
    // Singleton access
    static SDCardOperations& getInstance();

    // Format operations
    void requestFormat();
    void confirmFormat();
    void cancelFormat();
    
    // Progress updates
    void updateProgress(uint8_t progress, const char* message);
    void completeFormat(bool success, const char* message);
    
    // Status query
    bool isFormatting() const { return formatting; }
    uint8_t getProgress() const { return currentProgress; }
    
    // Callbacks for UI updates
    using ProgressCallback = std::function<void(uint8_t progress, const char* message)>;
    using CompleteCallback = std::function<void(bool success, const char* message)>;
    
    void setProgressCallback(ProgressCallback callback) { progressCallback = callback; }
    void setCompleteCallback(CompleteCallback callback) { completeCallback = callback; }

private:
    SDCardOperations() = default;
    ~SDCardOperations() = default;
    SDCardOperations(const SDCardOperations&) = delete;
    SDCardOperations& operator=(const SDCardOperations&) = delete;
    
    // Format task implementation
    static void formatTask(void* parameter);
    void performFormat();
    
    // State
    bool formatting = false;
    uint8_t currentProgress = 0;
    char currentMessage[64] = {0};
    
    // Callbacks
    ProgressCallback progressCallback = nullptr;
    CompleteCallback completeCallback = nullptr;
    
    // Task handle
    TaskHandle_t formatTaskHandle = nullptr;
};

} // namespace System
} // namespace Application

#endif // SD_CARD_OPERATIONS_H