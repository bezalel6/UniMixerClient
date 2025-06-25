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
