/*
 * LogoManager Usage Examples
 *
 * This file demonstrates how to use the LogoManager for logo asset storage
 * with fuzzy matching capabilities. These examples show the key functionality
 * including exact matching, fuzzy searching, user customization, and pattern management.
 *
 * Note: This is an example file - not compiled into the main application.
 * Use these patterns in your actual application code.
 */

#include "LogoManager.h"
#include <esp_log.h>

using namespace Application::LogoAssets;

static const char* TAG = "LogoExample";

// Example: Basic logo operations
void basicLogoOperations() {
    LogoManager& logoManager = LogoManager::getInstance();

    // Check if a logo exists for exact process name
    if (logoManager.logoExists("chrome.exe")) {
        ESP_LOGI(TAG, "Chrome logo found!");

        // Load the logo
        LogoLoadResult result = logoManager.loadLogo("chrome.exe");
        if (result.success) {
            ESP_LOGI(TAG, "Chrome logo loaded: %zu bytes, %dx%d",
                     result.size, result.metadata.width, result.metadata.height);

            // Use result.data with LVGL for display
            // ...

            // Always free the loaded data when done
            free(result.data);
        }
    }
}

// Example: Fuzzy matching for process variations
void fuzzyMatchingExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    // Test various Chrome process name variations
    const char* chromeVariations[] = {
        "chrome_browser.exe",
        "google-chrome.exe",
        "chromium",
        "Chrome.exe",
        "chrome_proxy",
        nullptr};

    for (int i = 0; chromeVariations[i]; i++) {
        ESP_LOGI(TAG, "Testing fuzzy match for: %s", chromeVariations[i]);

        FuzzyMatchResult fuzzyResult = logoManager.findLogoFuzzy(chromeVariations[i]);
        if (fuzzyResult.found) {
            ESP_LOGI(TAG, "  ✓ Found match: %s -> %s (confidence: %d%%)",
                     chromeVariations[i], fuzzyResult.canonicalName, fuzzyResult.confidence);

            // Check if user flagged this as incorrect
            if (fuzzyResult.metadata.userFlags.incorrect) {
                ESP_LOGW(TAG, "  ⚠ User flagged this match as incorrect");
                // Maybe show default icon or ask user for correction
            }
        } else {
            ESP_LOGI(TAG, "  ✗ No fuzzy match found");
        }
    }
}

// Example: Loading logos with smart fuzzy fallback
void smartLogoLoading() {
    LogoManager& logoManager = LogoManager::getInstance();

    const char* processName = "firefox_development.exe";

    // Try loading with automatic fuzzy fallback
    LogoLoadResult result = logoManager.loadLogoFuzzy(processName);
    if (result.success) {
        if (result.fuzzyMatch.found) {
            ESP_LOGI(TAG, "Loaded via fuzzy match: %s -> %s (confidence: %d%%)",
                     processName, result.fuzzyMatch.canonicalName, result.fuzzyMatch.confidence);
        } else {
            ESP_LOGI(TAG, "Loaded exact match for: %s", processName);
        }

        // Use logo data...
        free(result.data);
    } else {
        ESP_LOGI(TAG, "No logo found for: %s", processName);
        // Use default icon
    }
}

// Example: User customization and feedback
void userCustomizationExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    // User manually assigns a logo
    const char* processName = "my_custom_app.exe";
    const char* sourceLogo = "chrome.exe";

    LogoSaveResult assignResult = logoManager.assignLogo(processName, sourceLogo);
    if (assignResult.success) {
        ESP_LOGI(TAG, "Successfully assigned logo: %s -> %s", processName, sourceLogo);
    }

    // User flags an incorrect match
    logoManager.flagLogoIncorrect("wrong_match.exe", true);
    ESP_LOGI(TAG, "Flagged logo as incorrect for wrong_match.exe");

    // User verifies a correct match
    logoManager.markLogoVerified("correct_match.exe", true);
    ESP_LOGI(TAG, "Verified logo as correct for correct_match.exe");

    // Set manual assignment for future use
    logoManager.setManualAssignment("special_process.exe", "firefox.exe");
    ESP_LOGI(TAG, "Set manual assignment: special_process.exe -> firefox.exe");
}

// Example: Managing fuzzy matching patterns
void patternManagementExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    const char* canonicalName = "vscode.exe";

    // Add new pattern for better matching
    if (logoManager.addMatchingPattern(canonicalName, "visual.*studio.*code")) {
        ESP_LOGI(TAG, "Added new pattern for VS Code");
    }

    // Update all patterns at once
    const char* newPatterns = "code|code\\.exe|vscode|visual.*studio.*code|vs.*code|code.*insider";
    if (logoManager.updateMatchingPatterns(canonicalName, newPatterns)) {
        ESP_LOGI(TAG, "Updated patterns for VS Code: %s", newPatterns);
    }

    // Remove a specific pattern
    if (logoManager.removeMatchingPattern(canonicalName, "old_pattern")) {
        ESP_LOGI(TAG, "Removed old pattern from VS Code");
    }
}

