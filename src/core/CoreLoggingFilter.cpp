#include "CoreLoggingFilter.h"

// Static member definitions
bool CoreLoggingFilter::initialized_ = false;
bool CoreLoggingFilter::filterActive_ = true;
vprintf_like_t CoreLoggingFilter::originalVprintf_ = nullptr;
uint32_t CoreLoggingFilter::core0FilteredCount_ = 0;
uint32_t CoreLoggingFilter::core1AllowedCount_ = 0;

bool CoreLoggingFilter::init() {
    if (initialized_) {
        return true;
    }

    // Store the original vprintf function before hooking
    originalVprintf_ = esp_log_set_vprintf(coreFilterVprintf);

    if (!originalVprintf_) {
        // If esp_log_set_vprintf returns nullptr, it means we're the first to hook
        // The default vprintf is being used, so we need to get it manually
        originalVprintf_ = vprintf;
    }

    initialized_ = true;
    filterActive_ = true;

    // Use printf directly since ESP_LOG might not work yet during early init
    printf("[CoreLoggingFilter] Core 1-only logging filter initialized\n");
    printf("[CoreLoggingFilter] Core 0 logs will be filtered out\n");
    printf("[CoreLoggingFilter] Core 1 logs will be displayed normally\n");

    return true;
}

void CoreLoggingFilter::disableFilter() {
    if (!initialized_) return;

    filterActive_ = false;
    printf("[CoreLoggingFilter] Logging filter DISABLED - all cores can log\n");
}

void CoreLoggingFilter::enableFilter() {
    if (!initialized_) return;

    filterActive_ = true;
    printf("[CoreLoggingFilter] Logging filter ENABLED - Core 1 only\n");
}

void CoreLoggingFilter::getStats(uint32_t& core0Filtered, uint32_t& core1Allowed) {
    core0Filtered = core0FilteredCount_;
    core1Allowed = core1AllowedCount_;
}

int CoreLoggingFilter::coreFilterVprintf(const char* format, va_list args) {
    if (!initialized_ || !originalVprintf_) {
        // Fallback to standard vprintf if not properly initialized
        return vprintf(format, args);
    }

    // Check which core is calling
    BaseType_t coreId = xPortGetCoreID();

    if (!filterActive_) {
        // Filter disabled - allow all cores to log
        return originalVprintf_(format, args);
    }

    if (coreId == 1) {
        // Core 1 - allow logging
        core1AllowedCount_++;
        return originalVprintf_(format, args);
    } else {
        // Core 0 - filter out logging
        core0FilteredCount_++;

        // Optionally, you can write filtered logs to a different output
        // For now, we'll just silently drop them

        // Return a positive value to indicate "successful" write
        // (even though we didn't actually write anything)
        return 1;
    }
}
