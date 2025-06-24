// #include "LogoManager.h"
// #include "LogoSupplier.h"
// #include "MessageBusLogoSupplier.h"
// #include "../messaging/MessageAPI.h"
// #include <esp_log.h>

// static const char* TAG = "LogoSupplierExample";

// namespace Application {
// namespace LogoAssets {

// /**
//  * Example integration showing how to set up and use the LogoSupplier system
//  *
//  * This demonstrates:
//  * 1. Initializing the LogoSupplierManager
//  * 2. Registering a MessageBusLogoSupplier
//  * 3. Using async logo loading with automatic server requests
//  * 4. Handling logo request notifications
//  */

// class LogoSupplierExample {
//    public:
//     static bool initializeLogoSupplierSystem() {
//         ESP_LOGI(TAG, "Initializing LogoSupplier system...");

//         // 1. Initialize the LogoSupplierManager
//         if (!LogoSupplierManager::getInstance().init()) {
//             ESP_LOGE(TAG, "Failed to initialize LogoSupplierManager");
//             return false;
//         }

//         // 2. Initialize and register the MessageBusLogoSupplier
//         MessageBusLogoSupplier& messageBusSupplier = MessageBusLogoSupplier::getInstance();

//         // Configure supplier settings
//         messageBusSupplier.setRequestTimeout(30000);     // 30 second timeout
//         messageBusSupplier.setMaxConcurrentRequests(3);  // Max 3 concurrent requests

//         // Register the supplier with high priority
//         if (!LogoSupplierManager::getInstance().registerSupplier(&messageBusSupplier, 100)) {
//             ESP_LOGE(TAG, "Failed to register MessageBusLogoSupplier");
//             return false;
//         }

//         // 3. Configure LogoManager to use suppliers
//         LogoManager& logoManager = LogoManager::getInstance();
//         logoManager.enableAutoRequests(true);

//         // Set up notification callback (optional)
//         logoManager.setLogoRequestCallback([](const char* processName, bool success, const char* error) {
//             if (success) {
//                 ESP_LOGI(TAG, "Logo request succeeded for: %s", processName);
//             } else {
//                 ESP_LOGW(TAG, "Logo request failed for: %s (error: %s)",
//                          processName, error ? error : "unknown");
//             }
//         });

//         ESP_LOGI(TAG, "LogoSupplier system initialized successfully");
//         return true;
//     }

//     static void updateLogoSupplierSystem() {
//         // Update the supplier manager (call from main loop)
//         LogoSupplierManager::getInstance().update();
//     }

//     static void shutdownLogoSupplierSystem() {
//         ESP_LOGI(TAG, "Shutting down LogoSupplier system...");

//         // Unregister suppliers
//         LogoSupplierManager::getInstance().unregisterSupplier(&MessageBusLogoSupplier::getInstance());

//         // Deinitialize manager
//         LogoSupplierManager::getInstance().deinit();

//         ESP_LOGI(TAG, "LogoSupplier system shutdown complete");
//     }

//     // Example: Synchronous logo loading (will auto-request if missing)
//     static void exampleSyncLoading() {
//         ESP_LOGI(TAG, "=== Synchronous Logo Loading Example ===");

//         LogoManager& logoManager = LogoManager::getInstance();

//         // Try to load a logo - if it doesn't exist locally,
//         // LogoManager will automatically request it via suppliers
//         LogoLoadResult result = logoManager.loadLogo("chrome.exe");

//         if (result.success) {
//             ESP_LOGI(TAG, "Logo loaded successfully: %zu bytes", result.size);
//             // Use the logo data...
//             // Don't forget to free the memory when done:
//             free(result.data);
//         } else {
//             ESP_LOGI(TAG, "Logo not available immediately: %s", result.errorMessage);
//             ESP_LOGI(TAG, "If auto-request is enabled, the logo will be requested in the background");
//         }
//     }

//     // Example: Asynchronous logo loading with callback
//     static void exampleAsyncLoading() {
//         ESP_LOGI(TAG, "=== Asynchronous Logo Loading Example ===");

//         LogoManager& logoManager = LogoManager::getInstance();

//         // Request logo asynchronously - this will return immediately,
//         // and the callback will be called when the logo is available (or fails)
//         bool requestSubmitted = logoManager.loadLogoAsync("discord.exe",
//                                                           [](const LogoLoadResult& result) {
//                                                               if (result.success) {
//                                                                   ESP_LOGI(TAG, "Async logo loaded: %zu bytes", result.size);
//                                                                   // Use the logo data...
//                                                                   // Don't forget to free the memory when done:
//                                                                   free(result.data);
//                                                               } else {
//                                                                   ESP_LOGI(TAG, "Async logo loading failed: %s", result.errorMessage);
//                                                               }
//                                                           });

