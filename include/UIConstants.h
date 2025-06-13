#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

// UI Constants for consistent initialization throughout the app

// Default text values for labels
#define UI_LABEL_EMPTY ""           // Completely empty label
#define UI_LABEL_DASH "-"           // Dash placeholder for empty values
#define UI_LABEL_SPACE " "          // Single space for visual spacing
#define UI_LABEL_LOADING "..."      // Loading indicator
#define UI_LABEL_UNKNOWN "Unknown"  // Unknown value placeholder
#define UI_LABEL_NONE "None"        // None value placeholder
#define UI_LABEL_OFFLINE "Offline"  // Offline status
#define UI_LABEL_ONLINE "Online"    // Online status

// Connection status default values
#define UI_STATUS_DISCONNECTED "Disconnected"
#define UI_STATUS_CONNECTING "Connecting..."
#define UI_STATUS_CONNECTED "Connected"
#define UI_STATUS_ERROR "Error"
#define UI_STATUS_FAILED "Failed"

// Audio device default values
#define UI_AUDIO_DEVICE_NONE "No Device"
#define UI_AUDIO_DEVICE_DEFAULT "Default Device"
#define UI_AUDIO_DEVICE_SYSTEM "System Audio"

// Network default values
#define UI_NETWORK_NO_SSID "No Network"
#define UI_NETWORK_NO_IP "No IP"
#define UI_NETWORK_DISCONNECTED "Disconnected"

// MQTT default values
#define UI_MQTT_DISCONNECTED "Disconnected"
#define UI_MQTT_CONNECTING "Connecting..."
#define UI_MQTT_CONNECTED "Connected"

#endif  // UI_CONSTANTS_H