# OTA System Refactoring Plan

## Overview
The current OTA system has multiple competing implementations that create complexity, maintenance burden, and reliability issues. This plan consolidates everything into a single, simple, effective OTA system.

## Current Problems

### 🚨 Critical Issues
1. **Multiple Implementations**: 3-4 different OTA systems coexist
   - `OTAManager` (single-threaded, complex state machine)
   - `MultithreadedOTA` (over-engineered with 4+ tasks)
   - `OTAApplication` (thin wrapper)
   - `MultithreadedOTAApplication` (another thin wrapper)

2. **Over-Engineering**:
   - 4+ FreeRTOS tasks for simple HTTP download
   - Multiple queues, mutexes, and synchronization primitives
   - Complex inter-task communication
   - Excessive abstraction layers

3. **Code Duplication**: Similar functionality implemented multiple times
4. **Resource Waste**: Multiple implementations consume memory unnecessarily
5. **Configuration Conflicts**: Different systems use different approaches

## New Architecture: Single Simple OTA System

### Core Principle
**"One simple, reliable OTA implementation that actually works"**

### Design Philosophy
- **Simplicity over complexity**
- **Reliability over features**
- **Maintainability over performance**
- **Single responsibility principle**
- **Smooth LVGL performance** - dedicated UI thread for responsive interface

## Proposed Architecture

### 1. Dual-Core SimpleOTA Class
```cpp
class SimpleOTA {
public:
    // Core interface - just what's needed
    static bool init(const Config& config);
    static void deinit();
    static bool startUpdate();
    static bool isRunning();
    static uint8_t getProgress();
    static const char* getStatusMessage();
    static void cancel();

private:
    // Simple state machine
    enum State {
        IDLE,
        CONNECTING,
        DOWNLOADING,
        INSTALLING,
        COMPLETE,
        ERROR
    };

    // Dual-core task functions
    static void uiTask(void* parameter);     // Core 0 - LVGL updates
    static void otaTask(void* parameter);    // Core 1 - OTA operations

    // Thread-safe progress sharing
    static void updateProgress(uint8_t progress, const char* message);
    static void getProgressSafe(uint8_t* progress, char* message);
};
```

### 2. No Separate Applications
- Remove `OTAApplication` and `MultithreadedOTAApplication`
- Integrate directly into existing boot flow
- Use existing `BootManager` for mode switching

### 3. Configuration Unification
```cpp
struct OTAConfig {
    const char* serverURL;
    const char* wifiSSID;
    const char* wifiPassword;
    uint32_t timeoutMS;
    bool showProgress;
    bool autoReboot;
};
```

### 4. Dual-Core Architecture for LVGL Performance
- **Core 0**: Dedicated LVGL UI thread (smooth 60 FPS)
- **Core 1**: Simple OTA operations (network, download, install)
- **Minimal synchronization** - shared progress data with mutex
- **No complex task orchestration** - just UI + OTA threads

#### Why Dual-Core?
- **LVGL Performance**: LVGL requires consistent timing for smooth animations
- **Non-blocking UI**: Network operations won't freeze the interface
- **Optimal ESP32 usage**: Use both cores efficiently
- **Simple architecture**: Only 2 tasks vs 4+ in current system

## Implementation Plan

### Phase 1: Remove Existing Implementations (Week 1)

#### Step 1: Identify All OTA Files
- [x] `src/ota/OTAManager.h/.cpp`
- [x] `src/ota/MultithreadedOTA.h/.cpp`
- [x] `src/ota/OTAApplication.h/.cpp`
- [x] `src/ota/MultithreadedOTAApplication.h/.cpp`
- [x] `include/MultithreadedOTA.h`
- [x] `include/MultithreadedOTAApplication.h`
- [x] `include/OTAConfig.h`

