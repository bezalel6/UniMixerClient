# Advanced Hash Utilities Guide

## Overview

The hash system has been completely redesigned with advanced macros for better performance, cleaner code, and powerful utilities. The old `hasChanged()` method has been removed in favor of more efficient hash comparison patterns.

## Key Features

### 1. Smart Type Handling
The `hashMember()` function now handles:
- Hashable objects (calls their `hash()` method)
- Null pointer safety (returns 0 for null pointers)
- Pointer dereferencing (automatically dereferences valid pointers)
- C-style strings with null safety
- Primitive types

### 2. Advanced Macros

#### Basic Usage
```cpp
struct MyData : public Hashable {
    int value1;
    float value2;
    char name[64];
    
    IMPLEMENT_HASH(value1, value2, name);
};
```

#### Change Detection
```cpp
// Method 1: Using HASH_CHANGED macro
static uint32_t lastHash = 0;
if (HASH_CHANGED(myObject, lastHash)) {
    // Hash changed, update UI
    updateDisplay();
}

// Method 2: Using DEFINE_HASH_TRACKER macro
DEFINE_HASH_TRACKER(myObject, myObject);
if (myObject_changed) {
    // Hash changed
    doSomething();
}

// Method 3: Using UPDATE_IF_HASH_CHANGED macro
UPDATE_IF_HASH_CHANGED(myObject, lastHash, {
    updateUI();
    sendNotification();
});
```

#### Batch Setting
```cpp
// Set multiple fields efficiently - only invalidates once
BATCH_SET_BEGIN();
BATCH_SET(obj.field1, newValue1);
BATCH_SET(obj.field2, newValue2);
BATCH_SET(obj.field3, newValue3);
BATCH_SET_END();
```

### 3. Utility Functions

#### Hash Equality
```cpp
if (hashEquals(object1, object2)) {
    // Objects have same hash
}
```

#### Container Hashing
```cpp
std::vector<int> numbers = {1, 2, 3, 4};
uint32_t containerHash = hashContainer(numbers);
```

## Migration from Old System

### Before (removed)
```cpp
// This pattern is no longer supported
if (cardInfo.hasChanged()) {
    updateUI();
}
```

### After (current)
```cpp
// Efficient hash comparison
DEFINE_HASH_TRACKER(cardInfo, cardInfo);
if (cardInfo_changed) {
    updateUI();
}
```

## Real-World Example

Here's how the SDCardInfo uses the new system:

```cpp
typedef struct : public Hashable {
    sdcard_type_t cardType;
    uint64_t cardSize;
    uint64_t totalBytes;
    uint64_t usedBytes;
    unsigned long lastActivity;
    SDStatus status;
    SDStateFlags stateFlags;
    unsigned long lastMountAttempt;

    // Automatically implements hash() and provides set() method
    IMPLEMENT_HASH(cardType, cardSize, totalBytes, usedBytes, 
                   status, stateFlags, lastMountAttempt);

    // Safe field updates that automatically invalidate hash
    void updateSize(uint64_t newSize) {
        set(cardSize, newSize);
    }
} SDCardInfo;

// Usage in TaskManager
void networkTask(void *parameter) {
    while (tasksRunning) {
        Hardware::SD::SDCardInfo cardInfo = Hardware::SD::getCardInfo();
        
        // Efficient change detection using macro
        DEFINE_HASH_TRACKER(cardInfo, cardInfo);
        
        if (cardInfo_changed) {
            ESP_LOGI(TAG, "Card Info Changed");
            LVGLMessageHandler::updateSDStatus(/* ... */);
        }
        
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(500));
    }
}
```

## Performance Benefits

1. **Eliminated False Positives**: No more spurious "changed" notifications
2. **Efficient Caching**: Hash computed once and cached until invalidated
3. **Smart Invalidation**: Only recomputes when fields actually change
4. **Null Safety**: Handles null pointers gracefully
5. **Memory Efficient**: Single hash value per object instead of separate tracking

## Best Practices

1. Use `DEFINE_HASH_TRACKER` for simple change detection
2. Use `UPDATE_IF_HASH_CHANGED` for conditional actions
3. Use `BATCH_SET_*` macros when updating multiple fields
4. Always use `set()` method for field updates to ensure hash invalidation
5. Avoid direct field assignment unless you know the hash doesn't need updating

## Macro Reference

| Macro | Purpose | Usage |
|-------|---------|-------|
| `IMPLEMENT_HASH(...)` | Basic hash implementation | In struct/class definition |
| `HASHABLE_STRUCT(name, ...)` | Complete hashable struct | Standalone struct definition |
| `HASH_CHANGED(obj, lastHash)` | Check if hash changed | Returns bool, updates lastHash |
| `DEFINE_HASH_TRACKER(name, obj)` | Create hash tracker | Creates `name_changed` variable |
| `UPDATE_IF_HASH_CHANGED(obj, lastHash, action)` | Conditional execution | Executes action if hash changed |
| `BATCH_SET_BEGIN/SET/END` | Efficient multi-field updates | Between begin/end blocks |
| `IS(state, flag)` | Safe flag checking | Replaces manual bit operations |

The system is now more robust, efficient, and provides powerful utilities for hash-based change detection throughout the application. 
