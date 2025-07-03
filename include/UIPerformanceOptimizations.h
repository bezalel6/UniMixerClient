#pragma once

/**
 * UI Performance Optimizations
 *
 * CRITICAL PERFORMANCE FIX: The 3.8MB ui_img_2039083_png image is causing
 * 100-225ms LVGL processing delays. This header provides emergency fixes
 * and optimizations that can be applied without modifying auto-generated UI code.
 */

#include <ui/ui.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// EMERGENCY PERFORMANCE FIX: Image optimization
void ui_performance_optimize_large_images(void);

// Hide the problematic 3.8MB image that's causing performance issues
void ui_performance_hide_large_image(void);

// Replace large image with optimized placeholder
void ui_performance_replace_large_image_with_placeholder(void);

// Optimize UI object properties for performance
void ui_performance_optimize_ui_objects(void);

// Apply all performance optimizations
void ui_performance_apply_all_optimizations(void);

// Performance monitoring
void ui_performance_log_memory_usage(void);

#ifdef __cplusplus
}
#endif