#### Step 2: Create New Simple Implementation
```cpp
// include/SimpleOTA.h
#pragma once
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

class SimpleOTA {
public:
    struct Config {
        const char* serverURL;
        const char* wifiSSID;
        const char* wifiPassword;
        uint32_t timeoutMS = 300000;  // 5 minutes
        bool showProgress = true;
        bool autoReboot = true;
    };

    static bool init(const Config& config);
    static void deinit();
    static bool startUpdate();
    static void handleUpdate();  // Call from main loop
    static bool isRunning();
    static uint8_t getProgress();
    static const char* getStatusMessage();
    static void cancel();

private:
    enum State {
        IDLE,
        CONNECTING,
        DOWNLOADING,
        INSTALLING,
        COMPLETE,
        ERROR
    };

    static State currentState;
    static Config config;
    static uint8_t progress;
    static char statusMessage[128];
    static uint32_t startTime;
    static bool cancelled;

    static void setState(State newState, const char* message = nullptr);
    static void updateProgress(uint8_t prog, const char* message = nullptr);
    static bool connectWiFi();
    static void onProgress(int current, int total);
    static void updateUI();
};
```

#### Step 3: Dual-Core Implementation
```cpp
// src/ota/SimpleOTA.cpp
#include "SimpleOTA.h"
#include "../application/ui/LVGLMessageHandler.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char* TAG = "SimpleOTA";

// Static members
SimpleOTA::State SimpleOTA::currentState = IDLE;
SimpleOTA::Config SimpleOTA::config = {};
uint8_t SimpleOTA::progress = 0;
char SimpleOTA::statusMessage[128] = "Ready";
bool SimpleOTA::cancelled = false;

// Task handles
static TaskHandle_t uiTaskHandle = nullptr;
static TaskHandle_t otaTaskHandle = nullptr;

// Synchronization
static SemaphoreHandle_t progressMutex = nullptr;
static HTTPUpdate httpUpdate;

bool SimpleOTA::init(const Config& cfg) {
    config = cfg;
    currentState = IDLE;
    progress = 0;
    cancelled = false;
    strcpy(statusMessage, "OTA Ready");

    // Create mutex for progress sharing
    progressMutex = xSemaphoreCreateMutex();
    if (!progressMutex) {
        ESP_LOGE(TAG, "Failed to create progress mutex");
        return false;
    }

    // Create UI task on Core 0 (LVGL core)
    xTaskCreatePinnedToCore(
        uiTask,
        "OTA_UI",
        8192,          // Stack size
        nullptr,       // Parameters
        10,            // Priority (high for UI responsiveness)
        &uiTaskHandle,
        0              // Core 0 - UI core
    );

    ESP_LOGI(TAG, "Simple OTA initialized with dual-core architecture");
    return true;
}

void SimpleOTA::deinit() {
    cancel();

    // Clean up tasks
    if (uiTaskHandle) {
        vTaskDelete(uiTaskHandle);
        uiTaskHandle = nullptr;
    }
    if (otaTaskHandle) {
        vTaskDelete(otaTaskHandle);
        otaTaskHandle = nullptr;
    }

    // Clean up mutex
    if (progressMutex) {
        vSemaphoreDelete(progressMutex);
        progressMutex = nullptr;
    }

    WiFi.disconnect();
    ESP_LOGI(TAG, "Simple OTA deinitialized");
}

bool SimpleOTA::startUpdate() {
    if (currentState != IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return false;
    }

    updateProgress(0, "Starting OTA update...");
    currentState = CONNECTING;
    cancelled = false;

    // Create OTA task on Core 1 (network core)
    xTaskCreatePinnedToCore(
        otaTask,
        "OTA_NET",
        12288,         // Stack size
        nullptr,       // Parameters
        8,             // Priority (medium - lower than UI)
        &otaTaskHandle,
        1              // Core 1 - network core
    );

    ESP_LOGI(TAG, "Starting OTA update from: %s", config.serverURL);
    return true;
}

void SimpleOTA::uiTask(void* parameter) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        // Check if OTA is complete
        if (currentState == COMPLETE || currentState == ERROR) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Get current progress safely
        uint8_t currentProgress;
        char currentMessage[128];
        getProgressSafe(&currentProgress, currentMessage);

        // Update LVGL UI (smooth 60 FPS)
        if (config.showProgress) {
            Application::LVGLMessageHandler::updateOtaScreenProgress(
                currentProgress, currentMessage);
        }

        // Maintain 60 FPS UI updates
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(16)); // ~60 FPS
    }
}

void SimpleOTA::otaTask(void* parameter) {
    uint32_t startTime = millis();

    // Setup HTTPUpdate callbacks
    httpUpdate.onProgress([](int current, int total) {
        if (total > 0) {
            uint8_t downloadProgress = 20 + ((current * 70) / total);  // 20-90%
            updateProgress(downloadProgress, "Downloading...");
        }
    });

    // State machine for OTA operations
    while (currentState != COMPLETE && currentState != ERROR && !cancelled) {
        switch (currentState) {
            case CONNECTING:
                if (connectWiFi()) {
                    currentState = DOWNLOADING;
                    updateProgress(20, "Connected - starting download...");
                } else if (millis() - startTime > 30000) {  // 30s timeout
                    currentState = ERROR;
                    updateProgress(0, "WiFi connection timeout");
                }
                break;

            case DOWNLOADING:
                {
                    updateProgress(25, "Downloading firmware...");
                    HTTPUpdateResult result = httpUpdate.update(config.serverURL);

                    switch (result) {
                        case HTTP_UPDATE_OK:
                            currentState = COMPLETE;
                            updateProgress(100, "Update successful - rebooting...");
                            if (config.autoReboot) {
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                ESP.restart();
                            }
                            break;
                        case HTTP_UPDATE_NO_UPDATES:
                            currentState = ERROR;
                            updateProgress(0, "No updates available");
                            break;
                        case HTTP_UPDATE_FAILED:
                            currentState = ERROR;
                            updateProgress(0, httpUpdate.getLastErrorString().c_str());
                            break;
                    }
                }
                break;

            default:
                // Should not reach here
                break;
        }

        // Yield to prevent watchdog
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Task cleanup
    otaTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool SimpleOTA::connectWiFi() {
    static bool wifiStarted = false;

    if (!wifiStarted) {
        WiFi.begin(config.wifiSSID, config.wifiPassword);
        wifiStarted = true;
        updateProgress(5, "Connecting to WiFi...");
    }

    if (WiFi.status() == WL_CONNECTED) {
        updateProgress(20, "WiFi connected");
        return true;
    }

    // Update progress based on time
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate > 2000) {  // Update every 2 seconds
        static uint8_t wifiProgress = 5;
        wifiProgress = min(wifiProgress + 2, 18);  // 5-18%
        updateProgress(wifiProgress, "Connecting to WiFi...");
        lastUpdate = millis();
    }

    return false;
}

void SimpleOTA::updateProgress(uint8_t prog, const char* message) {
    if (xSemaphoreTake(progressMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        progress = prog;
        if (message) {
            strncpy(statusMessage, message, sizeof(statusMessage) - 1);
            statusMessage[sizeof(statusMessage) - 1] = '\0';
        }
        xSemaphoreGive(progressMutex);
        ESP_LOGI(TAG, "Progress: %d%% - %s", prog, statusMessage);
    }
}

void SimpleOTA::getProgressSafe(uint8_t* prog, char* message) {
    if (xSemaphoreTake(progressMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *prog = progress;
        strcpy(message, statusMessage);
        xSemaphoreGive(progressMutex);
    }
}

// Public getters (thread-safe)
bool SimpleOTA::isRunning() {
    return currentState != IDLE && currentState != COMPLETE && currentState != ERROR;
}

uint8_t SimpleOTA::getProgress() {
    uint8_t prog = 0;
    if (xSemaphoreTake(progressMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        prog = progress;
        xSemaphoreGive(progressMutex);
    }
    return prog;
}

const char* SimpleOTA::getStatusMessage() {
    return statusMessage;  // Note: For true thread safety, this should also use mutex
}

void SimpleOTA::cancel() {
    cancelled = true;
    currentState = ERROR;
    updateProgress(0, "Cancelled by user");
    WiFi.disconnect();
}
```

