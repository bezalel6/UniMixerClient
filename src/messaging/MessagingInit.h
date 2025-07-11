#pragma once

#include <Arduino.h>

namespace Messaging {

// Initialize the brutal messaging system
bool initMessaging();

// Shutdown the messaging system
void shutdownMessaging();

// Get status string
String getMessagingStatus();

}  // namespace Messaging
