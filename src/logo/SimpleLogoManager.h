#pragma once

#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include <Arduino.h>
#include <functional>
#include <unordered_map>

/**
 * SIMPLE LOGO MANAGER
 * 
 * No abstractions. No metadata. No JSON.
 * Just saves processName.png and retrieves it.
 * 
 * Usage:
 *   SimpleLogoManager::getInstance().requestLogo("chrome", [](bool success, uint8_t* data, size_t size, const String& error) {
 *       if (success) {
 *           // Use the PNG data
 *       }
 *   });
 */
class SimpleLogoManager {
public:
    using LogoCallback = std::function<void(bool success, uint8_t* data, size_t size, const String& error)>;

    static SimpleLogoManager& getInstance();
    
    bool init();
    void deinit();
    void update();
    
    // Core operations
    bool requestLogo(const String& processName, LogoCallback callback);
    bool hasLogo(const String& processName);
    bool deleteLogo(const String& processName);
    
    // Get path for LVGL (S:/logos/processName.png)
    String getLVGLPath(const String& processName);
    
    // Status
    String getStatus() const;
    
    // Paging support for logo browser
    bool scanLogosOnce();
    std::vector<String> getPagedLogos(int pageIndex, int itemsPerPage);
    int getTotalLogoCount();
    String getLogoLVGLPath(const String& logoPath);
    void refreshLogoList();
    
    // Filtered search support
    int getFilteredLogoCount(const String& filter);
    std::vector<String> getFilteredPagedLogos(const String& filter, int pageIndex, int itemsPerPage);

private:
    SimpleLogoManager() = default;
    static SimpleLogoManager* instance;
    bool initialized = false;
    
    // Logo browsing cache
    std::vector<String> cachedLogoPaths;
    bool logoListCached = false;
    unsigned long lastScanTime = 0;
    static const unsigned long CACHE_TIMEOUT_MS = 300000; // 5 minutes
    
    // Simple request tracking
    struct LogoRequest {
        String processName;
        String requestId;
        LogoCallback callback;
        unsigned long requestTime;
    };
    std::unordered_map<String, LogoRequest> pendingRequests;
    
    // Stats
    uint32_t requestsSubmitted = 0;
    uint32_t responsesReceived = 0;
    uint32_t requestsTimedOut = 0;
    uint32_t requestsFailed = 0;
    
    static const unsigned long REQUEST_TIMEOUT_MS = 30000;
    static const char* LOGOS_DIR;
    
    void handleAssetResponse(const Messaging::Message& msg);
    String getLogoPath(const String& processName);
    String sanitizeProcessName(const String& processName);
    bool ensureLogosDirectory();
    
    // Base64 decode helper
    static size_t base64Decode(const char* encoded, uint8_t* decoded, size_t maxDecodedSize);
};