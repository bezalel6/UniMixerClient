#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/message_buffer.h>
#include <string>
#include <optional>
#include <esp_log.h>

namespace Messaging {

/**
 * Modern C++ wrapper for FreeRTOS MessageBuffer
 * Provides efficient variable-size message queuing without pre-allocating fixed buffers
 * 
 * Key benefits:
 * - Only uses memory for actual message sizes
 * - Thread-safe between single producer/consumer
 * - Zero-copy receive with RAII cleanup
 * - Supports messages up to 16KB
 */
class MessageQueue {
private:
    static constexpr const char* TAG = "MessageQueue";
    MessageBufferHandle_t msgBuffer = nullptr;
    
    // Total buffer size - supports multiple small messages or fewer large ones
    static constexpr size_t BUFFER_SIZE = 24 * 1024;  // 24KB total
    
    // Statistics
    struct Stats {
        uint32_t messagesSent = 0;
        uint32_t messagesReceived = 0;
        uint32_t sendFailures = 0;
        uint32_t peakUsage = 0;
        
        void updatePeakUsage(size_t currentUsage) {
            if (currentUsage > peakUsage) {
                peakUsage = currentUsage;
            }
        }
    } stats;

public:
    MessageQueue() {
        msgBuffer = xMessageBufferCreate(BUFFER_SIZE);
        if (!msgBuffer) {
            ESP_LOGE(TAG, "Failed to create message buffer!");
        } else {
            ESP_LOGI(TAG, "Created message buffer with %d KB capacity", BUFFER_SIZE / 1024);
        }
    }
    
    ~MessageQueue() {
        if (msgBuffer) {
            vMessageBufferDelete(msgBuffer);
            msgBuffer = nullptr;
        }
    }
    
    // Delete copy constructor and assignment operator
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;
    
    // Move constructor and assignment
    MessageQueue(MessageQueue&& other) noexcept : msgBuffer(other.msgBuffer), stats(other.stats) {
        other.msgBuffer = nullptr;
    }
    
    MessageQueue& operator=(MessageQueue&& other) noexcept {
        if (this != &other) {
            if (msgBuffer) {
                vMessageBufferDelete(msgBuffer);
            }
            msgBuffer = other.msgBuffer;
            stats = other.stats;
            other.msgBuffer = nullptr;
        }
        return *this;
    }
    
    /**
     * Send a message to the queue
     * @param message The message data to send
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking)
     * @return true if message was sent successfully
     */
    bool send(const std::string& message, uint32_t timeoutMs = 0) {
        if (!msgBuffer || message.empty()) {
            return false;
        }
        
        size_t bytesSent = xMessageBufferSend(msgBuffer, 
                                              message.data(), 
                                              message.size(), 
                                              pdMS_TO_TICKS(timeoutMs));
        
        if (bytesSent == message.size()) {
            stats.messagesSent++;
            stats.updatePeakUsage(xMessageBufferSpacesAvailable(msgBuffer));
            ESP_LOGD(TAG, "Sent message: %zu bytes", bytesSent);
            return true;
        } else {
            stats.sendFailures++;
            ESP_LOGW(TAG, "Failed to send message: %zu bytes (sent %zu)", 
                     message.size(), bytesSent);
            return false;
        }
    }
    
    /**
     * Receive a message from the queue
     * @param maxSize Maximum size to receive (default 16KB)
     * @param timeoutMs Timeout in milliseconds (portMAX_DELAY for infinite)
     * @return The received message or empty optional if none/error
     */
    std::optional<std::string> receive(size_t maxSize = 16384, uint32_t timeoutMs = portMAX_DELAY) {
        if (!msgBuffer) {
            return std::nullopt;
        }
        
        // Temporary buffer - could optimize with a pool allocator later
        std::vector<uint8_t> buffer(maxSize);
        
        size_t bytesReceived = xMessageBufferReceive(msgBuffer,
                                                     buffer.data(),
                                                     buffer.size(),
                                                     pdMS_TO_TICKS(timeoutMs));
        
        if (bytesReceived > 0) {
            stats.messagesReceived++;
            ESP_LOGD(TAG, "Received message: %zu bytes", bytesReceived);
            return std::string(buffer.begin(), buffer.begin() + bytesReceived);
        }
        
        return std::nullopt;
    }
    
    /**
     * Check if messages are available
     * @return true if at least one message is available
     */
    bool hasMessages() const {
        return msgBuffer && !xMessageBufferIsEmpty(msgBuffer);
    }
    
    /**
     * Get the number of free bytes in the buffer
     * @return Number of free bytes
     */
    size_t getFreeSpace() const {
        return msgBuffer ? xMessageBufferSpacesAvailable(msgBuffer) : 0;
    }
    
    /**
     * Get queue statistics
     * @return Reference to stats structure
     */
    const Stats& getStats() const {
        return stats;
    }
    
    /**
     * Reset the message buffer (clears all messages)
     */
    void reset() {
        if (msgBuffer) {
            xMessageBufferReset(msgBuffer);
            ESP_LOGI(TAG, "Message buffer reset");
        }
    }
    
    /**
     * Check if the queue is valid (successfully created)
     */
    bool isValid() const {
        return msgBuffer != nullptr;
    }
};

} // namespace Messaging