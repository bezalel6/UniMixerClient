#ifndef LOGO_SUPPLIER_H
#define LOGO_SUPPLIER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include "MessageProtocol.h"

namespace Application {
namespace LogoAssets {

// =============================================================================
// LOGO REQUEST/RESPONSE STRUCTURES
// =============================================================================

// Asset request structure (matches server-side expectations)
typedef struct {
    MessageProtocol::ExternalMessageType messageType = MessageProtocol::ExternalMessageType::GET_ASSETS;
    String requestId;
    String deviceId;
    String processName;
    uint64_t timestamp;
} AssetRequest;

// Asset response structure (simplified for LVGL binary system)
typedef struct {
    MessageProtocol::ExternalMessageType messageType = MessageProtocol::ExternalMessageType::ASSET_RESPONSE;
    String requestId;
    String deviceId;
    String processName;
    uint8_t* assetData;    // LVGL binary data
    size_t assetDataSize;  // Size of binary data
    bool success;
    String errorMessage;
    uint64_t timestamp;
    bool hasAssetData;

    // Simplified metadata fields (no complex structure needed)
    uint16_t width = 0;
    uint16_t height = 0;
    String format = "bin";  // Always LVGL binary
} AssetResponse;

// Request completion callback type
typedef std::function<void(const AssetResponse& response)> AssetRequestCallback;

// =============================================================================
// LOGO SUPPLIER INTERFACE
// =============================================================================

/**
 * Abstract base class for logo suppliers
 *
 * This allows different sourcing strategies:
 * - Message bus (server requests)
 * - Online APIs
 * - Local databases
 * - Cache systems
 * etc.
 */
class LogoSupplier {
   public:
    virtual ~LogoSupplier() = default;

    /**
     * Initialize the supplier
     */
    virtual bool init() = 0;

    /**
     * Cleanup and shutdown
     */
    virtual void deinit() = 0;

    /**
     * Check if supplier is ready to handle requests
     */
    virtual bool isReady() const = 0;

    /**
     * Request a logo for the specified process
     *
     * @param processName The process name to request a logo for
     * @param callback Callback to invoke when request completes (success or failure)
     * @return true if request was submitted successfully, false otherwise
     */
    virtual bool requestLogo(const char* processName, AssetRequestCallback callback) = 0;

    /**
     * Update/process pending requests (call from main loop)
     */
    virtual void update() = 0;

    /**
     * Get supplier status information
     */
    virtual String getStatus() const = 0;

    /**
     * Get supplier type name for diagnostics
     */
    virtual const char* getSupplierType() const = 0;

   protected:
    // Helper methods for derived classes
    static AssetRequest createAssetRequest(const char* processName);
    static AssetResponse createAssetResponse(bool success, const char* processName,
                                             const char* requestId = nullptr,
                                             const char* errorMessage = nullptr);
};

// =============================================================================
// LOGO SUPPLIER MANAGER
// =============================================================================

/**
 * Manages logo suppliers and coordinates requests
 */
class LogoSupplierManager {
   public:
    static LogoSupplierManager& getInstance();

    // Lifecycle
    bool init();
    void deinit();
    void update();
    bool isInitialized() const { return initialized; }

    // Supplier management
    bool registerSupplier(LogoSupplier* supplier, int priority = 0);
    void unregisterSupplier(LogoSupplier* supplier);
    size_t getSupplierCount() const;

    // Logo requests
    bool requestLogo(const char* processName, AssetRequestCallback callback);
    String getStatus() const;

   private:
    LogoSupplierManager() = default;
    ~LogoSupplierManager() = default;
    LogoSupplierManager(const LogoSupplierManager&) = delete;
    LogoSupplierManager& operator=(const LogoSupplierManager&) = delete;

    struct SupplierEntry {
        LogoSupplier* supplier;
        int priority;
        bool enabled;
    };

    bool initialized = false;
    std::vector<SupplierEntry> suppliers;
    SemaphoreHandle_t supplierMutex = nullptr;

    // Internal helpers
    void sortSuppliersByPriority();
};

}  // namespace LogoAssets
}  // namespace Application

#endif  // LOGO_SUPPLIER_H
