#ifndef LOGO_MANAGER_H
#define LOGO_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <re.h>  // tiny-regex-c
#include <map>

namespace Application {
namespace LogoAssets {

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

// Directory paths
static const char* LOGOS_ROOT_DIR = "/logos";
static const char* LOGOS_BINARY_DIR = "/logos/binaries";
static const char* LOGOS_METADATA_DIR = "/logos/metadata";
static const char* LOGOS_MAPPINGS_DIR = "/logos/mappings";
static const char* LOGOS_CACHE_DIR = "/logos/cache";

// File extensions
static const char* LOGO_BINARY_EXT = ".bin";
static const char* METADATA_EXT = ".json";

// Limits
static const size_t MAX_LOGO_SIZE = 512 * 1024;  // 512KB max logo size
static const size_t MAX_PROCESS_NAME_LENGTH = 63;
static const size_t MAX_PATTERN_LENGTH = 64;
static const size_t MAX_PATTERNS_PER_LOGO = 8;
static const uint32_t METADATA_VERSION = 1;

// Fuzzy matching configuration
static const uint8_t MIN_MATCH_CONFIDENCE = 70;       // Minimum confidence for auto-match
static const uint8_t HIGH_CONFIDENCE_THRESHOLD = 90;  // High confidence threshold

// =============================================================================
// DATA STRUCTURES
// =============================================================================

// Logo metadata structure with fuzzy matching support
typedef struct {
    char processName[64];  // Original/canonical process name
    char patterns[256];    // Regex patterns for fuzzy matching (comma-separated)
    uint32_t fileSize;
    uint16_t width;
    uint16_t height;
    char format[16];    // "lvgl_bin", "lvgl_indexed", etc.
    char checksum[33];  // MD5 hex string for integrity
    uint64_t createdTimestamp;
    uint64_t modifiedTimestamp;
    struct {
        bool incorrect;         // User flagged as incorrect match
        bool verified;          // User verified as correct match
        bool custom;            // User uploaded custom logo
        bool autoDetected;      // Automatically detected/downloaded
        bool manualAssignment;  // User manually assigned this logo
    } userFlags;
    uint8_t matchConfidence;  // 0-100 confidence score for auto-matches
    uint8_t version;          // Metadata format version
} LogoMetadata;

// Fuzzy match result
typedef struct {
    bool found;
    char matchedPattern[64];
    char canonicalName[64];
    uint8_t confidence;  // Match confidence 0-100
    LogoMetadata metadata;
} FuzzyMatchResult;

// Logo load result structure
typedef struct {
    bool success;
    uint8_t* data;
    size_t size;
    LogoMetadata metadata;
    FuzzyMatchResult fuzzyMatch;  // If found via fuzzy matching
    char errorMessage[64];
} LogoLoadResult;

// Logo save result structure
typedef struct {
    bool success;
    size_t bytesWritten;
    char errorMessage[64];
} LogoSaveResult;

// Logo metadata result structure
typedef struct {
    bool success;
    LogoMetadata metadata;
    char errorMessage[64];
} LogoMetadataResult;

// =============================================================================
// LOGO MANAGER CLASS
// =============================================================================

class LogoManager {
   public:
    // Singleton access
    static LogoManager& getInstance();

    // === LIFECYCLE ===
    bool init();
    void deinit();
    bool isInitialized() const { return initialized; }

    // === CORE LOGO OPERATIONS ===

    // Exact matching (original functionality)
    bool logoExists(const char* processName);
    LogoLoadResult loadLogo(const char* processName);

    // Asynchronous logo loading with automatic supplier requests
    typedef std::function<void(const LogoLoadResult& result)> LogoLoadCallback;
    bool loadLogoAsync(const char* processName, LogoLoadCallback callback);

    // === FUZZY MATCHING OPERATIONS ===

    // Smart logo lookup with fuzzy matching
    FuzzyMatchResult findLogoFuzzy(const char* processName);
    LogoLoadResult loadLogoFuzzy(const char* processName);

    // Check if process name matches any known patterns
    bool hasMatchingPattern(const char* processName);

    // === USER CUSTOMIZATION ===

    // Manual logo assignment
    LogoSaveResult assignLogo(const char* processName, const char* sourceLogoName);
    LogoSaveResult saveLogo(const char* processName, const uint8_t* data,
                            size_t size, const LogoMetadata& metadata);

