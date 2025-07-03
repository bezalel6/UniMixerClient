# OTA Boot Flow - Dual Core Architecture

## Overview

This diagram shows the complete flow when the ESP32 boots into OTA mode, including all components, tasks, and their interactions across both cores.

## Boot Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                           OTA BOOT FLOW - DUAL CORE                                         │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                                             │
│  ┌─────────────────────────────────┐                    ┌─────────────────────────────────┐                │
│  │           CORE 0                │                    │           CORE 1                │                │
│  │     (Application Core)          │                    │     (Protocol Core)             │                │
│  └─────────────────────────────────┘                    └─────────────────────────────────┘                │
│                                                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                                    BOOT SEQUENCE (Both Cores)                                          │ │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                             │
│  1. ESP32 Boots                                                                                             │
│     ↓                                                                                                       │
│  2. BootManager::getCurrentMode()                                                                           │
│     ↓                                                                                                       │
│  3. Detects OTA Flag in NVS                                                                                 │
│     ↓                                                                                                       │
│  4. BootManager::getCurrentMode() returns OTA_UPDATE                                                        │
│     ↓                                                                                                       │
│  5. main.cpp switches to OTAApplication                                                                     │
│                                                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                                    CORE 0 - OTA APPLICATION                                            │ │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                             │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │   OTAApplication│    │   DisplayManager│    │ LVGLMessageHandler│   │   TaskManager   │                  │
│  │   ::init()      │    │   ::init()      │    │   ::init()      │    │   ::init()      │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│           │                       │                       │                       │                        │
│           │                       │                       │                       │                        │
│           ▼                       ▼                       ▼                       ▼                        │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │ • DeviceManager │    │ • SmartDisplay  │    │ • Message Queue │    │ • LVGL Task     │                  │
│  │ • Hardware Init │    │ • SPI Bus Config│    │ • UI Handlers   │    │ • Audio Task    │                  │
│  │ • GPIO Setup    │    │ • LVGL Init     │    │ • OTA Screen    │    │ • OTA Progress  │                  │
│  │ • I2C Setup     │    │ • UI Init       │    │ • Event Handlers│    │   Queue         │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│           │                       │                       │                       │                        │
│           │                       │                       │                       │                        │
│           └───────────────────────┼───────────────────────┼───────────────────────┘                        │
│                                   │                       │                                                │
│                                   ▼                       ▼                                                │
│                          ┌─────────────────┐    ┌─────────────────┐                                        │
│                          │ Enhanced OTA    │    │ • LVGL Task     │                                        │
│                          │ Screen Created  │    │   (Core 0)      │                                        │
│                          │ • Progress Bar  │    │ • Audio Task    │                                        │
│                          │ • Log Display   │    │   (Core 0)      │                                        │
│                          │ • Exit Button   │    │ • UI Updates    │                                        │
│                          │ • Retry/Reboot  │    │ • Touch Events  │                                        │
│                          │   (on failure)  │    │ • Animations    │                                        │
│                          └─────────────────┘    └─────────────────┘                                        │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       ▼                                                │
│                          ┌─────────────────┐    ┌─────────────────┐                                        │
│                          │ OTAManager      │    │ Message Queue   │                                        │
│                          │ ::init()        │    │ Processing      │                                        │
│                          └─────────────────┘    └─────────────────┘                                        │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       ▼                                                │
│                          ┌─────────────────┐    ┌─────────────────┐                                        │
│                          │ OTAManager      │    │ UI Event Loop   │                                        │
│                          │ ::startOTA()    │    │ • Progress      │                                        │
│                          └─────────────────┘    │   Updates       │                                        │
│                                   │             │ • Button Events │                                        │
│                                   │             │ • Log Updates   │                                        │
│                                   │             │ • Status Updates│                                        │
│                                   │             └─────────────────┘                                        │
│                                   │                       │                                                │
│                                   ▼                       │                                                │
│                          ┌─────────────────┐              │                                                │
│                          │ OTA State       │              │                                                │
│                          │ Machine         │              │                                                │
│                          │ • USER_INITIATED│              │                                                │
│                          │ • CONNECTING    │              │                                                │
│                          │ • CONNECTED     │              │                                                │
│                          │ • DOWNLOADING   │              │                                                │
│                          │ • INSTALLING    │              │                                                │
│                          │ • SUCCESS/FAILED│              │                                                │
│                          └─────────────────┘              │                                                │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       │                                                │
│                          ┌─────────────────┐              │                                                │
│                          │ Network         │              │                                                │
│                          │ Operations      │              │                                                │
│                          │ • WiFi Connect  │              │                                                │
│                          │ • HTTP Download │              │                                                │
│                          │ • Firmware      │              │                                                │
│                          │   Verification  │              │                                                │
│                          └─────────────────┘              │                                                │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       │                                                │
│                          ┌─────────────────┐              │                                                │
│                          │ Progress        │              │                                                │
│                          │ Updates         │              │                                                │
│                          │ • updateProgress│              │                                                │
│                          │ • TaskManager   │              │                                                │
│                          │ • LVGL Handler  │              │                                                │
│                          │ • UI Updates    │              │                                                │
│                          └─────────────────┘              │                                                │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       │                                                │
│                          ┌─────────────────┐              │                                                │
│                          │ Completion      │              │                                                │
│                          │ • Success:      │              │                                                │
│                          │   Auto Reboot   │              │                                                │
│                          │ • Failure:      │              │                                                │
│                          │   Show Controls │              │                                                │
│                          └─────────────────┘              │                                                │
│                                   │                       │                                                │
│                                   │                       │                                                │
│                                   ▼                       ▼                                                │
│                          ┌─────────────────────────────────────────────────────────────────┐              │
│                          │                    USER CONTROLS                               │              │
│                          │                                                                 │              │
│                          │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │              │
│                          │  │   EXIT OTA  │  │    RETRY    │  │   REBOOT    │              │              │
│                          │  │   (Always)  │  │ (On Failure)│  │ (On Failure)│              │              │
│                          │  └─────────────┘  └─────────────┘  └─────────────┘              │              │
│                          │                                                                 │              │
│                          │  • Exit OTA: Clear flag → Reboot to Normal                      │              │
│                          │  • Retry: Request OTA → Restart OTA Process                     │              │
│                          │  • Reboot: Clear flag → Reboot to Normal                        │              │
│                          └─────────────────────────────────────────────────────────────────┘              │
│                                                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                                    CORE 1 - MESSAGING ENGINE                                           │ │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                             │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │ Interrupt       │    │ MessageAPI      │    │ Core1           │    │ Message         │                  │
│  │ Messaging       │    │ ::init()        │    │ Interrupt       │    │ Handlers        │                  │
│  │ Engine          │    │                 │    │ Messaging       │    │                 │                  │
│  │ ::init()        │    │                 │    │ Engine          │    │                 │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│           │                       │                       │                       │                        │
│           │                       │                       │                       │                        │
│           ▼                       ▼                       ▼                       ▼                        │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │ • Interrupt     │    │ • Message Bus   │    │ • Core 1 Task   │    │ • OTA Progress  │                  │
│  │   Handlers      │    │ • Event Queue   │    │ • Message       │    │   Messages      │                  │
│  │ • Serial        │    │ • Publish/      │    │   Processing    │    │ • UI Updates    │                  │
│  │   Transport     │    │   Subscribe     │    │ • Inter-Core    │    │ • Status        │                  │
│  │ • Buffer        │    │ • Debug Log     │    │   Communication │    │   Messages      │                  │
│  │   Management    │    │   Messages      │    │ • Task          │    │ • Error         │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│           │                       │                       │                       │                        │
│           │                       │                       │                       │                        │
│           └───────────────────────┼───────────────────────┼───────────────────────┘                        │
│                                   │                       │                                                │
│                                   ▼                       ▼                                                │
│                          ┌─────────────────┐    ┌─────────────────┐                                        │
│                          │ Message         │    │ Inter-Core      │                                        │
│                          │ Processing      │    │ Communication   │                                        │
│                          │ • OTA Progress  │    │ • Core 0 ↔ Core 1│                                        │
│                          │ • UI Updates    │    │ • Message       │                                        │
│                          │ • Status        │    │   Routing       │                                        │
│                          │   Messages      │    │ • Event         │                                        │
│                          │ • Debug Log     │    │   Synchronization│                                        │
│                          └─────────────────┘    └─────────────────┘                                        │
│                                                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                                    SHARED RESOURCES                                                     │ │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                             │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │ BootManager     │    │ NVS Storage     │    │ Shared Memory   │    │ Hardware        │                  │
│  │ • Boot Mode     │    │ • OTA Flag      │    │ • Progress Data │    │ • Display       │                  │
│  │ • Mode          │    │ • Settings      │    │ • Status Info   │    │ • Touch         │                  │
│  │   Detection     │    │ • Configuration │    │ • Error States  │    │ • WiFi          │                  │
│  │ • Flag          │    │ • Boot History  │    │ • Network Info  │    │ • SPI/I2C       │                  │
│  │   Management    │    │ • State Data    │    │ • UI State      │    │ • GPIO          │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│                                                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐ │
│  │                                    ERROR HANDLING & RECOVERY                                            │ │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                                             │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐                  │
│  │ Network         │    │ Download        │    │ Installation    │    │ User            │                  │
│  │ Failures        │    │ Failures        │    │ Failures        │    │ Recovery        │                  │
│  │ • WiFi Timeout  │    │ • Server Error  │    │ • Flash Error   │    │ • Exit Button   │                  │
│  │ • Connection    │    │ • Network       │    │ • Verification  │    │ • Retry Button  │                  │
│  │   Lost          │    │   Timeout       │    │   Failed        │    │ • Reboot Button │                  │
│  │ • DNS Failure   │    │ • Corrupted     │    │ • Memory Error  │    │ • Auto Recovery │                  │
│  │ • Auth Failure  │    │   Firmware      │    │ • Power Loss    │    │ • Manual        │                  │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘                  │
│                                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Key Components and Their Roles

