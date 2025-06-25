#pragma once

namespace Messaging {
namespace Serial {

/**
 * Initialize the enhanced serial bridge with robust message framing
 */
bool init();

/**
 * Deinitialize the serial bridge
 */
void deinit();

/**
 * Update the serial bridge (process incoming data, check timeouts)
 */
void update();

/**
 * Print detailed statistics about serial communication performance
 */
void printStatistics();

}  // namespace Serial
}  // namespace Messaging