// Example: Saving custom logos
void saveCustomLogoExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    // Example logo data (in practice, this would be your LVGL binary data)
    const uint8_t exampleLogoData[] = {0x01, 0x02, 0x03, 0x04};  // Placeholder

    // Create metadata for the logo
    LogoMetadata metadata = {};
    strncpy(metadata.processName, "myapp.exe", sizeof(metadata.processName) - 1);
    strncpy(metadata.format, "lvgl_bin", sizeof(metadata.format) - 1);
    metadata.width = 64;
    metadata.height = 64;
    metadata.userFlags.custom = true;
    metadata.createdTimestamp = millis();
    metadata.version = 1;

    // Save the logo
    LogoSaveResult result = logoManager.saveLogo("myapp.exe", exampleLogoData,
                                                 sizeof(exampleLogoData), metadata);
    if (result.success) {
        ESP_LOGI(TAG, "Custom logo saved successfully: %zu bytes written", result.bytesWritten);
    } else {
        ESP_LOGE(TAG, "Failed to save custom logo: %s", result.errorMessage);
    }
}

// Example: Listing and managing stored logos
void logoManagementExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    ESP_LOGI(TAG, "=== Stored Logos ===");

    // List all stored logos
    logoManager.listLogos([](const char* processName, const LogoMetadata& metadata) {
        ESP_LOGI(TAG, "Logo: %s", processName);
        ESP_LOGI(TAG, "  Size: %u bytes (%dx%d)", metadata.fileSize, metadata.width, metadata.height);
        ESP_LOGI(TAG, "  Format: %s", metadata.format);
        ESP_LOGI(TAG, "  Patterns: %s", metadata.patterns);
        ESP_LOGI(TAG, "  Flags: %s%s%s%s%s",
                 metadata.userFlags.custom ? "Custom " : "",
                 metadata.userFlags.verified ? "Verified " : "",
                 metadata.userFlags.incorrect ? "Incorrect " : "",
                 metadata.userFlags.manualAssignment ? "Manual " : "",
                 metadata.userFlags.autoDetected ? "Auto " : "");
    });

    // Get total storage usage
    size_t totalSize = logoManager.getTotalStorageUsed();
    ESP_LOGI(TAG, "Total logo storage used: %zu bytes", totalSize);

    // Validate logo integrity
    if (!logoManager.validateLogoIntegrity("chrome.exe")) {
        ESP_LOGW(TAG, "Chrome logo failed integrity check - may be corrupted");
    }

    // Clean up invalid/corrupted logos
    if (logoManager.cleanupInvalidLogos()) {
        ESP_LOGI(TAG, "Logo cleanup completed successfully");
    }
}

// Example: Integration with audio system
void audioSystemIntegrationExample() {
    LogoManager& logoManager = LogoManager::getInstance();

    // Simulate getting process names from audio system
    const char* audioProcesses[] = {
        "Spotify.exe",
        "chrome.exe",
        "Discord.exe",
        "steam.exe",
        "VirtualDJ.exe",
        nullptr};

    ESP_LOGI(TAG, "=== Audio Process Logo Lookup ===");

    for (int i = 0; audioProcesses[i]; i++) {
        const char* processName = audioProcesses[i];

        // Try to find logo with fuzzy matching
        if (logoManager.hasMatchingPattern(processName)) {
            FuzzyMatchResult fuzzyResult = logoManager.findLogoFuzzy(processName);
            if (fuzzyResult.found && fuzzyResult.confidence >= 80) {
                ESP_LOGI(TAG, "High confidence match for %s: %s (%d%%)",
                         processName, fuzzyResult.canonicalName, fuzzyResult.confidence);

                // Load and use the logo for the audio interface
                LogoLoadResult logoResult = logoManager.loadLogo(fuzzyResult.canonicalName);
                if (logoResult.success) {
                    // Display in LVGL audio interface
                    // displayLogoInAudioUI(logoResult.data, logoResult.size);
                    free(logoResult.data);
                }
            } else if (fuzzyResult.found) {
                ESP_LOGW(TAG, "Low confidence match for %s: %s (%d%%) - may need user verification",
                         processName, fuzzyResult.canonicalName, fuzzyResult.confidence);
            }
        } else {
            ESP_LOGI(TAG, "No logo available for %s - using default icon", processName);
        }
    }
}

// Main example function (not called in actual application)
void runLogoManagerExamples() {
    ESP_LOGI(TAG, "=== LogoManager Examples ===");

    // Ensure LogoManager is initialized
    LogoManager& logoManager = LogoManager::getInstance();
    if (!logoManager.isInitialized()) {
        ESP_LOGE(TAG, "LogoManager not initialized!");
        return;
    }

    // Run examples
    basicLogoOperations();
    fuzzyMatchingExample();
    smartLogoLoading();
    userCustomizationExample();
    patternManagementExample();
    saveCustomLogoExample();
    logoManagementExample();
    audioSystemIntegrationExample();

    ESP_LOGI(TAG, "=== Examples Complete ===");
}
