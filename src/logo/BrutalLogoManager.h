#pragma once

#include "../hardware/SDManager.h"
#include "../messaging/Message.h"
#include "LogoStorage.h"
#include <Arduino.h>
#include <functional>
#include <unordered_map>
#include <vector>

/**
 * BRUTAL LOGO MANAGER
 *
 * No abstractions. No suppliers. No storage classes.
 * Just request logos and get them.
 *
 * All logos are PNG format.
 *
 * Usage:
 *   BrutalLogoManager::getInstance().requestLogo("chrome", [](bool success,
 * uint8_t* data, size_t size) { if (success) {
 *           // Use the PNG logo data
 *       }
 *   });
 */
class BrutalLogoManager {
public:
  using LogoCallback = std::function<void(bool success, uint8_t *data,
                                          size_t size, const String &error)>;

private:
  struct LogoRequest {
    String processName;
    String requestId;
    LogoCallback callback;
    unsigned long requestTime;
    bool expired = false;
  };

  static BrutalLogoManager *instance;
  bool initialized = false;

  // Simple request tracking
  std::unordered_map<String, LogoRequest> pendingRequests;

  // Stats
  uint32_t requestsSubmitted = 0;
  uint32_t responsesReceived = 0;
  uint32_t requestsTimedOut = 0;
  uint32_t requestsFailed = 0;

  static const unsigned long REQUEST_TIMEOUT_MS = 30000; // 30 seconds

  // Directory constants - directly embedded, no abstractions
  static const char *LOGOS_ROOT;
  static const char *FILES_DIR;
  static const char *MAPPINGS_DIR;
  static const char *METADATA_DIR;

  BrutalLogoManager() = default;

public:
  static BrutalLogoManager &getInstance() {
    if (!instance) {
      instance = new BrutalLogoManager();
    }
    return *instance;
  }

  bool init() {
    if (initialized)
      return true;

    // Directory structure is now created during SD mount process
    if (!Hardware::SD::isMounted()) {
      return false;
    }

    // Subscribe to asset responses using BRUTAL messaging
    Messaging::subscribe(
        Messaging::Message::TYPE_ASSET_RESPONSE,
        [this](const Messaging::Message &msg) { handleAssetResponse(msg); });

    initialized = true;
    return true;
  }

  void deinit() {
    // Fail all pending requests
    for (auto &[requestId, request] : pendingRequests) {
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
  bool requestLogo(const String &processName, LogoCallback callback) {
    if (!initialized) {
      if (callback)
        callback(false, nullptr, 0, "Not initialized");
      return false;
    }

    // // Check if logo already exists locally using SD manager
    // if (hasLogo(processName)) {
    //   String fileName =
    //       Logo::LogoStorage::getInstance().getProcessMapping(processName);
    //   if (!fileName.isEmpty()) {
    //     String filePath =
    //         Logo::LogoStorage::getInstance().getFileSystemPath(fileName);

    //     // Use SD manager's high-level operations
    //     if (Hardware::SD::isMounted() &&
    //         Hardware::SD::fileExists(filePath.c_str())) {
    //       size_t fileSize = Hardware::SD::getFileSize(filePath.c_str());
    //       if (fileSize > 0 && fileSize <= 100000) { // 100KB max
    //         char *buffer =
    //             (char *)malloc(fileSize + 1); // +1 for null terminator
    //         if (buffer) {
    //           // Use SD manager's readFile operation
    //           Hardware::SD::SDFileResult result =
    //               Hardware::SD::readFile(filePath.c_str(), buffer, fileSize);
    //           if (result.success) {
    //             if (callback)
    //               callback(true, (uint8_t *)buffer, result.bytesProcessed,
    //               "");
    //             return true;
    //           }
    //         }
    //       }
    //     }
    //   }
    // }

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
    if (!initialized)
      return;

    unsigned long currentTime = millis();
    std::vector<String> expiredRequests;

    // Find expired requests
    for (auto &[requestId, request] : pendingRequests) {
      if (!request.expired &&
          (currentTime - request.requestTime) > REQUEST_TIMEOUT_MS) {
        request.expired = true;
        expiredRequests.push_back(requestId);
      }
    }

    // Process expired requests
    for (const String &requestId : expiredRequests) {
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
   * Check if a logo exists for a process
   */
  bool hasLogo(const String &processName) {
    if (!initialized)
      return false;
    return Logo::LogoStorage::getInstance().hasProcessMapping(processName);
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
  void handleAssetResponse(const Messaging::Message &msg) {
    const auto &asset = msg.data.asset;

    auto it = pendingRequests.find(msg.requestId);
    if (it == pendingRequests.end()) {
      return; // Unknown request
    }

    LogoRequest &request = it->second;

    if (asset.success && strlen(asset.assetDataBase64) > 0) {
      // Data is received directly (no base64 decoding needed as per user's
      // changes)
      size_t dataSize = strlen(asset.assetDataBase64);

      if (dataSize > 0 && dataSize <= 100000) { // 100KB max
        uint8_t *logoData = (uint8_t *)malloc(dataSize);
        if (logoData) {
          memcpy(logoData, asset.assetDataBase64, dataSize);

          // Save logo to storage as PNG using SD manager
          Logo::LogoStorage &storage = Logo::LogoStorage::getInstance();
          String fileName = ;
          String filePath = storage.getFileSystemPath(fileName);

          // Use SD manager's writeFile operation
          Hardware::SD::SDFileResult writeResult = Hardware::SD::writeFile(
              filePath.c_str(), (const char *)logoData, false);

          if (writeResult.success) {
            // Save process mapping
            if (storage.saveProcessMapping(request.processName, fileName)) {
              // Save metadata
              storage.saveMetadata(request.processName, true, false, millis());

              // Success! Call callback with data
              if (request.callback) {
                request.callback(true, logoData, dataSize, "");
              }
              responsesReceived++;
            } else {
              // Failed to save mapping - cleanup using SD manager
              Hardware::SD::deleteFile(filePath.c_str());
              free(logoData);
              if (request.callback) {
                request.callback(false, nullptr, 0,
                                 "Failed to save logo mapping");
              }
              requestsFailed++;
            }
          } else {
            // Failed to save file
            free(logoData);
            if (request.callback) {
              request.callback(false, nullptr, 0, "Failed to save logo file");
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
};
