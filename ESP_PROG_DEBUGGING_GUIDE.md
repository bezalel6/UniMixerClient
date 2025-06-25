# Built-in USB Serial/JTAG Performance Debugging Guide

This guide provides a comprehensive plan for debugging multithreaded performance issues in the UniMixerClient ESP32-S3 application using the built-in USB Serial/JTAG interface (software-only debugging).

## Overview

The application runs 5 main tasks across 2 cores:
- **Core 0**: LVGL (UI), Messaging, Audio tasks
- **Core 1**: Network, OTA tasks

Common performance issues identified:
- LVGL mutex contention causing UI freezes
- Core 0 overload (3 tasks vs 1 on Core 1)
- Emergency mode triggers due to missed deadlines
- Memory fragmentation affecting real-time performance

## Hardware Setup

### Built-in USB Serial/JTAG (Recommended)
ESP32-S3 has **built-in USB Serial/JTAG** that requires **no external connections**:
```
ESP32-S3 Board    →    Computer
USB Port          →    USB Cable (same as programming)
```
**Advantages:**
- No pin conflicts with display/touch
- No external hardware needed
- Faster and more reliable than external JTAG

### Logic Analyzer Connections (Optional - Currently Disabled)
**Logic analyzer profiling is disabled by default.** If you want to enable GPIO profiling signals:

1. Set `LOGIC_ANALYZER_ENABLED` to `1` in `include/DebugUtils.h`
2. Uncomment GPIO pin definitions in `src/application/PerformanceProfiler.cpp`
3. Connect logic analyzer probes to these **safe** GPIO pins:
```
Channel 0   →    GPIO10  (Core 0 task activity) 
Channel 1   →    GPIO11  (Core 1 task activity)
Channel 2   →    GPIO12  (LVGL processing)
Channel 3   →    GPIO13  (Mutex contention)
```
**Note:** These pins avoid conflicts with your ST7262 display and GT911 touch controller.

## Phase 1: Environment Setup & Basic Profiling

### 1.1 Build Debug Environment
```bash
# Build with built-in USB Serial/JTAG debugging enabled
pio run -e esp32-8048S070C-debug

# Upload via built-in USB Serial/JTAG
pio run -e esp32-8048S070C-debug -t upload

# Start debugging session (no external hardware needed!)
pio debug -e esp32-8048S070C-debug
```

### 1.2 Initial System Analysis
Start by gathering baseline performance data:

```cpp
// Add to main.cpp setup()
#ifdef DEBUG_PERFORMANCE
    TaskProfiler::startContinuousMonitoring();
    DebugUtils::printSystemInfo();
#endif
```

Monitor serial output for:
- Task CPU usage percentages
- Memory fragmentation levels
- Mutex contention warnings
- Performance timing violations

### 1.3 Software-Only Profiling Focus
This debugging approach focuses on software-based analysis using:
- Serial output performance statistics
- Task CPU usage monitoring
- Memory fragmentation analysis
- Mutex contention detection via timing
- Real-time system state analysis

## Phase 2: Detailed Task Analysis

### 2.1 LVGL Task Performance Analysis

**Symptoms to monitor:**
- LVGL processing >20ms warnings
- Mutex lock timeouts
- Frame rate drops below 60fps
- Emergency mode activations

**Debugging steps:**
1. **Breakpoint Analysis**
   ```gdb
   (gdb) break TaskManager::lvglTask
   (gdb) continue
   (gdb) monitor esp32 semihosting enable
   ```

2. **Real-time Monitoring**
   ```cpp
   // Check for LVGL bottlenecks
   TaskProfiler::measureLVGLPerformance();
   TaskProfiler::detectMutexContention();
   ```

3. **Software Analysis Patterns**
   - PERF warnings for LVGL >20ms = processing bottleneck
   - Mutex lock timing >1ms = severe contention  
   - Task CPU usage >80% on Core 0 = overload

### 2.2 Network Task Analysis

**Symptoms to monitor:**
- Network update >50ms warnings
- WiFi connection instability
- Core 1 underutilization

**Debugging steps:**
1. **Network Operation Timing**
   ```cpp
   PERF_START(wifi_scan);
   WiFi.scanNetworks();
   PERF_END(wifi_scan, 100000);  // 100ms threshold
   ```

2. **Monitor Network Task via Software**
   - Low CPU usage in task stats = network task starvation
   - PERF warnings >50ms = network instability

### 2.3 Memory Analysis

**Critical checks:**
```cpp
MemoryProfiler::printHeapFragmentation();
MemoryProfiler::detectMemoryLeaks();
```

**Red flags:**
- Fragmentation >75% (critical)
- Largest free block <8KB
- Allocation failures

## Phase 3: Systematic Bottleneck Identification

### 3.1 Core Load Balancing Analysis
```cpp
TaskProfiler::printDetailedTaskStats();
TaskProfiler::detectTaskStarvation();
```

