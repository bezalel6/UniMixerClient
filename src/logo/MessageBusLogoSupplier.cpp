#include "MessageBusLogoSupplier.h"
#include "../hardware/DeviceManager.h"
#include "../logo/LogoManager.h"
#include "../logo/LogoStorage.h"
#include "../messaging/Message.h"
#include "ManagerMacros.h"
#include <esp_log.h>
#include <mbedtls/base64.h>


static const char *TAG = "MBLogoSupplier";

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

MessageBusLogoSupplier &MessageBusLogoSupplier::getInstance() {
  static MessageBusLogoSupplier instance;
  return instance;
}

bool MessageBusLogoSupplier::init() {
  INIT_GUARD("MessageBusLogoSupplier", initialized, TAG);

  ESP_LOGI(TAG, "Initializing MessageBusLogoSupplier");

  // Create mutex for thread-safe operations
  requestMutex = xSemaphoreCreateMutex();
  if (!requestMutex) {
    ESP_LOGE(TAG, "Failed to create request mutex");
    return false;
  }

  // Create mutex for deferred save operations
  deferredSaveMutex = xSemaphoreCreateMutex();
  if (!deferredSaveMutex) {
    ESP_LOGE(TAG, "Failed to create deferred save mutex");
    return false;
  }

  // Subscribe to asset responses using BRUTAL messaging
  Messaging::subscribe(Messaging::Message::TYPE_ASSET_RESPONSE,
                       [this](const Messaging::Message &msg) {
                         // Direct access to asset data
                         this->onAssetResponse(msg);
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

  // No need to unsubscribe - handlers are cleaned up automatically

  // Fail all pending requests
  if (requestMutex &&
      xSemaphoreTake(requestMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
    for (auto &[requestId, request] : pendingRequests) {
      if (request.callback) {
        AssetResponse response =
            createAssetResponse(false, request.processName.c_str(),
                                requestId.c_str(), "Service shutting down");
        request.callback(response);
      }
    }
    pendingRequests.clear();
    requestQueue.clear();
    xSemaphoreGive(requestMutex);
  }

  // Clean up mutexes
  CLEANUP_SEMAPHORE(requestMutex, TAG, "request");
  CLEANUP_SEMAPHORE(deferredSaveMutex, TAG, "deferred save");

  initialized = false;
  ESP_LOGI(TAG, "MessageBusLogoSupplier deinitialized");
}

bool MessageBusLogoSupplier::isReady() const {
  return initialized && pendingRequests.size() < maxConcurrentRequests;
}

bool MessageBusLogoSupplier::requestLogo(const char *processName,
                                         AssetRequestCallback callback) {
  REQUIRE_INIT("MessageBusLogoSupplier", initialized, TAG, false);
  VALIDATE_PARAM(processName, TAG, "processName", false);
  VALIDATE_PARAM(callback, TAG, "callback", false);

  MUTEX_GUARD(requestMutex, MANAGER_DEFAULT_MUTEX_TIMEOUT_MS, TAG,
              "logo request", false);

  // Create asset request
  AssetRequest request = createAssetRequest(processName);

  // Create pending request
  PendingRequest pendingRequest;
  pendingRequest.requestId = request.requestId;
  pendingRequest.processName = request.processName;
  pendingRequest.callback = callback;
  pendingRequest.requestTime = millis();
  pendingRequest.expired = false;

  // OPTIMIZED: Check memory availability before processing
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 32768) { // 32KB minimum free heap
    ESP_LOGW(TAG, "Low memory (%u bytes), rejecting logo request for: %s",
             freeHeap, processName);
    if (callback) {
      AssetResponse response = createAssetResponse(
          false, processName, request.requestId.c_str(), "Insufficient memory");
      callback(response);
    }
    return false;
  }

  // If no requests are currently active and sufficient memory, send immediately
  if (pendingRequests.size() <
      2) { // OPTIMIZED: Reduced from unlimited to 2 concurrent
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

    MUTEX_RELEASE(requestMutex, TAG, "logo request");
    return success;
  } else {
    // Queue the request for later processing
    requestQueue.push_back(pendingRequest);
    ESP_LOGI(TAG, "Asset request queued for: %s (queue size: %zu)", processName,
             requestQueue.size());

    MUTEX_RELEASE(requestMutex, TAG, "logo request");
    return true;
  }
}

void MessageBusLogoSupplier::update() {
  REQUIRE_INIT_VOID("MessageBusLogoSupplier", initialized, TAG);

  // Timeout expired requests
  timeoutExpiredRequests();

  // Process queued requests if no active requests
  processQueuedRequests();

  // Process deferred logo saves (prevents stack overflow on messaging task)
  processDeferredSaves();
}

String MessageBusLogoSupplier::getStatus() const {
  String status = "MessageBusLogoSupplier Status:\n";
  status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
  status += "- Ready: " + String(isReady() ? "Yes" : "No") + "\n";
  status += "- Active requests: " + String(pendingRequests.size()) + "\n";
  status += "- Queued requests: " + String(requestQueue.size()) + "\n";
  status += "- Max concurrent: " + String(maxConcurrentRequests) +
            " (serial limitation)\n";
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

void MessageBusLogoSupplier::onAssetResponse(const Messaging::Message &msg) {
  REQUIRE_INIT_VOID("MessageBusLogoSupplier", initialized, TAG);

  ESP_LOGD(TAG, "Received asset response from device: %s",
           msg.deviceId.c_str());

  // Direct access to asset data - BRUTAL!
  const auto &asset = msg.data.asset;

  // Convert to AssetResponse
  AssetResponse response =
      createAssetResponse(asset.success, asset.processName,
                          msg.requestId.c_str(), asset.errorMessage);
  response.width = asset.width;
  response.height = asset.height;
  response.format = asset.format;
  response.timestamp = msg.timestamp;

  if (response.requestId.isEmpty()) {
    ESP_LOGW(TAG, "Invalid asset response - missing request ID");
    return;
  }

  // Decode base64 asset data if present
  if (asset.success && strlen(asset.assetDataBase64) > 0) {
    size_t encodedLen = strlen(asset.assetDataBase64);
    size_t decodedSize = (encodedLen * 3) / 4;
    if (decodedSize > 0 && decodedSize <= 100000) { // 100KB max size
#if CONFIG_SPIRAM_USE_MALLOC
      response.assetData = (uint8_t *)ps_malloc(decodedSize);
#else
      response.assetData = (uint8_t *)malloc(decodedSize);
#endif
      if (response.assetData) {
        size_t actualSize = 0;
        int result = mbedtls_base64_decode(
            response.assetData, decodedSize, &actualSize,
            (const unsigned char *)asset.assetDataBase64, encodedLen);
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

  // Save asset data if successful and data is present
  bool shouldSave = response.success && response.hasAssetData &&
                    response.assetData && response.assetDataSize > 0;
  if (shouldSave) {
    // CRITICAL: Defer logo saving to prevent stack overflow on messaging task
    ESP_LOGI(TAG, "Deferring logo save for: %s (%zu bytes)",
             response.processName.c_str(), response.assetDataSize);
    deferLogoSave(response);
  }
  LOG_WARN_IF(!shouldSave && response.success, TAG,
              "Asset response successful but no data to save for: %s",
              response.processName.c_str());

  completeRequest(response.requestId, response);
}

bool MessageBusLogoSupplier::sendAssetRequest(const AssetRequest &request) {
  // Create and send asset request - BRUTAL!
  auto msg = Messaging::Message::createAssetRequest(request.processName,
                                                    request.deviceId);
  msg.requestId = request.requestId; // Use the existing request ID
  Messaging::sendMessage(msg);
  return true;
}

void MessageBusLogoSupplier::timeoutExpiredRequests() {
  REQUIRE_INIT_VOID("MessageBusLogoSupplier", initialized, TAG);

  MUTEX_QUICK_GUARD_VOID(requestMutex, TAG, "timeout expired requests");

  unsigned long currentTime = millis();
  std::vector<String> expiredRequests;

  // Find expired requests
  for (auto &[requestId, request] : pendingRequests) {
    if (!request.expired &&
        (currentTime - request.requestTime) > requestTimeoutMs) {
      request.expired = true;
      expiredRequests.push_back(requestId);
    }
  }

  // Process expired requests
  for (const String &requestId : expiredRequests) {
    auto it = pendingRequests.find(requestId);
    if (it != pendingRequests.end()) {
      ESP_LOGW(TAG, "Asset request timed out: %s (process: %s)",
               requestId.c_str(), it->second.processName.c_str());

      if (it->second.callback) {
        AssetResponse response =
            createAssetResponse(false, it->second.processName.c_str(),
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

  MUTEX_RELEASE(requestMutex, TAG, "timeout expired requests");
}

void MessageBusLogoSupplier::completeRequest(const String &requestId,
                                             const AssetResponse &response) {
  MUTEX_GUARD_VOID(requestMutex, MANAGER_DEFAULT_MUTEX_TIMEOUT_MS, TAG,
                   "request completion");

  auto it = pendingRequests.find(requestId);

  if (it != pendingRequests.end()) {
    if (it->second.callback) {
      it->second.callback(response);
    }

    pendingRequests.erase(it);

    if (response.success) {
      responsesReceived++;
      ESP_LOGI(TAG, "Asset request completed successfully: %s",
               requestId.c_str());
    } else {
      requestsFailed++;
      ESP_LOGW(TAG, "Asset request failed: %s (error: %s)", requestId.c_str(),
               response.errorMessage.c_str());
    }

    // Process next queued request if available
    processNextQueuedRequest();
  } else {
    ESP_LOGW(TAG, "Received response for unknown request: %s",
             requestId.c_str());
  }

  MUTEX_RELEASE(requestMutex, TAG, "request completion");
}

void MessageBusLogoSupplier::failRequest(const String &requestId,
                                         const char *errorMessage) {
  AssetResponse response =
      createAssetResponse(false, "", requestId.c_str(), errorMessage);
  completeRequest(requestId, response);
}

void MessageBusLogoSupplier::processQueuedRequests() {
  MUTEX_QUICK_GUARD_VOID(requestMutex, TAG, "process queued requests");

  processNextQueuedRequest();

  MUTEX_RELEASE(requestMutex, TAG, "process queued requests");
}

void MessageBusLogoSupplier::processNextQueuedRequest() {
  // This should be called with mutex already held
  if (requestQueue.empty() || !pendingRequests.empty()) {
    return; // No queued requests or already have active request
  }

  // Get next request from queue
  PendingRequest nextRequest = requestQueue.front();
  requestQueue.erase(requestQueue.begin());

  // Create asset request
  AssetRequest request = createAssetRequest(nextRequest.processName.c_str());
  request.requestId = nextRequest.requestId; // Use the existing request ID

  // Add to pending requests
  pendingRequests[nextRequest.requestId] = nextRequest;

  // Send the request
  bool success = sendAssetRequest(request);
  if (success) {
    requestsSubmitted++;
    ESP_LOGI(TAG,
             "Queued asset request sent for: %s (requestId: %s, queue "
             "remaining: %zu)",
             nextRequest.processName.c_str(), nextRequest.requestId.c_str(),
             requestQueue.size());
  } else {
    // Remove from pending if send failed
    pendingRequests.erase(nextRequest.requestId);
    ESP_LOGE(TAG, "Failed to send queued asset request for: %s",
             nextRequest.processName.c_str());

    // Call the callback with failure
    if (nextRequest.callback) {
      AssetResponse response = createAssetResponse(
          false, nextRequest.processName.c_str(), nextRequest.requestId.c_str(),
          "Failed to send request");
      nextRequest.callback(response);
    }
  }
}

bool MessageBusLogoSupplier::saveAssetToStorage(const AssetResponse &response) {
  LOG_WARN_IF(!response.hasAssetData || !response.assetData ||
                  response.assetDataSize == 0,
              TAG, "No asset data to save for process: %s",
              response.processName.c_str());

  if (!response.hasAssetData || !response.assetData ||
      response.assetDataSize == 0) {
    return false;
  }

  ESP_LOGI(TAG, "Saving LVGL logo binary for process: %s (%zu bytes)",
           response.processName.c_str(), response.assetDataSize);

  // Use the new LogoManager to save the LVGL binary data
  // The response.assetData is already in LVGL binary format from the server
  String logoPath = Logo::LogoManager::getInstance().saveLogo(
      response.processName.c_str(), response.assetData, response.assetDataSize,
      Logo::LogoStorage::FileType::BINARY);

  if (!logoPath.isEmpty()) {
    ESP_LOGI(TAG, "Successfully saved LVGL logo for: %s at path: %s",
             response.processName.c_str(), logoPath.c_str());
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to save LVGL logo for: %s",
             response.processName.c_str());
    return false;
  }
}

void MessageBusLogoSupplier::deferLogoSave(const AssetResponse &response) {
  if (!deferredSaveMutex) {
    ESP_LOGE(TAG, "Deferred save mutex not initialized");
    return;
  }

  if (xSemaphoreTake(deferredSaveMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Make a copy of the response for deferred processing
    AssetResponse deferredResponse = response;

    // Deep copy the asset data to prevent memory issues
    if (response.assetData && response.assetDataSize > 0) {
      deferredResponse.assetData = (uint8_t *)malloc(response.assetDataSize);
      if (deferredResponse.assetData) {
        memcpy(deferredResponse.assetData, response.assetData,
               response.assetDataSize);
        deferredSaveQueue.push_back(deferredResponse);
        ESP_LOGI(
            TAG,
            "Deferred logo save queued for: %s (%zu bytes, queue size: %zu)",
            response.processName.c_str(), response.assetDataSize,
            deferredSaveQueue.size());
      } else {
        ESP_LOGE(TAG, "Failed to allocate memory for deferred logo save: %s",
                 response.processName.c_str());
      }
    }

    xSemaphoreGive(deferredSaveMutex);
  } else {
    ESP_LOGE(TAG, "Failed to acquire deferred save mutex");
  }
}

void MessageBusLogoSupplier::processDeferredSaves() {
  if (!deferredSaveMutex || deferredSaveQueue.empty()) {
    return;
  }

  if (xSemaphoreTake(deferredSaveMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Process one deferred save per update cycle to prevent blocking
    if (!deferredSaveQueue.empty()) {
      AssetResponse response = deferredSaveQueue.front();
      deferredSaveQueue.erase(deferredSaveQueue.begin());

      ESP_LOGI(TAG, "Processing deferred logo save for: %s (%zu bytes)",
               response.processName.c_str(), response.assetDataSize);

      // Save the logo (this runs on Core 0 main loop, not messaging task)
      bool success = saveAssetToStorage(response);

      // Clean up the allocated memory
      if (response.assetData) {
        free(response.assetData);
      }

      if (success) {
        ESP_LOGI(TAG, "Deferred logo save completed for: %s",
                 response.processName.c_str());
      } else {
        ESP_LOGE(TAG, "Deferred logo save failed for: %s",
                 response.processName.c_str());
      }
    }

    xSemaphoreGive(deferredSaveMutex);
  }
}

} // namespace LogoAssets
} // namespace Application
