#ifndef INTERRUPT_MESSAGING_ENGINE_H
#define INTERRUPT_MESSAGING_ENGINE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include "MessageCore.h"
#include "MessageData.h"
#include "../include/BinaryProtocol.h"

namespace Messaging {
namespace Core1 {

/**
 * Interrupt-Driven Messaging Engine for Core 1
 *
 * INTEGRATES WITH MessageCore ARCHITECTURE:
 * - Leverages MessageCore's transport system
 * - Uses existing internal/external message routing
 * - Implements interrupt-driven Serial transport
 * - Provides zero busy-waiting Core 1 messaging engine
 * - Routes messages using MessageCore's shouldRouteToCore1() logic
 */
class InterruptMessagingEngine {
   public:
    /**
     * Initialize the interrupt-driven messaging engine on Core 1
     * Integrates with MessageCore as the primary Serial transport
     */
    static bool init();

    /**
     * Start the messaging engine task on Core 1
     */
    static bool start();

    /**
     * Stop the messaging engine
     */
    static void stop();

    /**
     * Check if engine is running
     */
    static bool isRunning();

    /**
     * Get performance statistics
     */
    static void getStats(uint32_t& messagesReceived, uint32_t& messagesSent,
                         uint32_t& bufferOverruns, uint32_t& core1Routed);

    /**
     * Get binary protocol statistics
     */
    static const BinaryProtocol::ProtocolStatistics& getBinaryStats();

   private:
    static bool initialized;
    static bool running;
    static TaskHandle_t messagingTaskHandle;

    // MessageCore integration
    static MessageCore* messageCore;

    // Binary protocol framing
    static BinaryProtocol::BinaryProtocolFramer* binaryFramer;

    // Ring buffer configuration for interrupt-driven I/O
    static const size_t UART_RX_BUFFER_SIZE = 4096;
    static const size_t UART_TX_BUFFER_SIZE = 2048;
    static const size_t MESSAGE_QUEUE_SIZE = 64;
    static const size_t INTERNAL_MSG_QUEUE_SIZE = 128;

    // Task configuration
    static const size_t MESSAGING_TASK_STACK_SIZE = 8 * 1024;  // 8KB stack for Core 1 messaging

   public:
    // Binary-safe message structure for queue
    struct BinaryMessage {
        uint8_t* data;
        size_t length;
    };

   private:
    // Inter-core communication queues
    static QueueHandle_t incomingDataQueue;       // Raw UART data
    static QueueHandle_t outgoingMessageQueue;    // Binary messages to send
    static QueueHandle_t core1ProcessingQueue;    // Messages routed to Core 1
    static QueueHandle_t core0NotificationQueue;  // Notifications to Core 0

    // Synchronization
    static SemaphoreHandle_t uartMutex;
    static SemaphoreHandle_t routingMutex;

    // Message parsing buffers
    static char rxBuffer[UART_RX_BUFFER_SIZE];
    static size_t rxBufferPos;
    static char txBuffer[UART_TX_BUFFER_SIZE];

    // Statistics tracking
    static uint32_t messagesReceived;
    static uint32_t messagesSent;
    static uint32_t bufferOverruns;
    static uint32_t core1RoutedMessages;
    static uint32_t interruptCount;

    // =============================================================================
    // CORE 1 MESSAGING TASK
    // =============================================================================

    /**
     * Main Core 1 messaging task - processes all message I/O
     */
    static void messagingTask(void* parameter);

    /**
     * Process incoming UART data with message framing
     */
    static void processIncomingData();

    /**
     * Process outgoing messages from queue
     */
    static void processOutgoingMessages();

    /**
     * Process Core 1 routed messages (External message processing)
     */
    static void processCore1Messages();

    /**
     * Route internal messages using MessageCore logic
     */
    static void routeInternalMessage(const InternalMessage& message);

    // =============================================================================
    // INTERRUPT-DRIVEN UART HANDLING
    // =============================================================================

    /**
     * UART interrupt handler - minimal processing, queue data
     */
    static void IRAM_ATTR uartISR(void* arg);

    /**
     * Initialize UART with interrupt support
     */
    static bool initUART();

    /**
     * Send raw data via UART (interrupt-safe)
     */
    static bool sendRawData(const char* data, size_t length);

    // =============================================================================
    // MessageCore TRANSPORT INTEGRATION
    // =============================================================================

    /**
     * Register interrupt engine as MessageCore Serial transport
     */
    static bool registerWithMessageCore();

    /**
     * Transport interface functions for MessageCore
     */
    static bool transportSend(const String& payload);
    static bool transportIsConnected();
    static void transportUpdate();
    static String transportGetStatus();
    // REMOVED: transportInit() - caused infinite recursion, transport init handled by init()/start()
    static void transportDeinit();

    // =============================================================================
    // MESSAGE PARSING AND ROUTING
    // =============================================================================

    /**
     * Parse complete message from buffer
     */
    static bool parseCompleteMessage(const char* buffer, size_t length, ExternalMessage& message);

    /**
     * Route message using MessageCore's shouldRouteToCore1() logic
     */
    static void routeExternalMessage(const ExternalMessage& message);

    /**
     * Handle Core 1 processing for external messages
     */
    static void processExternalMessageOnCore1(const ExternalMessage& message);

    /**
     * Send notification to Core 0
     */
    static void notifyCore0(const InternalMessage& message);
};

// =============================================================================
// CORE 1 MESSAGE ROUTING UTILITIES
// =============================================================================

/**
 * Utility functions for Core 1 message processing
 */
namespace Core1Utils {

/**
 * Process external message on Core 1 (heavy processing)
 */
bool processExternalMessage(const ExternalMessage& message);

/**
 * Convert external to internal messages using Core 1 processing
 */
std::vector<InternalMessage> convertExternalToInternal(const ExternalMessage& external);

/**
 * Validate and sanitize external message on Core 1
 */
bool validateExternalMessage(ExternalMessage& message);

}  // namespace Core1Utils

}  // namespace Core1
}  // namespace Messaging

#endif  // INTERRUPT_MESSAGING_ENGINE_H