    // User feedback operations
    bool flagLogoIncorrect(const char* processName, bool incorrect = true);
    bool markLogoVerified(const char* processName, bool verified = true);
    bool setManualAssignment(const char* processName, const char* targetLogo);

    // === PATTERN MANAGEMENT ===

    // Add/update fuzzy matching patterns for a logo
    bool addMatchingPattern(const char* canonicalName, const char* pattern);
    bool removeMatchingPattern(const char* canonicalName, const char* pattern);
    bool updateMatchingPatterns(const char* canonicalName, const char* patterns);

    // === UTILITY OPERATIONS ===

    bool deleteLogo(const char* processName);
    LogoMetadataResult getLogoMetadata(const char* processName);
    bool updateLogoMetadata(const char* processName, const LogoMetadata& metadata);
    bool listLogos(std::function<void(const char* processName, const LogoMetadata& metadata)> callback);
    bool validateLogoIntegrity(const char* processName);
    size_t getTotalStorageUsed();
    bool cleanupInvalidLogos();

    // === LOGO SUPPLIER INTEGRATION ===

    // Enable/disable automatic logo requests via suppliers
    void enableAutoRequests(bool enabled = true) { autoRequestEnabled = enabled; }
    bool isAutoRequestEnabled() const { return autoRequestEnabled; }

    // Set callback for logo request notifications (optional)
    void setLogoRequestCallback(std::function<void(const char* processName, bool success, const char* error)> callback);

   private:
    LogoManager() = default;
    ~LogoManager() = default;
    LogoManager(const LogoManager&) = delete;
    LogoManager& operator=(const LogoManager&) = delete;

    // Internal state
    bool initialized = false;
    SemaphoreHandle_t logoOperationMutex = nullptr;

    // === FUZZY MATCHING HELPERS ===

    FuzzyMatchResult performFuzzyMatch(const char* processName);
    bool compileAndTestPattern(const char* pattern, const char* testString);
    uint8_t calculateMatchConfidence(const char* processName, const char* pattern);
    void preprocessProcessName(const char* input, char* output, size_t outputSize);

    // === FILE SYSTEM HELPERS ===

    String getLogoPath(const char* processName);
    String getMetadataPath(const char* processName);
    String getMappingsPath();
    bool ensureDirectoryStructure();

    // === METADATA OPERATIONS ===

    bool saveMetadataFile(const char* processName, const LogoMetadata& metadata);
    LogoMetadataResult loadMetadataFile(const char* processName);
    bool loadUserMappings();
    bool saveUserMappings();

    // === UTILITY HELPERS ===

    String calculateChecksum(const uint8_t* data, size_t size);
    bool copyLogoFile(const char* sourceName, const char* destName);

    // === LOGO SUPPLIER HELPERS ===

    void onAssetReceived(const char* processName, const uint8_t* data, size_t size,
                         const LogoMetadata& metadata, bool success, const char* error);
    bool requestLogoFromSupplier(const char* processName);

    // === RESULT CREATION HELPERS ===

    LogoLoadResult createLoadResult(bool success, uint8_t* data = nullptr,
                                    size_t size = 0, const char* error = nullptr);
    LogoSaveResult createSaveResult(bool success, size_t bytes = 0,
                                    const char* error = nullptr);
    LogoMetadataResult createMetadataResult(bool success, const LogoMetadata& metadata = {},
                                            const char* error = nullptr);
    FuzzyMatchResult createFuzzyResult(bool found, const char* pattern = nullptr,
                                       const char* canonical = nullptr, uint8_t confidence = 0);

    // Internal storage for user mappings
    JsonDocument* userMappings = nullptr;

    // Logo supplier integration
    bool autoRequestEnabled = true;
    std::function<void(const char*, bool, const char*)> logoRequestCallback = nullptr;

    // Async request tracking
    struct AsyncRequest {
        std::vector<LogoLoadCallback> callbacks;
        unsigned long requestTime;
        bool inProgress;
    };
    std::map<String, AsyncRequest> pendingAsyncRequests;
};

}  // namespace LogoAssets
}  // namespace Application

#endif  // LOGO_MANAGER_H
