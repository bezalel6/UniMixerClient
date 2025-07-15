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
    void updateStatus(const char* status);
    
    // Update progress percentage (0-100)
    void updateProgress(int percentage);
    
    // Hide the boot screen and clean up
    void hide();
    
    // Check if boot screen is currently visible
    bool isVisible();
}

// Convenience macro for updating boot status
#define BOOT_STATUS(msg) BootProgress::updateStatus(msg)