//         if (requestSubmitted) {
//             ESP_LOGI(TAG, "Async logo request submitted");
//         } else {
//             ESP_LOGE(TAG, "Failed to submit async logo request");
//         }
//     }

//     // Example: Check system status
//     static void printSystemStatus() {
//         ESP_LOGI(TAG, "=== LogoSupplier System Status ===");

//         // LogoSupplierManager status
//         String managerStatus = LogoSupplierManager::getInstance().getStatus();
//         ESP_LOGI(TAG, "SupplierManager Status:\n%s", managerStatus.c_str());

//         // MessageBusLogoSupplier status
//         String supplierStatus = MessageBusLogoSupplier::getInstance().getStatus();
//         ESP_LOGI(TAG, "MessageBusSupplier Status:\n%s", supplierStatus.c_str());

//         // Messaging system status
//         String messagingStatus = Messaging::MessageAPI::getStatus();
//         ESP_LOGI(TAG, "Messaging Status:\n%s", messagingStatus.c_str());
//     }

//     // Example: Manual logo request (for testing)
//     static void requestSpecificLogo(const char* processName) {
//         ESP_LOGI(TAG, "=== Manual Logo Request Example ===");
//         ESP_LOGI(TAG, "Requesting logo for: %s", processName);

//         bool success = LogoSupplierManager::getInstance().requestLogo(processName,
//                                                                       [processName](const AssetResponse& response) {
//                                                                           if (response.success && response.hasAssetData) {
//                                                                               ESP_LOGI(TAG, "Manual request succeeded for %s: %zu bytes",
//                                                                                        processName, response.assetDataSize);

//                                                                               // Save the logo using LogoManager
//                                                                               LogoManager& logoManager = LogoManager::getInstance();
//                                                                               LogoSaveResult saveResult = logoManager.saveLogo(processName,
//                                                                                                                                response.assetData,
//                                                                                                                                response.assetDataSize,
//                                                                                                                                response.metadata);

//                                                                               if (saveResult.success) {
//                                                                                   ESP_LOGI(TAG, "Logo saved successfully");
//                                                                               } else {
//                                                                                   ESP_LOGW(TAG, "Failed to save logo: %s", saveResult.errorMessage);
//                                                                               }

//                                                                               // Clean up the asset data
//                                                                               if (response.assetData) {
//                                                                                   free(response.assetData);
//                                                                               }
//                                                                           } else {
//                                                                               ESP_LOGW(TAG, "Manual request failed for %s: %s",
//                                                                                        processName, response.errorMessage.c_str());
//                                                                           }
//                                                                       });

//         if (success) {
//             ESP_LOGI(TAG, "Manual logo request submitted");
//         } else {
//             ESP_LOGE(TAG, "Failed to submit manual logo request");
//         }
//     }
// };

// }  // namespace LogoAssets
// }  // namespace Application

// /*
//  * USAGE EXAMPLE:
//  *
//  * In your main application initialization:
//  *
//  *   // Initialize messaging system first
//  *   Messaging::MessageAPI::init();
//  *
//  *   // Initialize LogoManager
//  *   LogoManager::getInstance().init();
//  *
//  *   // Initialize LogoSupplier system
//  *   LogoSupplierExample::initializeLogoSupplierSystem();
//  *
//  * In your main loop:
//  *
//  *   // Update messaging system
//  *   Messaging::MessageAPI::update();
//  *
//  *   // Update logo supplier system
//  *   LogoSupplierExample::updateLogoSupplierSystem();
//  *
//  * Usage examples:
//  *
//  *   // Sync loading (auto-requests if missing)
//  *   LogoSupplierExample::exampleSyncLoading();
//  *
//  *   // Async loading
//  *   LogoSupplierExample::exampleAsyncLoading();
//  *
//  *   // Check status
//  *   LogoSupplierExample::printSystemStatus();
//  *
//  *   // Manual request
//  *   LogoSupplierExample::requestSpecificLogo("spotify.exe");
//  *
//  * Cleanup:
//  *
//  *   LogoSupplierExample::shutdownLogoSupplierSystem();
//  *   LogoManager::getInstance().deinit();
//  *   Messaging::MessageAPI::shutdown();
//  */
