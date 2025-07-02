#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * Core 1-Only Logging Filter for Dual-Core ESP32-S3 Architecture
 *
 * This system restricts ESP_LOG output to only Core 1 to prevent
 * interference between cores and improve performance.
 *
 * Core 0: UI/LVGL operations (silent logging)
 * Core 1: Messaging engine (full logging enabled)
 *
 * Usage:
 *   CoreLoggingFilter::init();  // Call early in main()
 */
class CoreLoggingFilter {
   public:
    /**
     * Initialize Core 1-only logging filter
     * Must be called before any ESP_LOG calls
     */
    static bool init();

    /**
     * Temporarily disable filtering (allow all cores to log)
     * Useful for debugging or emergency situations
     */
    static void disableFilter();

    /**
     * Re-enable Core 1-only filtering
     */
    static void enableFilter();

    /**
     * Check if filtering is currently active
     */
    static bool isFilterActive() { return filterActive_; }

    /**
     * Get statistics about filtered logs
     */
    static void getStats(uint32_t& core0Filtered, uint32_t& core1Allowed);

   private:
    static bool initialized_;
    static bool filterActive_;
    static vprintf_like_t originalVprintf_;
    static uint32_t core0FilteredCount_;
    static uint32_t core1AllowedCount_;

    /**
     * Custom vprintf that filters by core
     */
    static int coreFilterVprintf(const char* format, va_list args);
};