### Phase 2: Integration (Week 2)

#### Step 1: Update Boot Flow
- Modify `BootManager` to use `SimpleOTA` instead of applications
- Remove OTA application concepts

#### Step 2: Update UI Integration
- Modify `LVGLMessageHandler` to work with `SimpleOTA`
- Remove application-specific UI code

#### Step 3: Configuration Update
- Create simple configuration system
- Remove multiple config files

### Phase 3: Testing & Validation (Week 3)

#### Step 1: Unit Testing
- Test each state transition
- Test error conditions
- Test cancellation

#### Step 2: Integration Testing
- Test full OTA flow
- Test network interruption
- Test power cycle recovery

#### Step 3: Performance Testing
- Measure memory usage
- Measure update speed
- Test UI responsiveness

### Phase 4: Documentation & Cleanup (Week 4)

#### Step 1: Remove Old Files
- Delete all old OTA implementations
- Clean up includes and dependencies
- Update build configuration

#### Step 2: Update Documentation
- Create simple usage guide
- Update architecture documentation
- Add troubleshooting guide

## Benefits of New Architecture

### 🎯 Simplicity
- **Single OTA implementation** instead of 3-4 competing systems
- **Simple dual-core design** instead of complex 4+ task orchestration
- **Minimal synchronization** - just one mutex for progress sharing
- **Easy to understand** - 400 lines instead of 2000+