### Core 0 (Application Core)
- **OTAApplication**: Main OTA orchestrator
- **DisplayManager**: Display and LVGL initialization
- **LVGLMessageHandler**: UI event handling and screen management
- **TaskManager**: Task scheduling and OTA progress queue
- **OTAManager**: OTA state machine and network operations

### Core 1 (Protocol Core)
- **Interrupt Messaging Engine**: Inter-core communication
- **MessageAPI**: Message bus and event routing
- **Message Handlers**: Processing OTA and UI messages

### Shared Resources
- **BootManager**: Boot mode detection and flag management
- **NVS Storage**: Persistent storage for OTA flags and settings
- **Hardware**: Display, touch, WiFi, and other peripherals

## State Flow

1. **Boot Detection**: BootManager detects OTA flag in NVS
2. **Application Switch**: main.cpp switches to OTAApplication
3. **Initialization**: All components initialize in sequence
4. **UI Setup**: Enhanced OTA screen created with controls
5. **OTA Process**: State machine handles WiFi, download, installation
6. **Progress Updates**: Real-time updates via TaskManager queue
7. **Completion**: Success (auto-reboot) or failure (show controls)
8. **User Recovery**: Exit, retry, or reboot options

## Error Recovery

- **Network Issues**: Retry button restarts OTA process
- **Download Failures**: Retry or exit options
- **Installation Errors**: Exit to normal mode
- **User Escape**: Exit button always available
- **System Hangs**: Hardware reset returns to normal mode

## Benefits of This Architecture

- **Dual Core**: UI responsiveness on Core 0, messaging on Core 1
- **Fault Tolerance**: Multiple recovery options for different failure modes
- **User Control**: Always-available exit option prevents stuck states
- **Real-time Feedback**: Progress updates and detailed logging
- **Clean Separation**: OTA mode completely isolated from normal operation
