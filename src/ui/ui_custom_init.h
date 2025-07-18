#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Custom UI initialization
 * 
 * This module provides initialization for custom UI components
 * that are added to the SquareLine Studio generated UI.
 */

/**
 * Initialize custom UI components after SquareLine Studio UI is created
 * This function should be called after ui_init() to add custom components
 */
void ui_custom_init();

/**
 * Cleanup custom UI components
 * This function should be called before ui_destroy()
 */
void ui_custom_cleanup();

#ifdef __cplusplus
}
#endif