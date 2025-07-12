#include "SimpleLogoManager.h"
#include "../messaging/MessagingInit.h"
#include <esp_log.h>

static const char* TAG = "SimpleLogoManager";

SimpleLogoManager* SimpleLogoManager::instance = nullptr;
const char* SimpleLogoManager::LOGOS_DIR = "/logos/";

SimpleLogoManager& SimpleLogoManager::getInstance() {
    if (!instance) {
        instance = new SimpleLogoManager();
    }
    return *instance;
}

bool SimpleLogoManager::init() {
    if (initialized) return true;
    
    ESP_LOGI(TAG, "Initializing SimpleLogoManager");
    
    // Ensure SD is mounted
    if (!Hardware::SD::isMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return false;
    }
    
    // Create logos directory
    if (!ensureLogosDirectory()) {
        ESP_LOGE(TAG, "Failed to create logos directory");
        return false;
    }
    
    // Subscribe to asset responses
    Messaging::subscribe(
        Messaging::Message::TYPE_ASSET_RESPONSE,
        [this](const Messaging::Message& msg) { handleAssetResponse(msg); }
    );
    
    initialized = true;
    ESP_LOGI(TAG, "SimpleLogoManager initialized");
    return true;
}

void SimpleLogoManager::deinit() {
    if (!initialized) return;
    
    // Fail all pending requests
    for (auto& [requestId, request] : pendingRequests) {
        if (request.callback) {
            request.callback(false, nullptr, 0, "System shutting down");
        }
    }
    pendingRequests.clear();
    
    initialized = false;
    ESP_LOGI(TAG, "SimpleLogoManager deinitialized");
}

void SimpleLogoManager::update() {
    if (!initialized) return;
    
    unsigned long currentTime = millis();
    std::vector<String> expiredRequests;
    
    // Find expired requests
    for (auto& [requestId, request] : pendingRequests) {
        if ((currentTime - request.requestTime) > REQUEST_TIMEOUT_MS) {
            expiredRequests.push_back(requestId);
        }
    }
    
    // Process expired requests
    for (const String& requestId : expiredRequests) {
        auto it = pendingRequests.find(requestId);
        if (it != pendingRequests.end()) {
            if (it->second.callback) {
                it->second.callback(false, nullptr, 0, "Request timed out");
            }
            pendingRequests.erase(it);
            requestsTimedOut++;
        }
    }
}

bool SimpleLogoManager::requestLogo(const String& processName, LogoCallback callback) {
    if (!initialized) {
        if (callback) callback(false, nullptr, 0, "Not initialized");
        return false;
    }
    
    String sanitized = sanitizeProcessName(processName);
    
    // Check if logo already exists
    if (hasLogo(sanitized)) {
        String filePath = getLogoPath(sanitized);
        size_t fileSize = Hardware::SD::getFileSize(filePath.c_str());
        
        if (fileSize > 0 && fileSize <= 100000) { // 100KB max
            uint8_t* buffer = (uint8_t*)malloc(fileSize);
            if (buffer) {
                Hardware::SD::SDFileResult result = Hardware::SD::readFile(filePath.c_str(), (char*)buffer, fileSize);
                if (result.success) {
                    if (callback) callback(true, buffer, result.bytesProcessed, "");
                    return true;
                }
                free(buffer);
            }
        }
    }
    
    // Create asset request
    auto msg = Messaging::Message::createAssetRequest(sanitized.c_str(), "");
    
    // Track the request
    LogoRequest request;
    request.processName = sanitized;
    request.requestId = msg.requestId;
    request.callback = callback;
    request.requestTime = millis();
    
    pendingRequests[msg.requestId] = request;
    
    // Send the message
    Messaging::sendMessage(msg);
    
    requestsSubmitted++;
    return true;
}

bool SimpleLogoManager::hasLogo(const String& processName) {
    if (!initialized) return false;
    String filePath = getLogoPath(sanitizeProcessName(processName));
    return Hardware::SD::fileExists(filePath.c_str());
}

bool SimpleLogoManager::deleteLogo(const String& processName) {
    if (!initialized) return false;
    String filePath = getLogoPath(sanitizeProcessName(processName));
    Hardware::SD::SDFileResult result = Hardware::SD::deleteFile(filePath.c_str());
    return result.success;
}

String SimpleLogoManager::getLVGLPath(const String& processName) {
    return "S:" + getLogoPath(sanitizeProcessName(processName));
}

String SimpleLogoManager::getStatus() const {
    String status = "SimpleLogoManager Status:\n";
    status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
    status += "- Pending requests: " + String(pendingRequests.size()) + "\n";
    status += "- Requests submitted: " + String(requestsSubmitted) + "\n";
    status += "- Responses received: " + String(responsesReceived) + "\n";
    status += "- Requests timed out: " + String(requestsTimedOut) + "\n";
    status += "- Requests failed: " + String(requestsFailed) + "\n";
    return status;
}

void SimpleLogoManager::handleAssetResponse(const Messaging::Message& msg) {
    const auto& asset = msg.data.asset;
    
    auto it = pendingRequests.find(msg.requestId);
    if (it == pendingRequests.end()) {
        return; // Unknown request
    }
    
    LogoRequest& request = it->second;
    
    if (asset.success && asset.assetDataSize > 0) {
        // Save PNG directly to SD card
        String filePath = getLogoPath(request.processName);
        Hardware::SD::SDFileResult writeResult = Hardware::SD::writeFile(
            filePath.c_str(), 
            (const char*)asset.assetData, 
            false
        );
        
        if (writeResult.success) {
            // Success! Call callback with data
            if (request.callback) {
                request.callback(true, (uint8_t*)asset.assetData, asset.assetDataSize, "");
            }
            responsesReceived++;
        } else {
            // Failed to save
            if (request.callback) {
                request.callback(false, nullptr, 0, "Failed to save logo file");
            }
            requestsFailed++;
        }
    } else {
        // Server reported failure
        if (request.callback) {
            String error = strlen(asset.errorMessage) > 0 
                ? String(asset.errorMessage) 
                : "Server error";
            request.callback(false, nullptr, 0, error);
        }
        requestsFailed++;
    }
    
    // Remove completed request
    pendingRequests.erase(it);
}

String SimpleLogoManager::getLogoPath(const String& processName) {
    return String(LOGOS_DIR) + processName + ".png";
}

String SimpleLogoManager::sanitizeProcessName(const String& processName) {
    String sanitized = processName;
    
    // Remove any path separators
    sanitized.replace("/", "_");
    sanitized.replace("\\", "_");
    
    // Remove any special characters that might cause issues
    sanitized.replace(":", "_");
    sanitized.replace("*", "_");
    sanitized.replace("?", "_");
    sanitized.replace("\"", "_");
    sanitized.replace("<", "_");
    sanitized.replace(">", "_");
    sanitized.replace("|", "_");
    
    // Trim whitespace
    sanitized.trim();
    
    // If empty, use default
    if (sanitized.isEmpty()) {
        sanitized = "unknown";
    }
    
    return sanitized;
}

bool SimpleLogoManager::ensureLogosDirectory() {
    return Hardware::SD::ensureDirectory(LOGOS_DIR);
}