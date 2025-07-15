#pragma once

/**
 * Boot Progress Screen
 *
 * Shows initialization progress during system startup.
 * Provides visual feedback while components are being initialized.
 */

namespace BootProgress {
// Initialize and show the boot progress screen
bool init();

// Update the current initialization step being performed
void updateStatus(const char *status);

// Update progress percentage (0-100)
void updateProgress(int percentage);

// Hide the boot screen and clean up
void hide();

// Complete boot process - handles both success and BSOD scenarios
void completeBootProcess();

// Force cleanup of boot screen objects (used when BSOD takes over)
void forceCleanup();

// Check if boot screen is currently visible
bool isVisible();
}  // namespace BootProgress

// Convenience macro for updating boot status
#define BOOT_STATUS(msg) BootProgress::updateStatus(msg)

// Convenience macro for completing boot process
#define BOOT_COMPLETE() BootProgress::completeBootProcess()
