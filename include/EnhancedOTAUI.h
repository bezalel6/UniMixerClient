#pragma once

#include <lvgl.h>

namespace UI {
namespace OTA {

/**
 * @brief Enhanced OTA User Interface
 * 
 * Provides a comprehensive, responsive UI for the multithreaded OTA system with:
 * 
 * - Real-time progress bar with smooth animations
 * - Download speed and ETA display
 * - Live log with timestamped messages
 * - System statistics and performance metrics
 * - Always-responsive control buttons
 * - Color-coded status indicators
 * - Professional visual design
 */

/**
 * @brief Create the enhanced OTA screen
 * 
 * Creates a full-screen OTA interface with all components:
 * - Progress bar and percentage display
 * - Download speed and time remaining
 * - Scrolling log area with timestamps  
 * - System performance statistics
 * - Exit, retry, and reboot buttons
 * 
 * @return true if screen created successfully, false otherwise
 */
bool createEnhancedOTAScreen();

/**
 * @brief Update the enhanced OTA screen
 * 
 * Updates all UI elements with current progress data:
 * - Progress bar animation
 * - Status text and colors
 * - Download statistics
 * - Button visibility based on state
 * 
 * This should be called regularly (10-60 FPS) for smooth updates.
 * The function includes throttling to prevent UI overload.
 */
void updateEnhancedOTAScreen();

/**
 * @brief Add a log message to the OTA log display
 * 
 * Adds a timestamped message to the scrolling log area.
 * The log automatically scrolls to show the latest messages.
 * 
 * @param message The message to add to the log
 */
void addLogMessage(const char* message);

/**
 * @brief Destroy the enhanced OTA screen
 * 
 * Safely removes all UI elements and cleans up resources.
 * Properly handles LVGL object deletion and event callback removal.
 */
void destroyEnhancedOTAScreen();

/**
 * @brief Check if the enhanced OTA screen is created
 * 
 * @return true if screen exists, false otherwise
 */
bool isEnhancedOTAScreenCreated();

} // namespace OTA
} // namespace UI