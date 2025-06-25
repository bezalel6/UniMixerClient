#include "MessageBusLogoSupplier.h"
#include "../messaging/MessageConfig.h"
#include "../hardware/DeviceManager.h"
#include "../logo/LogoManager.h"
#include <esp_log.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

static const char* TAG = "MBLogoSupplier";

namespace Application {
namespace LogoAssets {

// =============================================================================
// CONFIGURATION
// =============================================================================

// Message types are now defined in Messaging::Config
// No more topic-based constants needed in messageType-based system

// =============================================================================
// SINGLETON AND LIFECYCLE
// =============================================================================

MessageBusLogoSupplier& MessageBusLogoSupplier::getInstance() {
    static MessageBusLogoSupplier instance;
    return instance;
}

bool MessageBusLogoSupplier::init() {
    if (initialized) {
        ESP_LOGW(TAG, "MessageBusLogoSupplier already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing MessageBusLogoSupplier");

    // Check if messaging system is available
    if (!Messaging::MessageAPI::isHealthy()) {
        ESP_LOGW(TAG, "Messaging system not healthy - will retry when available");
        // Don't fail init, just mark as not ready until messaging is available
    }

    // Create mutex for thread-safe operations
    requestMutex = xSemaphoreCreateMutex();
    if (!requestMutex) {
        ESP_LOGE(TAG, "Failed to create request mutex");
        return false;
    }

    // Subscribe to asset responses (by messageType)
    Messaging::MessageAPI::subscribeToType(Messaging::Config::MESSAGE_TYPE_ASSET_RESPONSE,
                                           [this](const Messaging::Message& message) {
                                               this->onAssetResponse(message);
                                           });

    // Clear statistics
    requestsSubmitted = 0;
    responsesReceived = 0;
    requestsTimedOut = 0;
    requestsFailed = 0;

    pendingRequests.clear();
    requestQueue.clear();
    initialized = true;

    ESP_LOGI(TAG, "MessageBusLogoSupplier initialized successfully");
    return true;
}

void MessageBusLogoSupplier::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing MessageBusLogoSupplier");

    // Unsubscribe from messages (messageType-based)
    Messaging::MessageAPI::unsubscribeFromType(Messaging::Config::MESSAGE_TYPE_ASSET_RESPONSE);

    // Fail all pending requests
    if (requestMutex && xSemaphoreTake(requestMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        for (auto& [requestId, request] : pendingRequests) {
            if (request.callback) {
                AssetResponse response = createAssetResponse(false, request.processName.c_str(),
                                                             requestId.c_str(), "Service shutting down");
                request.callback(response);
            }
        }
        pendingRequests.clear();
        requestQueue.clear();
        xSemaphoreGive(requestMutex);
    }

    // Clean up mutex
    if (requestMutex) {
        vSemaphoreDelete(requestMutex);
        requestMutex = nullptr;
    }

    initialized = false;
    ESP_LOGI(TAG, "MessageBusLogoSupplier deinitialized");
}

bool MessageBusLogoSupplier::isReady() const {
    return initialized && Messaging::MessageAPI::isHealthy() &&
           pendingRequests.size() < maxConcurrentRequests;
}

bool MessageBusLogoSupplier::requestLogo(const char* processName, AssetRequestCallback callback) {
    if (!initialized || !processName || !callback) {
        return false;
    }

    if (!requestMutex || xSemaphoreTake(requestMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for logo request");
        return false;
    }

    // Create asset request
    AssetRequest request = createAssetRequest(processName);

    // Create pending request
    PendingRequest pendingRequest;
    pendingRequest.requestId = request.requestId;
    pendingRequest.processName = request.processName;
    pendingRequest.callback = callback;
    pendingRequest.requestTime = millis();
    pendingRequest.expired = false;

    // If no requests are currently active, send immediately
    if (pendingRequests.empty()) {
        pendingRequests[request.requestId] = pendingRequest;

        bool success = sendAssetRequest(request);
        if (success) {
            requestsSubmitted++;
            ESP_LOGI(TAG, "Asset request sent immediately for: %s (requestId: %s)",
                     processName, request.requestId.c_str());
        } else {
            pendingRequests.erase(request.requestId);
            ESP_LOGE(TAG, "Failed to send asset request for: %s", processName);
        }

        xSemaphoreGive(requestMutex);
        return success;
    } else {
        // Queue the request for later processing
        requestQueue.push_back(pendingRequest);
        ESP_LOGI(TAG, "Asset request queued for: %s (queue size: %zu)",
                 processName, requestQueue.size());

        xSemaphoreGive(requestMutex);
        return true;
    }
}

void MessageBusLogoSupplier::update() {
    if (!initialized) {
        return;
    }

    // Timeout expired requests
    timeoutExpiredRequests();

    // Process queued requests if no active requests
    processQueuedRequests();
}

String MessageBusLogoSupplier::getStatus() const {
    String status = "MessageBusLogoSupplier Status:\n";
    status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    status += "- Ready: " + String(isReady() ? "Yes" : "No") + "\n";
    status += "- Active requests: " + String(pendingRequests.size()) + "\n";
    status += "- Queued requests: " + String(requestQueue.size()) + "\n";
    status += "- Max concurrent: " + String(maxConcurrentRequests) + " (serial limitation)\n";
    status += "- Request timeout: " + String(requestTimeoutMs / 1000) + "s\n";
    status += "- Requests submitted: " + String(requestsSubmitted) + "\n";
    status += "- Responses received: " + String(responsesReceived) + "\n";
    status += "- Requests timed out: " + String(requestsTimedOut) + "\n";
    status += "- Requests failed: " + String(requestsFailed) + "\n";
    return status;
}

// =============================================================================
// PRIVATE METHODS
// =============================================================================

void MessageBusLogoSupplier::onAssetResponse(const Messaging::Message& message) {
    if (!initialized) {
        return;
    }

    ESP_LOGD(TAG, "Received asset response: %d chars", message.payload.length());

    // Parse response
    AssetResponse response = parseAssetResponse(message.payload);
    if (response.requestId.isEmpty()) {
        ESP_LOGW(TAG, "Invalid asset response - missing request ID");
        return;
    }

    // Save asset data if successful and data is present
    if (response.success && response.hasAssetData && response.assetData && response.assetDataSize > 0) {
        saveAssetToStorage(response);
    }

    completeRequest(response.requestId, response);
}

bool MessageBusLogoSupplier::sendAssetRequest(const AssetRequest& request) {
    String jsonPayload = createAssetRequestJson(request);
    return Messaging::MessageAPI::publish(jsonPayload);
}

void MessageBusLogoSupplier::timeoutExpiredRequests() {
    if (!initialized || !requestMutex) {
        return;
    }

    if (xSemaphoreTake(requestMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    unsigned long currentTime = millis();
    std::vector<String> expiredRequests;

    // Find expired requests
    for (auto& [requestId, request] : pendingRequests) {
        if (!request.expired && (currentTime - request.requestTime) > requestTimeoutMs) {
            request.expired = true;
            expiredRequests.push_back(requestId);
        }
    }

    // Process expired requests
    for (const String& requestId : expiredRequests) {
        auto it = pendingRequests.find(requestId);
        if (it != pendingRequests.end()) {
            ESP_LOGW(TAG, "Asset request timed out: %s (process: %s)",
                     requestId.c_str(), it->second.processName.c_str());

            if (it->second.callback) {
                AssetResponse response = createAssetResponse(false, it->second.processName.c_str(),
                                                             requestId.c_str(), "Request timed out");
                it->second.callback(response);
            }

            pendingRequests.erase(it);
            requestsTimedOut++;
        }
    }

    // Process next queued request if we just freed up a slot
    if (!expiredRequests.empty()) {
        processNextQueuedRequest();
    }

    xSemaphoreGive(requestMutex);
}

AssetResponse MessageBusLogoSupplier::parseAssetResponse(const String& jsonPayload) {
    AssetResponse response;
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, jsonPayload);
    if (error) {
        ESP_LOGW(TAG, "Failed to parse asset response JSON: %s", error.c_str());
        return response;
    }

    // Extract basic fields (all in camelCase as requested)
    response.messageType = doc["messageType"] | "";
    response.requestId = doc["requestId"] | "";
    response.deviceId = doc["deviceId"] | "";
    response.processName = doc["processName"] | "";
    response.success = doc["success"] | false;
    response.errorMessage = doc["errorMessage"] | "";
    response.timestamp = Hardware::Device::getMillis();

    // Parse simplified metadata if present
    if (doc["metadata"].is<JsonObject>() && !doc["metadata"].isNull()) {
        JsonObject metadataObj = doc["metadata"];

        // Extract simple width/height for the simplified response
        response.width = metadataObj["width"] | 0;
        response.height = metadataObj["height"] | 0;

        String format = metadataObj["format"] | "bin";
        response.format = format;
    }

    // Parse asset data if present (base64 encoded)
    if (doc["assetData"].is<String>() && !doc["assetData"].isNull()) {
        String base64Data = doc["assetData"];
        if (!base64Data.isEmpty()) {
            // Calculate decoded size
            size_t decodedSize = (base64Data.length() * 3) / 4;
            if (decodedSize > 0 && decodedSize <= 100000) {  // 100KB max size
                                                             // Allocate memory for decoded data
#if CONFIG_SPIRAM_USE_MALLOC
                response.assetData = (uint8_t*)ps_malloc(decodedSize);
#else
                response.assetData = (uint8_t*)malloc(decodedSize);
#endif
                if (response.assetData) {
                    size_t actualSize = 0;
                    int result = mbedtls_base64_decode(response.assetData, decodedSize,
                                                       &actualSize, (const unsigned char*)base64Data.c_str(),
                                                       base64Data.length());
                    if (result == 0) {
                        response.assetDataSize = actualSize;
                        response.hasAssetData = true;
                    } else {
                        free(response.assetData);
                        response.assetData = nullptr;
                        ESP_LOGW(TAG, "Failed to decode base64 asset data");
                    }
                }
            }
        }
    }

    return response;
}

String MessageBusLogoSupplier::createAssetRequestJson(const AssetRequest& request) {
    JsonDocument doc;

    doc["messageType"] = request.messageType;
    doc["requestId"] = request.requestId;
    doc["deviceId"] = request.deviceId;
    doc["processName"] = request.processName;
    doc["timestamp"] = request.timestamp;

    String result;
    serializeJson(doc, result);
    return result;
}

void MessageBusLogoSupplier::completeRequest(const String& requestId, const AssetResponse& response) {
    if (!requestMutex || xSemaphoreTake(requestMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for request completion");
        return;
    }

    auto it = pendingRequests.find(requestId);

    if (it != pendingRequests.end()) {
        if (it->second.callback) {
            it->second.callback(response);
        }

        pendingRequests.erase(it);

        if (response.success) {
            responsesReceived++;
            ESP_LOGI(TAG, "Asset request completed successfully: %s", requestId.c_str());
        } else {
            requestsFailed++;
            ESP_LOGW(TAG, "Asset request failed: %s (error: %s)",
                     requestId.c_str(), response.errorMessage.c_str());
        }

        // Process next queued request if available
        processNextQueuedRequest();
    } else {
        ESP_LOGW(TAG, "Received response for unknown request: %s", requestId.c_str());
    }

    xSemaphoreGive(requestMutex);
}

void MessageBusLogoSupplier::failRequest(const String& requestId, const char* errorMessage) {
    AssetResponse response = createAssetResponse(false, "", requestId.c_str(), errorMessage);
    completeRequest(requestId, response);
}

void MessageBusLogoSupplier::processQueuedRequests() {
    if (!requestMutex || xSemaphoreTake(requestMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;  // Don't block if mutex unavailable
    }

    processNextQueuedRequest();

    xSemaphoreGive(requestMutex);
}

void MessageBusLogoSupplier::processNextQueuedRequest() {
    // This should be called with mutex already held
    if (requestQueue.empty() || !pendingRequests.empty()) {
        return;  // No queued requests or already have active request
    }

    // Get next request from queue
    PendingRequest nextRequest = requestQueue.front();
    requestQueue.erase(requestQueue.begin());

    // Create asset request
    AssetRequest request = createAssetRequest(nextRequest.processName.c_str());
    request.requestId = nextRequest.requestId;  // Use the existing request ID

    // Add to pending requests
    pendingRequests[nextRequest.requestId] = nextRequest;

    // Send the request
    bool success = sendAssetRequest(request);
    if (success) {
        requestsSubmitted++;
        ESP_LOGI(TAG, "Queued asset request sent for: %s (requestId: %s, queue remaining: %zu)",
                 nextRequest.processName.c_str(), nextRequest.requestId.c_str(), requestQueue.size());
    } else {
        // Remove from pending if send failed
        pendingRequests.erase(nextRequest.requestId);
        ESP_LOGE(TAG, "Failed to send queued asset request for: %s", nextRequest.processName.c_str());

        // Call the callback with failure
        if (nextRequest.callback) {
            AssetResponse response = createAssetResponse(false, nextRequest.processName.c_str(),
                                                         nextRequest.requestId.c_str(), "Failed to send request");
            nextRequest.callback(response);
        }
    }
}

bool MessageBusLogoSupplier::saveAssetToStorage(const AssetResponse& response) {
    if (!response.hasAssetData || !response.assetData || response.assetDataSize == 0) {
        ESP_LOGW(TAG, "No asset data to save for process: %s", response.processName.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Saving LVGL logo binary for process: %s (%zu bytes)",
             response.processName.c_str(), response.assetDataSize);

    // Use the new LogoManager to save the LVGL binary data
    // The response.assetData is already in LVGL binary format from the server
    bool success = Logo::LogoManager::getInstance().saveLogo(
        response.processName.c_str(),
        response.assetData,
        response.assetDataSize);

    if (success) {
        ESP_LOGI(TAG, "Successfully saved LVGL logo for: %s", response.processName.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to save LVGL logo for: %s", response.processName.c_str());
    }

    return success;
}

}  // namespace LogoAssets
}  // namespace Application
