#include "LogoSupplier.h"
#include "../hardware/DeviceManager.h"
#include "../messaging/MessageConfig.h"
#include <MessageProtocol.h>
#include <esp_log.h>
#include <algorithm>

static const char* TAG = "LogoSupplier";

namespace Application {
namespace LogoAssets {

// =============================================================================
// LOGO SUPPLIER BASE CLASS HELPERS
// =============================================================================

AssetRequest LogoSupplier::createAssetRequest(const char* processName) {
    AssetRequest request;
    // Use new enum-based constants for external messages
    request.messageType = MessageProtocol::externalMessageTypeToString(Messaging::Config::EXT_MSG_GET_ASSETS);
    request.requestId = Messaging::Config::generateRequestId();
    request.deviceId = Messaging::Config::getDeviceId();
    request.processName = String(processName ? processName : "");
    request.timestamp = Hardware::Device::getMillis();
    return request;
}

AssetResponse LogoSupplier::createAssetResponse(bool success, const char* processName,
                                                const char* requestId, const char* errorMessage) {
    AssetResponse response;
    // Use new enum-based constants for external messages
    response.messageType = MessageProtocol::externalMessageTypeToString(Messaging::Config::EXT_MSG_ASSET_RESPONSE);
    response.requestId = String(requestId ? requestId : "");
    response.deviceId = Messaging::Config::getDeviceId();
    response.processName = String(processName ? processName : "");
    response.success = success;
    response.errorMessage = String(errorMessage ? errorMessage : "");
    response.timestamp = Hardware::Device::getMillis();
    response.assetData = nullptr;
    response.assetDataSize = 0;
    response.hasAssetData = false;
    return response;
}

// =============================================================================
// LOGO SUPPLIER MANAGER
// =============================================================================

LogoSupplierManager& LogoSupplierManager::getInstance() {
    static LogoSupplierManager instance;
    return instance;
}

bool LogoSupplierManager::init() {
    if (initialized) {
        ESP_LOGW(TAG, "LogoSupplierManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing LogoSupplierManager");

    // Create mutex for thread-safe operations
    supplierMutex = xSemaphoreCreateMutex();
    if (!supplierMutex) {
        ESP_LOGE(TAG, "Failed to create supplier mutex");
        return false;
    }

    suppliers.clear();
    initialized = true;

    ESP_LOGI(TAG, "LogoSupplierManager initialized successfully");
    return true;
}

void LogoSupplierManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing LogoSupplierManager");

    // Deinitialize all suppliers
    if (supplierMutex && xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        for (auto& entry : suppliers) {
            if (entry.supplier && entry.enabled) {
                entry.supplier->deinit();
            }
        }
        suppliers.clear();
        xSemaphoreGive(supplierMutex);
    }

    // Clean up mutex
    if (supplierMutex) {
        vSemaphoreDelete(supplierMutex);
        supplierMutex = nullptr;
    }

    initialized = false;
    ESP_LOGI(TAG, "LogoSupplierManager deinitialized");
}

void LogoSupplierManager::update() {
    if (!initialized || !supplierMutex) {
        return;
    }

    if (xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (auto& entry : suppliers) {
            if (entry.supplier && entry.enabled) {
                entry.supplier->update();
            }
        }
        xSemaphoreGive(supplierMutex);
    }
}

bool LogoSupplierManager::registerSupplier(LogoSupplier* supplier, int priority) {
    if (!initialized || !supplier || !supplierMutex) {
        return false;
    }

    if (xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for supplier registration");
        return false;
    }

    // Check if supplier is already registered
    for (const auto& entry : suppliers) {
        if (entry.supplier == supplier) {
            ESP_LOGW(TAG, "Supplier already registered: %s", supplier->getSupplierType());
            xSemaphoreGive(supplierMutex);
            return false;
        }
    }

    // Initialize the supplier
    if (!supplier->init()) {
        ESP_LOGE(TAG, "Failed to initialize supplier: %s", supplier->getSupplierType());
        xSemaphoreGive(supplierMutex);
        return false;
    }

    // Add to list
    SupplierEntry entry;
    entry.supplier = supplier;
    entry.priority = priority;
    entry.enabled = true;

    suppliers.push_back(entry);
    sortSuppliersByPriority();

    ESP_LOGI(TAG, "Registered supplier: %s (priority: %d)", supplier->getSupplierType(), priority);

    xSemaphoreGive(supplierMutex);
    return true;
}

void LogoSupplierManager::unregisterSupplier(LogoSupplier* supplier) {
    if (!initialized || !supplier || !supplierMutex) {
        return;
    }

    if (xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for supplier unregistration");
        return;
    }

    auto it = std::find_if(suppliers.begin(), suppliers.end(),
                           [supplier](const SupplierEntry& entry) {
                               return entry.supplier == supplier;
                           });

    if (it != suppliers.end()) {
        // Deinitialize supplier
        if (it->enabled && it->supplier) {
            it->supplier->deinit();
        }

        suppliers.erase(it);
        ESP_LOGI(TAG, "Unregistered supplier: %s", supplier->getSupplierType());
    }

    xSemaphoreGive(supplierMutex);
}

size_t LogoSupplierManager::getSupplierCount() const {
    if (!initialized || !supplierMutex) {
        return 0;
    }

    size_t count = 0;
    if (xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        count = suppliers.size();
        xSemaphoreGive(supplierMutex);
    }

    return count;
}

bool LogoSupplierManager::requestLogo(const char* processName, AssetRequestCallback callback) {
    if (!initialized || !processName || !callback || !supplierMutex) {
        return false;
    }

    if (xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for logo request");
        return false;
    }

    // Try suppliers in priority order
    for (auto& entry : suppliers) {
        if (entry.supplier && entry.enabled && entry.supplier->isReady()) {
            bool success = entry.supplier->requestLogo(processName, callback);
            if (success) {
                ESP_LOGI(TAG, "Logo request submitted via %s for: %s",
                         entry.supplier->getSupplierType(), processName);
                xSemaphoreGive(supplierMutex);
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "No available suppliers to handle logo request for: %s", processName);
    xSemaphoreGive(supplierMutex);
    return false;
}

String LogoSupplierManager::getStatus() const {
    String status = "LogoSupplierManager Status:\n";
    status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    status += "- Supplier count: " + String(getSupplierCount()) + "\n";

    if (initialized && supplierMutex && xSemaphoreTake(supplierMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (size_t i = 0; i < suppliers.size(); i++) {
            const auto& entry = suppliers[i];
            status += "- Supplier " + String(i + 1) + ": " + String(entry.supplier->getSupplierType());
            status += " (priority: " + String(entry.priority) + ", enabled: " + String(entry.enabled ? "Yes" : "No");
            status += ", ready: " + String(entry.supplier->isReady() ? "Yes" : "No") + ")\n";
        }
        xSemaphoreGive(supplierMutex);
    }

    return status;
}

void LogoSupplierManager::sortSuppliersByPriority() {
    std::sort(suppliers.begin(), suppliers.end(),
              [](const SupplierEntry& a, const SupplierEntry& b) {
                  return a.priority > b.priority;  // Higher priority first
              });
}

}  // namespace LogoAssets
}  // namespace Application
