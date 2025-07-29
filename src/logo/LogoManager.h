#pragma once

#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include <Arduino.h>
#include <unordered_map>
#include <vector>
#include "re.h"  // tiny-regex-c library

/**
 * LOGO MANAGER
 *
 * Simplified logo management system that prevents server spam.
 * Just tracks process names and request timestamps.
 *
 * Usage:
 *   LogoManager::getInstance().requestLogo("chrome");
 *   if (LogoManager::getInstance().hasLogo("chrome")) {
 *       String path = LogoManager::getInstance().getLVGLPath("chrome");
 *   }
 */
class LogoManager {
   public:
    static LogoManager& getInstance();

    bool init();
    void deinit();

    // Core operations - no callbacks, no complexity
    void requestLogo(const String& processName);
    bool hasLogo(const String& processName);
    String getLVGLPath(const String& processName);

   private:
    LogoManager() = default;
    static LogoManager* instance;
    bool initialized = false;

    // Simple request tracking - just timestamps to prevent spam
    std::unordered_map<String, unsigned long> requestTimestamps;
    
    // Spam prevention constant - retry after 1 minute
    static const unsigned long RETRY_ASSET_REQUEST_MS = 60000;

    // Regex matching support (kept from original)
    struct LogoEntry {
        String filename;
        String lowerFilename;  // For case-insensitive matching
    };
    std::vector<LogoEntry> logoCache;
    std::unordered_map<String, re_t> regexCache;

    // Stats (simplified)
    uint32_t requestsSubmitted = 0;
    uint32_t responsesReceived = 0;

    static const char* LOGOS_DIR;

    // Message handlers
    void handleAssetResponse(const Messaging::Message& msg);

    // Helper methods
    void updateLogoCache();
    String findMatchingLogo(const String& processName);
    String extractProcessCore(const String& processName);
    re_t getCompiledRegex(const String& pattern);
    void clearRegexCache();
    bool ensureLogosDirectory();

    // Base64 decode helper
    static size_t base64Decode(const char* encoded, uint8_t* decoded, size_t maxDecodedSize);
};