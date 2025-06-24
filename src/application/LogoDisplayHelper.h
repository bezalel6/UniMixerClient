#ifndef LOGO_DISPLAY_HELPER_H
#define LOGO_DISPLAY_HELPER_H

#include "LogoManager.h"
#include <lvgl.h>
#include <functional>

namespace Application {
namespace LogoAssets {

// =============================================================================
// LOGO DISPLAY HELPER
// =============================================================================

/**
 * Helper class for displaying logos alongside audio devices in the UI
 * Handles async logo loading and fallback to default icons
 */
class LogoDisplayHelper {
   public:
    // Logo display callback type
    typedef std::function<void(lv_obj_t* container, const uint8_t* logoData, size_t logoSize, bool success)> LogoDisplayCallback;

    // Logo request result
    struct LogoDisplayResult {
        bool success;
        const uint8_t* logoData;
        size_t logoSize;
        LogoMetadata metadata;
        bool wasAsync;  // true if logo was loaded asynchronously
    };

    // === STATIC HELPER METHODS ===

    /**
     * Create a logo image widget for a process
     *
     * @param parent Parent container
     * @param processName Process name to load logo for
     * @param callback Optional callback when logo loads (for async requests)
     * @return LVGL image object (may show default icon initially)
     */
    static lv_obj_t* createLogoImage(lv_obj_t* parent, const char* processName,
                                     LogoDisplayCallback callback = nullptr);

    /**
     * Update an existing logo image with async loading
     *
     * @param logoImage Existing LVGL image object
     * @param processName Process name to load logo for
     * @param callback Optional callback when logo loads
     * @return true if logo was loaded immediately, false if async request was made
     */
    static bool updateLogoImage(lv_obj_t* logoImage, const char* processName,
                                LogoDisplayCallback callback = nullptr);

    /**
     * Load logo synchronously (immediate result)
     *
     * @param processName Process name to load logo for
     * @return LogoDisplayResult with immediate logo data or default icon info
     */
    static LogoDisplayResult loadLogoSync(const char* processName);

    /**
     * Load logo asynchronously
     *
     * @param processName Process name to load logo for
     * @param callback Callback to invoke when logo is available
     * @return true if request was submitted successfully
     */
    static bool loadLogoAsync(const char* processName, LogoDisplayCallback callback);

    /**
     * Create a default icon for a process (when no logo is available)
     *
     * @param parent Parent container
     * @param processName Process name (used for icon selection)
     * @return LVGL label with appropriate icon
     */
    static lv_obj_t* createDefaultIcon(lv_obj_t* parent, const char* processName);

    /**
     * Get appropriate default icon for a process
     *
     * @param processName Process name
     * @return LVGL symbol string
     */
    static const char* getDefaultIconForProcess(const char* processName);

    /**
     * Set logo image data to an LVGL image object
     *
     * @param logoImage LVGL image object
     * @param logoData Logo binary data
     * @param logoSize Logo data size
     * @return true if logo was set successfully
     */
    static bool setLogoImageData(lv_obj_t* logoImage, const uint8_t* logoData, size_t logoSize);

    /**
     * Create a composite widget with logo and process name
     *
     * @param parent Parent container
     * @param processName Process name
     * @param displayName Display name (optional, uses processName if null)
     * @param callback Optional callback for async logo loading
     * @return Container with logo and label
     */
    static lv_obj_t* createProcessWidget(lv_obj_t* parent, const char* processName,
                                         const char* displayName = nullptr,
                                         LogoDisplayCallback callback = nullptr);

   private:
    // Internal callback wrapper for async requests
    struct AsyncCallbackWrapper {
        lv_obj_t* targetImage;
        LogoDisplayCallback userCallback;
    };

    static void onAsyncLogoLoaded(const LogoLoadResult& result);
    static void cleanupLogoData(const uint8_t* logoData);
};

}  // namespace LogoAssets
}  // namespace Application

#endif  // LOGO_DISPLAY_HELPER_H
