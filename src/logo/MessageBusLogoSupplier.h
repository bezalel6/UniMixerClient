#ifndef MESSAGE_BUS_LOGO_SUPPLIER_H
#define MESSAGE_BUS_LOGO_SUPPLIER_H

#include "LogoSupplier.h"
#include "../messaging/Message.h"
#include <map>

namespace Application {
namespace LogoAssets {

// =============================================================================
// MESSAGE BUS LOGO SUPPLIER
// =============================================================================

/**
 * Logo supplier that uses the message bus to request logos from a server
 */
class MessageBusLogoSupplier : public LogoSupplier {
   public:
    static MessageBusLogoSupplier& getInstance();

    // LogoSupplier interface
    bool init() override;
    void deinit() override;
    bool isReady() const override;
    bool requestLogo(const char* processName, AssetRequestCallback callback) override;
    void update() override;
    String getStatus() const override;
    const char* getSupplierType() const override { return "MessageBus"; }

    // Configuration
    void setRequestTimeout(unsigned long timeoutMs) { requestTimeoutMs = timeoutMs; }
    void setMaxConcurrentRequests(size_t maxRequests) { maxConcurrentRequests = maxRequests; }

   private:
    MessageBusLogoSupplier() = default;
    ~MessageBusLogoSupplier() = default;
    MessageBusLogoSupplier(const MessageBusLogoSupplier&) = delete;
    MessageBusLogoSupplier& operator=(const MessageBusLogoSupplier&) = delete;

    // Configuration - message types now defined in Messaging::Config

    // Internal state
    bool initialized = false;
    unsigned long requestTimeoutMs = 30000;  // 30 second timeout
    size_t maxConcurrentRequests = 1;

    // Pending requests tracking
    struct PendingRequest {
        String requestId;
        String processName;
        AssetRequestCallback callback;
        unsigned long requestTime;
        bool expired;
    };

    std::map<String, PendingRequest> pendingRequests;  // requestId -> PendingRequest
    std::vector<PendingRequest> requestQueue;          // Queue for serialized requests
    SemaphoreHandle_t requestMutex = nullptr;

    // Statistics
    unsigned long requestsSubmitted = 0;
    unsigned long responsesReceived = 0;
    unsigned long requestsTimedOut = 0;
    unsigned long requestsFailed = 0;
    
    // Deferred logo save queue (to prevent stack overflow on messaging task)
    std::vector<AssetResponse> deferredSaveQueue;
    SemaphoreHandle_t deferredSaveMutex = nullptr;

    // Internal methods
    void onAssetResponse(const Messaging::Message& msg);
    bool sendAssetRequest(const AssetRequest& request);
    void timeoutExpiredRequests();

    void completeRequest(const String& requestId, const AssetResponse& response);
    void failRequest(const String& requestId, const char* errorMessage);
    void processQueuedRequests();
    void processNextQueuedRequest();
    bool saveAssetToStorage(const AssetResponse& response);
    
    // Deferred logo save methods
    void deferLogoSave(const AssetResponse& response);
    void processDeferredSaves();
};

}  // namespace LogoAssets
}  // namespace Application

#endif  // MESSAGE_BUS_LOGO_SUPPLIER_H
