#pragma once

#include <Arduino.h>
#include <functional>
#include <unordered_map>
#include "../messaging/Message.h"

/**
 * BRUTAL LOGO MANAGER
 * 
 * No abstractions. No suppliers. No storage classes.
 * Just request logos and get them.
 * 
 * Usage:
 *   BrutalLogoManager::getInstance().requestLogo("chrome", [](bool success, uint8_t* data, size_t size) {
 *       if (success) {
 *           // Use the logo data
 *       }
 *   });
 */
class BrutalLogoManager {
public:
    using LogoCallback = std::function<void(bool success, uint8_t* data, size_t size, const String& error)>;

private:
    struct LogoRequest {
        String processName;
        String requestId;
        LogoCallback callback;
        unsigned long requestTime;
        bool expired = false;
    };

    static BrutalLogoManager* instance;
    bool initialized = false;
    
    // Simple request tracking
    std::unordered_map<String, LogoRequest> pendingRequests;
    
    // Stats
    uint32_t requestsSubmitted = 0;
    uint32_t responsesReceived = 0;
    uint32_t requestsTimedOut = 0;
    uint32_t requestsFailed = 0;
    
    static const unsigned long REQUEST_TIMEOUT_MS = 30000; // 30 seconds

    BrutalLogoManager() = default;

public:
    static BrutalLogoManager& getInstance() {
        if (!instance) {
            instance = new BrutalLogoManager();
        }
        return *instance;
    }

    bool init() {
        if (initialized) return true;
        
        // Subscribe to asset responses using BRUTAL messaging
        Messaging::subscribe(Messaging::Message::TYPE_ASSET_RESPONSE, [this](const Messaging::Message& msg) {
            handleAssetResponse(msg);
        });
        
        initialized = true;
        return true;
    }

    void deinit() {
        // Fail all pending requests
        for (auto& [requestId, request] : pendingRequests) {
            if (request.callback) {
                request.callback(false, nullptr, 0, "System shutting down");
            }
        }
        pendingRequests.clear();
        initialized = false;
    }

    /**
     * Request a logo for a process
     * Callback will be called with the result
     */
    bool requestLogo(const String& processName, LogoCallback callback) {
        if (!initialized) {
            if (callback) callback(false, nullptr, 0, "Not initialized");
            return false;
        }

        // Create asset request message
        auto msg = Messaging::Message::createAssetRequest(processName, "");
        
        // Track the request
        LogoRequest request;
        request.processName = processName;
        request.requestId = msg.requestId;
        request.callback = callback;
        request.requestTime = millis();
        
        pendingRequests[msg.requestId] = request;
        
        // Send the message
        Messaging::sendMessage(msg);
        
        requestsSubmitted++;
        return true;
    }

    /**
     * Update - call this regularly to timeout expired requests
     */
    void update() {
        if (!initialized) return;
        
        unsigned long currentTime = millis();
        std::vector<String> expiredRequests;
        
        // Find expired requests
        for (auto& [requestId, request] : pendingRequests) {
            if (!request.expired && (currentTime - request.requestTime) > REQUEST_TIMEOUT_MS) {
                request.expired = true;
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

    /**
     * Get status string for debugging
     */
    String getStatus() const {
        String status = "BrutalLogoManager Status:\n";
        status += "- Initialized: " + String(initialized ? "Yes" : "No") + "\n";
        status += "- Pending requests: " + String(pendingRequests.size()) + "\n";
        status += "- Requests submitted: " + String(requestsSubmitted) + "\n";
        status += "- Responses received: " + String(responsesReceived) + "\n";
        status += "- Requests timed out: " + String(requestsTimedOut) + "\n";
        status += "- Requests failed: " + String(requestsFailed) + "\n";
        return status;
    }

private:
    /**
     * Handle asset response from server
     */
    void handleAssetResponse(const Messaging::Message& msg) {
        const auto& asset = msg.data.asset;
        
        auto it = pendingRequests.find(msg.requestId);
        if (it == pendingRequests.end()) {
            return; // Unknown request
        }
        
        LogoRequest& request = it->second;
        
        if (asset.success && strlen(asset.assetDataBase64) > 0) {
            // Decode base64 data
            size_t encodedLen = strlen(asset.assetDataBase64);
            size_t decodedSize = (encodedLen * 3) / 4;
            
            if (decodedSize > 0 && decodedSize <= 100000) { // 100KB max
                uint8_t* logoData = (uint8_t*)malloc(decodedSize);
                if (logoData) {
                    size_t actualSize = 0;
                    int result = mbedtls_base64_decode(
                        logoData, decodedSize, &actualSize,
                        (const unsigned char*)asset.assetDataBase64, encodedLen
                    );
                    
                    if (result == 0) {
                        // Success!
                        if (request.callback) {
                            request.callback(true, logoData, actualSize, "");
                        }
                        responsesReceived++;
                    } else {
                        // Decode failed
                        free(logoData);
                        if (request.callback) {
                            request.callback(false, nullptr, 0, "Failed to decode base64 data");
                        }
                        requestsFailed++;
                    }
                } else {
                    // Memory allocation failed
                    if (request.callback) {
                        request.callback(false, nullptr, 0, "Memory allocation failed");
                    }
                    requestsFailed++;
                }
            } else {
                // Invalid size
                if (request.callback) {
                    request.callback(false, nullptr, 0, "Invalid logo size");
                }
                requestsFailed++;
            }
        } else {
            // Server reported failure
            if (request.callback) {
                String error = strlen(asset.errorMessage) > 0 ? String(asset.errorMessage) : "Server error";
                request.callback(false, nullptr, 0, error);
            }
            requestsFailed++;
        }
        
        // Remove completed request
        pendingRequests.erase(it);
    }
};

// Static instance
BrutalLogoManager* BrutalLogoManager::instance = nullptr;