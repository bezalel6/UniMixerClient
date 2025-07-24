#pragma once

#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include <Arduino.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>

namespace AssetManagement {

/**
 * Professional Logo Management System
 * 
 * Primary purpose: Map process names to logo files with intelligent matching
 * Secondary purpose: Provide search functionality for logo browsing
 * 
 * Features:
 * - Direct process name to logo mapping
 * - Alias support for common variations
 * - Process name normalization (removes .exe, versions, etc.)
 * - Fuzzy matching for close matches
 * - Efficient caching and lookup
 */
class LogoManager {
public:
    using LogoCallback = std::function<void(bool success, uint8_t* data, size_t size, const String& error)>;

    // Singleton access
    static LogoManager& getInstance();
    
    // Lifecycle
    bool init();
    void deinit();
    void update();
    
    // Primary API - Process to Logo mapping
    String getLogoPath(const String& processName);
    bool hasLogo(const String& processName);
    
    // Logo request management
    bool requestLogo(const String& processName, LogoCallback callback);
    bool deleteLogo(const String& processName);
    
    // Get path for LVGL (S:/logos/processName.png)
    String getLVGLPath(const String& processName);
    
    // Status and statistics
    String getStatus() const;
    
    // Logo browsing support
    bool scanLogosOnce();
    std::vector<String> getPagedLogos(int pageIndex, int itemsPerPage);
    int getTotalLogoCount();
    String getLogoLVGLPath(const String& logoPath);
    void refreshLogoList();
    
    // Search support (on-demand, not reactive)
    struct SearchResult {
        String processName;
        String logoPath;
        float confidence;  // 1.0 = exact match, lower = fuzzy
    };
    std::vector<SearchResult> searchLogos(const String& query, size_t limit = 20);
    
    // Filtered search support
    int getFilteredLogoCount(const String& filter);
    std::vector<String> getFilteredPagedLogos(const String& filter, int pageIndex, int itemsPerPage);

private:
    LogoManager() = default;
    static LogoManager* instance;
    bool initialized = false;
    
    // Logo database entry
    struct LogoEntry {
        String canonicalName;      // Normalized name
        String fileName;           // Actual file name
        std::vector<String> aliases; // Alternative names
    };
    
    // Primary lookup tables
    std::unordered_map<String, LogoEntry> logoDatabase;
    std::unordered_map<String, String> aliasMap; // alias -> canonical name
    
    // Logo browsing cache
    std::vector<String> cachedLogoPaths;
    bool logoListCached = false;
    unsigned long lastScanTime = 0;
    static const unsigned long CACHE_TIMEOUT_MS = 300000; // 5 minutes
    
    // Request tracking
    struct LogoRequest {
        String processName;
        String requestId;
        LogoCallback callback;
        unsigned long requestTime;
    };
    std::unordered_map<String, LogoRequest> pendingRequests;
    
    // Statistics
    uint32_t requestsSubmitted = 0;
    uint32_t responsesReceived = 0;
    uint32_t requestsTimedOut = 0;
    uint32_t requestsFailed = 0;
    
    static const unsigned long REQUEST_TIMEOUT_MS = 30000;
    static const char* LOGOS_DIR;
    
    // Internal methods
    void buildLogoDatabase();
    void loadKnownAliases();
    void loadCustomMappings();
    String normalizeProcessName(const String& processName) const;
    String findLogoForProcess(const String& processName);
    std::optional<LogoEntry> findClosestMatch(const String& normalized);
    int calculateEditDistance(const String& s1, const String& s2) const;
    void handleAssetResponse(const Messaging::Message& msg);
    String getLogoPath(const String& processName);
    String sanitizeProcessName(const String& processName);
    bool ensureLogosDirectory();
    
    // Base64 decode helper
    static size_t base64Decode(const char* encoded, uint8_t* decoded, size_t maxDecodedSize);
    
    // Known aliases database
    void initializeKnownAliases();
};

} // namespace AssetManagement