**Expected healthy distribution:**
- Core 0: 60-70% (LVGL + Messaging + Audio)
- Core 1: 30-40% (Network + OTA)

**Problematic patterns:**
- Core 0 >85% = overload
- Core 1 <20% = underutilization
- Any task 0% CPU while state=Ready = starvation

### 3.2 Mutex Contention Deep Dive

**Built-in JTAG Debugging:**
1. Set conditional breakpoint on LVGL mutex
   ```gdb
   (gdb) break xSemaphoreTakeRecursive if $arg0 == lvglMutex
   (gdb) commands
   > bt
   > print *((TaskHandle_t*)pxCurrentTCB)
   > continue
   > end
   ```

2. **Analyze wait patterns:**
   - Multiple tasks waiting = priority inversion
   - Long wait times = possible deadlock
   - Frequent contention = architectural issue

### 3.3 Real-time Constraint Validation

**Critical timing requirements:**
- LVGL: 16ms max per frame (60fps)
- Audio: 1ms max latency
- Network: 100ms max update cycle
- UI responsiveness: <50ms

**Measurement approach:**
```cpp
// In critical sections
PrecisionTimer timer("critical_section", 1000);
// ... critical code ...
timer.checkpoint("halfway");
// ... more critical code ...
// Destructor logs if >1ms
```

## Phase 4: Advanced Debugging Techniques

### 4.1 Task Switch Tracing
```gdb
(gdb) monitor esp32 semihosting enable
(gdb) monitor esp32 xtensa timer enable
(gdb) set logging on
(gdb) continue
```

**Analyze for:**
- Excessive context switches
- Priority inversions
- Task starvation patterns

### 4.2 Interrupt Latency Analysis
```cpp
TaskProfiler::measureInterruptLatency();
```

**Check for:**
- Display interrupt response time
- WiFi interrupt processing
- Timer interrupt jitter

### 4.3 Dynamic Priority Adjustment Testing
```cpp
// Test adaptive priority system
setTaskSystemState(TASK_STATE_HIGH_LOAD);
vTaskDelay(pdMS_TO_TICKS(5000));
TaskProfiler::printDetailedTaskStats();

setTaskSystemState(TASK_STATE_OTA_ACTIVE);
vTaskDelay(pdMS_TO_TICKS(5000));
TaskProfiler::printDetailedTaskStats();
```

## Phase 5: Optimization & Verification

### 5.1 Immediate Performance Improvements

Based on analysis results, implement targeted fixes:

1. **Core Load Rebalancing**
   - Move lighter tasks to Core 1
   - Reduce LVGL task priority during non-UI operations

2. **Mutex Optimization**
   - Reduce LVGL mutex scope
   - Implement lock-free communication where possible

3. **Memory Management**
   - Pre-allocate critical buffers
   - Implement custom allocators for frequent operations

### 5.2 Verification Testing
```cpp
// Performance regression testing
PerformanceCounters::init();
TaskProfiler::startContinuousMonitoring();

// Run for extended period
vTaskDelay(pdMS_TO_TICKS(300000));  // 5 minutes

PerformanceCounters::printCounters();
TaskProfiler::stopContinuousMonitoring();
```

### 5.3 Software Performance Validation
After optimizations, verify with software metrics:
- PERF warnings for LVGL should be <16ms consistently
- Mutex contention analysis should show minimal blocking
- Task CPU usage should show balanced load distribution

## Troubleshooting Common Issues

### Issue: High LVGL Mutex Contention
**Symptoms:** Frequent PERF warnings for mutex lock >1ms, UI freezes
**Solution:** 
- Reduce mutex scope in audio task
- Batch UI updates to reduce lock frequency

### Issue: Core 0 Overload
**Symptoms:** Task stats show Core 0 >80% CPU usage, task starvation warnings
**Solution:**
- Move messaging task to Core 1
- Reduce audio task update frequency

### Issue: Memory Fragmentation
**Symptoms:** Allocation failures, performance degradation
**Solution:**
- Implement memory pools for frequent allocations
- Increase heap size in `sdkconfig`

### Issue: Network Task Starvation
**Symptoms:** Network task shows low CPU usage, connection drops
**Solution:**
- Increase network task priority during WiFi operations
- Reduce other task frequencies when network is critical

## Performance Targets

After optimization, target these metrics:
- **LVGL Task**: <10ms average processing time
- **Mutex Contention**: <1% of total time
- **Core Load Balance**: Core 0 <75%, Core 1 >25%
- **Memory Fragmentation**: <50%
- **Frame Rate**: Stable 60fps
- **UI Responsiveness**: <50ms response time

## Continuous Monitoring

Implement in production:
```cpp
// Lightweight performance monitoring
if (millis() % 60000 == 0) {  // Every minute
    TaskProfiler::printCPUUsageStats();
    MemoryProfiler::printHeapFragmentation();
}
```

This systematic approach will identify and resolve multithreaded performance issues while maintaining real-time system requirements.
