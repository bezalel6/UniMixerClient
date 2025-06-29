# Built-in USB Serial/JTAG Debugging Quick Reference

## Quick Start Commands

```bash
# 1. Build debug environment
pio run -e esp32-8048S070C-debug

# 2. Upload with built-in USB Serial/JTAG (no external hardware needed!)
pio run -e esp32-8048S070C-debug -t upload

# 3. Start debugging (same USB cable as programming)
pio debug -e esp32-8048S070C-debug
```

## Essential GDB Commands

```gdb
# Monitor ESP32 system
(gdb) monitor esp32 semihosting enable
(gdb) monitor esp32 xtensa timer enable

# Key breakpoints for multithreaded debugging
(gdb) break TaskManager::lvglTask
(gdb) break TaskManager::networkTask
(gdb) break xSemaphoreTakeRecursive

# Conditional breakpoint for mutex contention
(gdb) break xSemaphoreTakeRecursive if $arg0 == lvglMutex

# Task analysis
(gdb) info threads
(gdb) thread apply all bt
```

## Logic Analyzer Monitoring (Optional - Currently Disabled)

**Logic analyzer GPIO profiling is disabled by default.** To enable:
1. Set `LOGIC_ANALYZER_ENABLED = 1` in `include/DebugUtils.h`
2. Uncomment GPIO pins in `src/application/PerformanceProfiler.cpp`
3. Rebuild with debug environment

| GPIO | Signal | Critical Threshold |
|------|--------|-------------------|
| GPIO10 | Core 0 Activity | >80% duty cycle = overload |
| GPIO11 | Core 1 Activity | <20% duty cycle = underutilized |
| GPIO12 | LVGL Processing | >20ms high = bottleneck |
| GPIO13 | Mutex Contention | >1ms high = severe contention |

## Performance Monitoring Code

```cpp
// Continuous monitoring
TaskProfiler::startContinuousMonitoring();

// One-time analysis
TaskProfiler::printDetailedTaskStats();
TaskProfiler::detectTaskStarvation();
TaskProfiler::detectMutexContention();

// Memory analysis
MemoryProfiler::printHeapFragmentation();
```

## Critical Performance Thresholds

- **LVGL Processing**: <16ms (60fps target)
- **Mutex Lock Time**: <1ms
- **Core 0 Load**: <75%
- **Memory Fragmentation**: <50%
- **Audio Latency**: <1ms
- **UI Response**: <50ms

## Common Issues & Quick Fixes

### High Mutex Contention (PERF warnings >1ms)
```cpp
// Reduce LVGL mutex scope
if (lvglTryLock(10)) {  // Shorter timeout
    // Minimal UI work only
    lvglUnlock();
}
```

### Core 0 Overload (Task stats >80% CPU)
```cpp
// Move messaging task to Core 1
xTaskCreatePinnedToCore(messagingTask, "Messaging", 
                       STACK_SIZE, NULL, PRIORITY, 
                       &handle, 1);  // Core 1
```

### Memory Fragmentation >75%
```cpp
// Pre-allocate critical buffers
static uint8_t lvgl_buffer[64*1024];  // At startup
```

## Emergency Debugging

If system is unresponsive:
1. Check for mutex deadlock via serial output PERF warnings
2. Use `monitor reset halt` in GDB
3. Examine task states with `info threads`
4. Look for stack overflows with `bt`

## Performance Targets After Optimization

- LVGL: <10ms average processing
- Mutex contention: <1% total time  
- Balanced cores: 60-70% / 30-40%
- Stable 60fps UI
- <50ms UI responsiveness 

# Performance Monitoring Commands

## Real-time System Health
```cpp
// Add to any task for live monitoring
ESP_LOGI("HEALTH", "Heap: %u, PSRAM: %u, Queue: %d", 
         ESP.getFreeHeap(), ESP.getFreePsram(), uxQueueMessagesWaiting(lvglMessageQueue));

// Check task stack usage
ESP_LOGI("STACK", "LVGL: %u, Audio: %u, Network: %u", 
         uxTaskGetStackHighWaterMark(lvglTaskHandle),
         uxTaskGetStackHighWaterMark(audioTaskHandle), 
         uxTaskGetStackHighWaterMark(networkTaskHandle));
```

## LVGL Performance Diagnostics
```cpp
// Monitor LVGL processing times
uint32_t start = millis();
lv_timer_handler();
uint32_t duration = millis() - start;
if (duration > 20) ESP_LOGW("PERF", "LVGL slow: %ums", duration);

// Check rendering state
lv_disp_t *disp = lv_disp_get_default();
ESP_LOGI("RENDER", "In progress: %s", disp->rendering_in_progress ? "YES" : "NO");
```

## Mutex Contention Analysis
```cpp
// Track mutex wait times
uint32_t start = millis();
bool acquired = lvglTryLock(50);
uint32_t wait_time = millis() - start;
if (wait_time > 10) ESP_LOGW("MUTEX", "LVGL wait: %ums", wait_time);
if (acquired) lvglUnlock();
```

## Memory Leak Detection
```cpp
// Periodic memory reporting
static uint32_t last_heap = 0;
uint32_t current_heap = ESP.getFreeHeap();
int32_t heap_delta = (int32_t)current_heap - (int32_t)last_heap;
ESP_LOGI("MEM", "Heap: %u (%+d), Largest: %u", 
         current_heap, heap_delta, ESP.getMaxAllocHeap());
last_heap = current_heap;
```

## SD Card Performance
```cpp
// Monitor SD operation times
uint32_t start = millis();
bool result = Hardware::SD::directoryExists("/logos");
uint32_t duration = millis() - start;
ESP_LOGI("SD", "Directory check: %ums, result: %s", duration, result ? "OK" : "FAIL");
```

## Emergency Diagnostics
```cpp
// When system becomes unresponsive:
void emergencyDiagnostics() {
    ESP_LOGE("EMERGENCY", "=== SYSTEM DIAGNOSTICS ===");
    ESP_LOGE("EMERGENCY", "Free heap: %u bytes", ESP.getFreeHeap());
    ESP_LOGE("EMERGENCY", "Free PSRAM: %u bytes", ESP.getFreePsram());
    ESP_LOGE("EMERGENCY", "Message queue: %d/%d", 
             uxQueueMessagesWaiting(lvglMessageQueue), LVGL_MESSAGE_QUEUE_SIZE);
    ESP_LOGE("EMERGENCY", "LVGL mutex state: %s", 
             lvglMutex ? "EXISTS" : "NULL");
    ESP_LOGE("EMERGENCY", "Tasks running: %s", tasksRunning ? "YES" : "NO");
    ESP_LOGE("EMERGENCY", "Current task: %s", pcTaskGetTaskName(NULL));
    ESP_LOGE("EMERGENCY", "=== END DIAGNOSTICS ===");
}
```

## Performance Benchmarking
```cpp
// System load test
void performanceTest() {
    ESP_LOGI("TEST", "Starting performance test...");
    
    // Test LVGL processing under load
    for (int i = 0; i < 100; i++) {
        uint32_t start = micros();
        lv_timer_handler();
        uint32_t duration = micros() - start;
        if (duration > 20000) { // >20ms
            ESP_LOGW("TEST", "LVGL iteration %d: %uus (SLOW)", i, duration);
        }
        vTaskDelay(pdMS_TO_TICKS(16)); // 60fps simulation
    }
    
    ESP_LOGI("TEST", "Performance test completed");
}
```
