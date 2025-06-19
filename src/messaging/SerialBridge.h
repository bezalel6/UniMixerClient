#pragma once

namespace Messaging {
namespace Serial {

/**
 * Initialize the serial bridge
 */
bool init();

/**
 * Deinitialize the serial bridge
 */
void deinit();

/**
 * Update the serial bridge (process incoming data)
 */
void update();

}  // namespace Serial
}  // namespace Messaging