### 🔒 Reliability
- **Fewer moving parts** = fewer failure points
- **Simpler error handling** - clear error states
- **Easier debugging** - two clear execution paths
- **Less memory usage** - no complex queue/task overhead

### 🎨 LVGL Performance
- **Dedicated UI thread** on Core 0 for smooth 60 FPS
- **Never blocked** - OTA operations run on separate core
- **Consistent timing** - vTaskDelayUntil for precise UI updates
- **Always responsive** - UI thread has highest priority

### 🔧 Maintainability
- **Two files to maintain** instead of 8+ files
- **Clear interfaces** - simple public API
- **Easy to modify** - straightforward dual-core implementation
- **Self-contained** - minimal external dependencies

### 📊 Performance
- **Optimal core usage** - UI on Core 0, OTA on Core 1
- **Lower memory footprint** - no complex task/queue overhead
- **Faster startup** - no complex initialization
- **Better responsiveness** - dedicated UI thread
- **Cleaner resource management** - explicit init/deinit

## Migration Strategy

### Phase 1: Parallel Implementation
1. Keep existing implementations temporarily
2. Implement new `SimpleOTA` alongside
3. Add feature flag to switch between implementations
4. Test new implementation thoroughly

### Phase 2: Gradual Migration
1. Update UI to use new implementation
2. Update boot flow to use new implementation
3. Run extensive testing
4. Monitor for issues

### Phase 3: Full Cutover
1. Remove old implementations once new one is proven
2. Clean up all references
3. Update documentation
4. Commit final changes

## Risk Mitigation

### Development Risks
- **Backup all existing implementations** before deletion
- **Maintain git branches** for rollback capability
- **Incremental testing** at each phase
- **Feature flags** to switch between implementations

### Production Risks
- **Thorough testing** on development hardware
- **Staged rollout** to small groups first
- **Monitoring** for success/failure rates
- **Rollback plan** ready if issues arise

## Success Metrics

### Code Quality
- **Lines of code**: Target 80% reduction (2000+ → 400 lines)
- **Cyclomatic complexity**: Target 70% reduction
- **File count**: Target 90% reduction (8+ → 1 file)
- **Memory usage**: Target 50% reduction

### Reliability
- **Update success rate**: Target >95%
- **Error recovery**: Target 100% graceful handling
- **UI responsiveness**: Target <100ms response time
- **Resource leaks**: Target zero memory/handle leaks

### Maintainability
- **Development time**: Target 70% reduction for new features
- **Bug fix time**: Target 60% reduction
- **Testing effort**: Target 50% reduction
- **Documentation clarity**: Target complete coverage

## Conclusion

This refactoring plan addresses the core issues with the current OTA system:

1. **Eliminates complexity** through architectural simplification
2. **Improves reliability** by reducing failure points
3. **Enhances maintainability** through cleaner code structure
4. **Reduces resource usage** by eliminating overhead
5. **Increases developer productivity** through simpler interfaces

The new `SimpleOTA` system provides everything needed for reliable firmware updates without the complexity and maintenance burden of the current multi-implementation approach.

**Next Steps:**
1. Get approval for this refactoring approach
2. Create feature branch for development
3. Implement Phase 1 (parallel implementation)
4. Begin testing and validation process
