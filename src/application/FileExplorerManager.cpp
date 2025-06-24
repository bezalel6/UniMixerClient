#include "FileExplorerManager.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include "LVGLMessageHandler.h"
#include <ui/ui.h>
#include <esp_log.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char* TAG = "FileExplorerManager";

namespace Application {
namespace FileExplorer {

// Thread-safe callback system
static SemaphoreHandle_t g_callbackMutex = nullptr;
static volatile bool g_callbackActive = false;
static String g_loadingPath;
static FileExplorerManager* g_manager = nullptr;

// Button debouncing and UI state management
static unsigned long g_lastButtonClickTime = 0;
static const unsigned long BUTTON_DEBOUNCE_MS = 1000;  // Reasonable debounce for UI operations
static bool g_operationInProgress = false;

// Enhanced event filtering to catch phantom touch events
struct ButtonEventTracker {
    lv_obj_t* buttonPtr;
    unsigned long lastEventTime;
    uint32_t eventCount;
    bool operationActive;
};

static ButtonEventTracker g_newButtonTracker = {nullptr, 0, 0, false};
static const unsigned long AGGRESSIVE_DEBOUNCE_MS = 2000;  // 2 second lockout

// Static callback function for listDirectory - now thread-safe
static void directoryListingCallback(const char* name, bool isDir, size_t size) {
    // Take mutex to ensure thread safety
    if (!g_callbackMutex || xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "FAILED to take callback mutex, skipping item");
        return;
    }

    // Check if callback is still active (not cancelled by another thread)
    if (!g_callbackActive) {
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    // Defensive null-checking
    if (!name || strlen(name) == 0 || strlen(name) > 255) {
        ESP_LOGW(TAG, "Callback received invalid name, skipping");
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    if (!g_manager) {
        ESP_LOGW(TAG, "No manager instance available for callback");
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    // Validate the path
    if (g_loadingPath.isEmpty() || g_loadingPath.length() > 200) {
        ESP_LOGW(TAG, "Loading path is invalid in callback");
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    // Check available heap before doing string operations
    if (ESP.getFreeHeap() < 8192) {  // Less than 8KB free heap
        ESP_LOGW(TAG, "Low memory, skipping file item: %s", name);
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    try {
        FileItem item;

        // Use safer string operations with pre-allocation
        item.name.reserve(strlen(name) + 1);
        item.name = String(name);

        // Build full path more safely
        size_t pathLen = g_loadingPath.length() + strlen(name) + 2;  // +2 for "/" and null terminator
        if (pathLen > 255) {
            ESP_LOGW(TAG, "Path too long for item: %s", name);
            xSemaphoreGive(g_callbackMutex);
            return;
        }

        item.fullPath.reserve(pathLen);
        item.fullPath = g_loadingPath;
        if (!item.fullPath.endsWith("/")) {
            item.fullPath += "/";
        }
        item.fullPath += name;

        item.isDirectory = isDir;
        item.size = size;

        // Format file size more safely
        item.sizeString.reserve(16);
        item.sizeString = g_manager->formatFileSize(size);

        ESP_LOGD(TAG, "Added item: %s, fullPath: %s, isDir: %s",
                 item.name.c_str(), item.fullPath.c_str(), item.isDirectory ? "true" : "false");

        g_manager->addItem(item);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "EXCEPTION in callback for %s: %s", name, e.what());
    } catch (...) {
        ESP_LOGE(TAG, "UNKNOWN EXCEPTION occurred in directory listing callback for: %s", name);
    }

    xSemaphoreGive(g_callbackMutex);
}

FileExplorerManager& FileExplorerManager::getInstance() {
    static FileExplorerManager instance;
    return instance;
}

bool FileExplorerManager::init() {
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing File Explorer Manager");

    // Create mutex for thread-safe callbacks
    if (!g_callbackMutex) {
        g_callbackMutex = xSemaphoreCreateMutex();
        if (!g_callbackMutex) {
            ESP_LOGE(TAG, "Failed to create callback mutex");
            return false;
        }
    }

    currentPath = "/";
    state = FE_STATE_IDLE;
    selectedItem = nullptr;
    uiCreated = false;  // Initialize UI state flag

    // Initialize dynamic UI components to nullptr
    contentPanel = nullptr;
    fileList = nullptr;
    actionPanel = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;
    modalOverlay = nullptr;
    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;

    initialized = true;
    return true;
}

void FileExplorerManager::deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing File Explorer Manager");

    // Cancel any active callbacks
    if (g_callbackMutex && xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_callbackActive = false;
        g_loadingPath = "";
        g_manager = nullptr;
        xSemaphoreGive(g_callbackMutex);
    }

    // Properly clean up UI first
    destroyDynamicUI();
    clearItems();

    // Reset all state
    initialized = false;
    uiCreated = false;
    g_operationInProgress = false;

    // Clean up mutex
    if (g_callbackMutex) {
        vSemaphoreDelete(g_callbackMutex);
        g_callbackMutex = nullptr;
    }
}

bool FileExplorerManager::navigateToPath(const String& path) {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "Cannot navigate: SD card not mounted");
        state = FE_STATE_ERROR;
        return false;
    }

    // Validate the path parameter
    const char* pathCStr = nullptr;
    try {
        pathCStr = path.c_str();
        ESP_LOGI(TAG, "Navigating to path: %s", pathCStr ? pathCStr : "NULL");
    } catch (...) {
        ESP_LOGE(TAG, "EXCEPTION while accessing path.c_str() in navigateToPath");
        state = FE_STATE_ERROR;
        return false;
    }

    if (!pathCStr) {
        ESP_LOGE(TAG, "CRITICAL: path.c_str() returned NULL in navigateToPath");
        state = FE_STATE_ERROR;
        return false;
    }

    if (loadDirectory(path)) {
        ESP_LOGI(TAG, "Successfully loaded directory: %s", pathCStr);
        currentPath = path;
        updateUI();
        return true;
    }

    ESP_LOGE(TAG, "Failed to navigate to path: %s", pathCStr);
    return false;
}

bool FileExplorerManager::navigateUp() {
    if (currentPath == "/") {
        return false;  // Already at root
    }

    // Find parent directory
    int lastSlash = currentPath.lastIndexOf('/');
    if (lastSlash <= 0) {
        return navigateToPath("/");
    }

    String parentPath = currentPath.substring(0, lastSlash);
    if (parentPath.isEmpty()) {
        parentPath = "/";
    }

    return navigateToPath(parentPath);
}

bool FileExplorerManager::navigateToRoot() {
    return navigateToPath("/");
}

void FileExplorerManager::refreshCurrentDirectory() {
    ESP_LOGI(TAG, "Refreshing current directory");
    navigateToPath(currentPath);
}

bool FileExplorerManager::createDirectory(const String& name) {
    if (!Hardware::SD::isMounted() || name.isEmpty()) {
        resetButtonState();
        return false;
    }

    String fullPath = currentPath;
    if (!fullPath.endsWith("/")) {
        fullPath += "/";
    }
    fullPath += name;

    ESP_LOGI(TAG, "Creating directory: %s", fullPath.c_str());

    bool success = Hardware::SD::createDirectory(fullPath.c_str());

    if (success) {
        ESP_LOGI(TAG, "Directory created successfully, refreshing view");
        refreshCurrentDirectory();
    } else {
        ESP_LOGE(TAG, "Failed to create directory: %s", fullPath.c_str());
    }

    // Always reset button state after operation
    resetButtonState();

    return success;
}

bool FileExplorerManager::deleteItem(const String& path) {
    if (!Hardware::SD::isMounted() || path.isEmpty()) {
        return false;
    }

    ESP_LOGI(TAG, "Deleting item: %s", path.c_str());

    // Check if it's a directory
    if (Hardware::SD::directoryExists(path.c_str())) {
        if (Hardware::SD::removeDirectory(path.c_str())) {
            refreshCurrentDirectory();
            return true;
        }
    } else {
        // It's a file
        auto result = Hardware::SD::deleteFile(path.c_str());
        if (result.success) {
            refreshCurrentDirectory();
            return true;
        }
    }

    return false;
}

bool FileExplorerManager::createTextFile(const String& name, const String& content) {
    if (!Hardware::SD::isMounted() || name.isEmpty()) {
        return false;
    }

    String fullPath = currentPath;
    if (!fullPath.endsWith("/")) {
        fullPath += "/";
    }
    fullPath += name;

    ESP_LOGI(TAG, "Creating text file: %s", fullPath.c_str());

    auto result = Hardware::SD::writeFile(fullPath.c_str(), content.c_str(), false);
    if (result.success) {
        refreshCurrentDirectory();
        return true;
    }

    return false;
}

bool FileExplorerManager::readTextFile(const String& path, String& content) {
    if (!Hardware::SD::isMounted() || path.isEmpty()) {
        return false;
    }

    char buffer[2048];  // 2KB buffer for text files
    auto result = Hardware::SD::readFile(path.c_str(), buffer, sizeof(buffer));

    if (result.success) {
        content = String(buffer);
        return true;
    }

    return false;
}

bool FileExplorerManager::writeTextFile(const String& path, const String& content) {
    if (!Hardware::SD::isMounted() || path.isEmpty()) {
        return false;
    }

    auto result = Hardware::SD::writeFile(path.c_str(), content.c_str(), false);
    return result.success;
}

void FileExplorerManager::updateUI() {
    if (!initialized) {
        ESP_LOGW(TAG, "updateUI called but manager not initialized");
        return;
    }

    ESP_LOGI(TAG, "=== UI UPDATE === updateUI() called");
    ESP_LOGI(TAG, "uiCreated: %s, contentPanel: %p, actionPanel: %p",
             uiCreated ? "true" : "false", contentPanel, actionPanel);

    // Create UI only once - follow proper lifecycle management
    if (!uiCreated) {
        ESP_LOGI(TAG, "=== UI UPDATE === Creating UI for the first time");
        createDynamicUI();
        uiCreated = true;
    } else if (!contentPanel || !actionPanel) {
        // This should not happen if lifecycle is properly managed
        ESP_LOGW(TAG, "=== UI UPDATE === UI marked as created but objects missing - fixing");
        ESP_LOGW(TAG, "contentPanel: %p, actionPanel: %p", contentPanel, actionPanel);
        destroyDynamicUI();
        createDynamicUI();
        uiCreated = true;
    } else {
        ESP_LOGI(TAG, "=== UI UPDATE === UI already exists, updating content only");
    }

    updatePathDisplay();
    updateSDStatus();
    updateFileList();
    ESP_LOGI(TAG, "=== UI UPDATE === updateUI() completed");
}

void FileExplorerManager::updateSDStatus() {
    if (!ui_objSDStatusIndicator) {
        return;
    }

    if (Hardware::SD::isMounted()) {
        lv_label_set_text(ui_objSDStatusIndicator, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(ui_objSDStatusIndicator, lv_color_make(0, 255, 0), LV_PART_MAIN);
    } else {
        lv_label_set_text(ui_objSDStatusIndicator, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(ui_objSDStatusIndicator, lv_color_make(255, 0, 0), LV_PART_MAIN);
    }
}

void FileExplorerManager::updatePathDisplay() {
    if (ui_lblCurrentPath) {
        lv_label_set_text(ui_lblCurrentPath, currentPath.c_str());
    }
}

void FileExplorerManager::updateFileList() {
    if (!fileList) {
        return;
    }

    // Clear existing items
    lv_obj_clean(fileList);

    // Add ".." entry if not at root
    if (currentPath != "/") {
        lv_obj_t* item = lv_list_add_button(fileList, LV_SYMBOL_DIRECTORY, "..");
        lv_obj_set_user_data(item, (void*)(-1));  // Special marker for parent directory
    }

    // Add directory items first, then files
    for (size_t i = 0; i < currentItems.size(); i++) {
        const FileItem& item = currentItems[i];

        const char* icon = item.isDirectory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
        String displayText = item.name;

        if (!item.isDirectory) {
            displayText += " (" + item.sizeString + ")";
        }

        lv_obj_t* listItem = lv_list_add_button(fileList, icon, displayText.c_str());
        lv_obj_set_user_data(listItem, (void*)i);  // Store index

        // Add event callback with better validation
        lv_obj_add_event_cb(listItem, [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
            
            // Validate event and item
            if (!e || !item) {
                return;
            }
            
            int index = (int)(intptr_t)lv_obj_get_user_data(item);
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            
            // Get current items with bounds checking
            const auto& currentItems = manager.getCurrentItems();
            
            if (code == LV_EVENT_CLICKED) {
                if (index == -1) {
                    // Parent directory - validate we're not at root
                    if (manager.getCurrentPath() != "/") {
                        manager.navigateUp();
                    }
                } else if (index >= 0 && index < (int)currentItems.size()) {
                    // Validate the item still exists and is valid
                    const FileItem& fileItem = currentItems[index];
                    if (!fileItem.name.isEmpty() && !fileItem.fullPath.isEmpty()) {
                        manager.onFileItemClicked(&fileItem);
                    } else {
                        ESP_LOGW("FileExplorerManager", "Invalid file item at index %d", index);
                    }
                } else {
                    ESP_LOGW("FileExplorerManager", "Invalid item index: %d (size: %d)", index, currentItems.size());
                }
            } else if (code == LV_EVENT_LONG_PRESSED) {
                if (index >= 0 && index < (int)currentItems.size()) {
                    const FileItem& fileItem = currentItems[index];
                    if (!fileItem.name.isEmpty()) {
                        manager.showProperties(&fileItem);
                    }
                }
            } }, LV_EVENT_ALL, nullptr);
    }
}

void FileExplorerManager::createDynamicUI() {
    if (!ui_screenFileExplorer) {
        ESP_LOGW(TAG, "File explorer screen not available");
        return;
    }

    ESP_LOGI(TAG, "=== UI CREATION === Creating dynamic UI components");

    // Create main content panel (middle 60%)
    contentPanel = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_width(contentPanel, lv_pct(100));
    lv_obj_set_height(contentPanel, lv_pct(60));
    lv_obj_set_align(contentPanel, LV_ALIGN_CENTER);
    lv_obj_remove_flag(contentPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Create file list
    fileList = lv_list_create(contentPanel);
    lv_obj_set_size(fileList, lv_pct(100), lv_pct(100));
    lv_obj_set_align(fileList, LV_ALIGN_CENTER);

    // Create action panel (bottom 10%)
    actionPanel = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_width(actionPanel, lv_pct(100));
    lv_obj_set_height(actionPanel, lv_pct(10));
    lv_obj_set_align(actionPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(actionPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(actionPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Create New button with aggressive event filtering
    ESP_LOGI(TAG, "=== UI CREATION === Creating New button with phantom event protection");
    btnNewFolder = lv_button_create(actionPanel);
    lv_obj_t* lblNewFolder = lv_label_create(btnNewFolder);
    lv_label_set_text(lblNewFolder, "New");

    // Initialize button tracker
    g_newButtonTracker.buttonPtr = btnNewFolder;
    g_newButtonTracker.lastEventTime = 0;
    g_newButtonTracker.eventCount = 0;
    g_newButtonTracker.operationActive = false;

    lv_obj_add_event_cb(btnNewFolder, [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        unsigned long currentTime = Hardware::Device::getMillis();
        
        ESP_LOGI(TAG, "=== PHANTOM FILTER === Event: code=%d, target=%p, time=%lu", 
                 code, target, currentTime);
        ESP_LOGI(TAG, "=== PHANTOM FILTER === Tracker: ptr=%p, lastTime=%lu, count=%u, active=%s",
                 g_newButtonTracker.buttonPtr, g_newButtonTracker.lastEventTime, 
                 g_newButtonTracker.eventCount, g_newButtonTracker.operationActive ? "YES" : "NO");
        
        if (code == LV_EVENT_CLICKED) {
            // Validate button pointer
            if (target != g_newButtonTracker.buttonPtr) {
                ESP_LOGE(TAG, "=== PHANTOM FILTER === BLOCKED: Button pointer mismatch!");
                return;
            }
            
            // Check if operation is active
            if (g_newButtonTracker.operationActive) {
                ESP_LOGW(TAG, "=== PHANTOM FILTER === BLOCKED: Operation already active");
                return;
            }
            
            // Aggressive time-based filtering
            unsigned long timeDiff = currentTime - g_newButtonTracker.lastEventTime;
            if (g_newButtonTracker.lastEventTime != 0 && timeDiff < AGGRESSIVE_DEBOUNCE_MS) {
                ESP_LOGW(TAG, "=== PHANTOM FILTER === BLOCKED: Phantom event detected (time diff: %lu ms)", timeDiff);
                return;
            }
            
            // Update tracker
            g_newButtonTracker.lastEventTime = currentTime;
            g_newButtonTracker.eventCount++;
            g_newButtonTracker.operationActive = true;
            
            ESP_LOGI(TAG, "=== PHANTOM FILTER === ACCEPTED: Event #%u, calling handler", g_newButtonTracker.eventCount);
            FileExplorerManager::getInstance().onNewFolderClicked();
        } else {
            ESP_LOGD(TAG, "=== PHANTOM FILTER === Non-click event: %d", code);
        } }, LV_EVENT_CLICKED, nullptr);

    // Create other buttons (simplified for now)
    ESP_LOGI(TAG, "=== UI CREATION === Creating other buttons");
    btnRefresh = lv_button_create(actionPanel);
    lv_obj_t* lblRefresh = lv_label_create(btnRefresh);
    lv_label_set_text(lblRefresh, LV_SYMBOL_REFRESH);
    lv_obj_add_event_cb(btnRefresh, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "=== LVGL EVENT === Refresh button clicked");
            FileExplorerManager::getInstance().onRefreshClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    btnProperties = lv_button_create(actionPanel);
    lv_obj_t* lblProperties = lv_label_create(btnProperties);
    lv_label_set_text(lblProperties, "Info");
    lv_obj_add_event_cb(btnProperties, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "=== LVGL EVENT === Properties button clicked");
            FileExplorerManager::getInstance().onPropertiesClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    btnDelete = lv_button_create(actionPanel);
    lv_obj_t* lblDelete = lv_label_create(btnDelete);
    lv_label_set_text(lblDelete, LV_SYMBOL_TRASH);
    lv_obj_add_event_cb(btnDelete, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ESP_LOGI(TAG, "=== LVGL EVENT === Delete button clicked");
            FileExplorerManager::getInstance().onDeleteClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    ESP_LOGI(TAG, "=== UI CREATION === Dynamic UI creation completed");
}

void FileExplorerManager::destroyDynamicUI() {
    ESP_LOGI(TAG, "=== UI CLEANUP === Destroying dynamic UI with explicit event cleanup");

    // Reset button tracker to prevent invalid pointer reference
    g_newButtonTracker.buttonPtr = nullptr;
    g_newButtonTracker.lastEventTime = 0;
    g_newButtonTracker.eventCount = 0;
    g_newButtonTracker.operationActive = false;

    // BEST PRACTICE: Explicitly remove event callbacks before destroying objects
    if (btnNewFolder) {
        lv_obj_remove_event_cb(btnNewFolder, nullptr);  // Remove all callbacks
        ESP_LOGI(TAG, "Removed event callbacks from New button");
    }
    if (btnRefresh) {
        lv_obj_remove_event_cb(btnRefresh, nullptr);
        ESP_LOGI(TAG, "Removed event callbacks from Refresh button");
    }
    if (btnProperties) {
        lv_obj_remove_event_cb(btnProperties, nullptr);
        ESP_LOGI(TAG, "Removed event callbacks from Properties button");
    }
    if (btnDelete) {
        lv_obj_remove_event_cb(btnDelete, nullptr);
        ESP_LOGI(TAG, "Removed event callbacks from Delete button");
    }

    // Clean up modal first
    if (modalOverlay) {
        lv_obj_del(modalOverlay);
        modalOverlay = nullptr;
    }

    // Clean up main panels (this will automatically clean up child objects)
    if (contentPanel) {
        lv_obj_del(contentPanel);
        contentPanel = nullptr;
    }
    if (actionPanel) {
        lv_obj_del(actionPanel);
        actionPanel = nullptr;
    }

    // Reset all pointers to nullptr (LVGL deletes children automatically)
    fileList = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;
    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;

    // Reset UI creation state
    uiCreated = false;

    ESP_LOGI(TAG, "=== UI CLEANUP === Dynamic UI destruction completed with tracker reset");
}

bool FileExplorerManager::loadDirectory(const String& path) {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    if (path.isEmpty() || path.length() > 200) {
        ESP_LOGW(TAG, "Path is invalid: %s", path.c_str());
        return false;
    }

    // Check available memory before starting
    if (ESP.getFreeHeap() < 10240) {  // Less than 10KB free heap
        ESP_LOGW(TAG, "Insufficient memory to load directory");
        state = FE_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG, "Loading directory: %s", path.c_str());

    state = FE_STATE_LOADING;
    clearItems();

    // Safely convert String to const char* with validation
    const char* pathStr = path.c_str();

    if (!pathStr) {
        ESP_LOGE(TAG, "CRITICAL: path.c_str() returned NULL for path object");
        state = FE_STATE_ERROR;
        return false;
    }

    // Additional validation of path content
    if (strlen(pathStr) == 0 || strlen(pathStr) > 200) {
        ESP_LOGE(TAG, "CRITICAL: Invalid path length: %d", strlen(pathStr));
        state = FE_STATE_ERROR;
        return false;
    }

    // Check if directory exists before trying to list it
    if (!Hardware::SD::directoryExists(pathStr)) {
        ESP_LOGE(TAG, "Directory does not exist: %s", pathStr);
        state = FE_STATE_ERROR;
        return false;
    }

    // Add timeout protection to prevent hanging operations
    unsigned long startTime = Hardware::Device::getMillis();
    const unsigned long DIRECTORY_LOAD_TIMEOUT_MS = 10000;  // 10 second timeout

    // Thread-safe callback setup
    if (!g_callbackMutex || xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire callback mutex");
        state = FE_STATE_ERROR;
        return false;
    }

    // Cancel any previous operation and set up new one
    g_callbackActive = false;
    Hardware::Device::delay(10);  // Give any active callbacks time to finish

    g_loadingPath = path;
    g_manager = this;
    g_callbackActive = true;

    xSemaphoreGive(g_callbackMutex);

    // Call the SD listing function with timeout protection
    bool success = false;
    try {
        success = Hardware::SD::listDirectory(pathStr, directoryListingCallback);

        // Check for timeout
        if (Hardware::Device::getMillis() - startTime > DIRECTORY_LOAD_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Directory listing timed out after %lu ms", DIRECTORY_LOAD_TIMEOUT_MS);
            success = false;
        }
    } catch (...) {
        ESP_LOGE(TAG, "EXCEPTION occurred during Hardware::SD::listDirectory() call");
        success = false;
    }

    // Thread-safe cleanup
    if (g_callbackMutex && xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        g_callbackActive = false;
        g_loadingPath = "";
        g_manager = nullptr;
        xSemaphoreGive(g_callbackMutex);
    }

    if (success) {
        try {
            // Sort items: directories first, then files, both alphabetically
            std::sort(currentItems.begin(), currentItems.end(), [](const FileItem& a, const FileItem& b) {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory > b.isDirectory;  // Directories first
                }
                return a.name.compareTo(b.name) < 0;  // Alphabetical
            });

            state = FE_STATE_IDLE;
            ESP_LOGI(TAG, "Loaded %d items from directory", currentItems.size());
        } catch (...) {
            ESP_LOGE(TAG, "Exception occurred while sorting directory items");
            state = FE_STATE_ERROR;
            success = false;
        }
    } else {
        state = FE_STATE_ERROR;
        ESP_LOGE(TAG, "Failed to load directory: %s", path.c_str());
    }

    return success;
}

void FileExplorerManager::clearItems() {
    currentItems.clear();
    selectedItem = nullptr;
}

String FileExplorerManager::formatFileSize(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + "B";
    } else if (bytes < 1024 * 1024) {
        return String(bytes / 1024.0, 1) + "KB";
    } else {
        return String(bytes / (1024.0 * 1024.0), 1) + "MB";
    }
}

// Event handlers
void FileExplorerManager::onFileItemClicked(const FileItem* item) {
    // Validate item pointer and state
    if (!item) {
        ESP_LOGW(TAG, "Null item clicked, ignoring");
        return;
    }

    // Check if we're in a valid state for navigation
    if (state == FE_STATE_LOADING) {
        ESP_LOGW(TAG, "Navigation ignored - still loading directory");
        return;
    }

    // Validate item data
    if (item->name.isEmpty() || item->fullPath.isEmpty()) {
        ESP_LOGW(TAG, "Invalid item data (empty name or path), ignoring click");
        return;
    }

    selectedItem = item;

    if (item->isDirectory) {
        ESP_LOGI(TAG, "Clicking directory: %s, fullPath: %s", item->name.c_str(), item->fullPath.c_str());

        // Additional validation for directory path
        if (item->fullPath.length() > 200) {
            ESP_LOGW(TAG, "Directory path too long (%d chars), ignoring", item->fullPath.length());
            return;
        }

        // Check available memory before navigation
        if (ESP.getFreeHeap() < 8192) {
            ESP_LOGW(TAG, "Low memory (%u bytes), delaying directory navigation", ESP.getFreeHeap());
            return;
        }

        // Make a local copy of the path to prevent String corruption issues
        String targetPath = item->fullPath;
        ESP_LOGI(TAG, "Created local path copy: %s (length: %d)", targetPath.c_str(), targetPath.length());

        navigateToPath(targetPath);
    } else {
        // For now, just select the file
        ESP_LOGI(TAG, "Selected file: %s, fullPath: %s", item->name.c_str(), item->fullPath.c_str());
    }
}

void FileExplorerManager::onFileItemDoubleClicked(const FileItem* item) {
    onFileItemClicked(item);  // Same behavior for now
}

void FileExplorerManager::onBackButtonClicked() {
    ESP_LOGI(TAG, "Back button clicked");
    // This will be handled by the event handler in UiEventHandlers.cpp
}

void FileExplorerManager::onRefreshClicked() {
    ESP_LOGI(TAG, "=== BUTTON EVENT === Refresh button clicked");
    refreshCurrentDirectory();
}

void FileExplorerManager::onNewFolderClicked() {
    unsigned long currentTime = Hardware::Device::getMillis();
    ESP_LOGI(TAG, "=== BUTTON EVENT === New folder button clicked at %lu ms", currentTime);

    // The phantom filtering is now handled in the LVGL callback
    // This function is only called for verified, non-phantom events
    ESP_LOGI(TAG, "BUTTON ACCEPTED: Starting folder creation (verified by phantom filter)");

    // Provide immediate visual feedback
    if (btnNewFolder) {
        lv_obj_add_state(btnNewFolder, LV_STATE_DISABLED);
        lv_obj_t* label = lv_obj_get_child(btnNewFolder, 0);
        if (label) {
            lv_label_set_text(label, "...");
        }
    }

    showCreateFolderDialog();
}

void FileExplorerManager::onDeleteClicked() {
    // Get current timestamp for detailed button tracking
    unsigned long currentTime = Hardware::Device::getMillis();
    unsigned long timeSinceLastClick = currentTime - g_lastButtonClickTime;

    ESP_LOGI(TAG, "=== BUTTON EVENT === Delete button clicked");
    ESP_LOGI(TAG, "Current time: %lu ms, Last click: %lu ms, Time diff: %lu ms",
             currentTime, g_lastButtonClickTime, timeSinceLastClick);

    // Debounce button clicks to prevent double-firing
    if (timeSinceLastClick < BUTTON_DEBOUNCE_MS) {
        ESP_LOGW(TAG, "BUTTON DEBOUNCED: Delete button click ignored (time diff: %lu ms)", timeSinceLastClick);
        return;
    }

    g_lastButtonClickTime = currentTime;
    ESP_LOGI(TAG, "BUTTON ACCEPTED: Processing delete action");

    if (selectedItem) {
        showDeleteConfirmation(selectedItem);
    }
}

void FileExplorerManager::onPropertiesClicked() {
    ESP_LOGI(TAG, "=== BUTTON EVENT === Properties button clicked");
    if (selectedItem) {
        showProperties(selectedItem);
    }
}

// Dialog implementations - simplified for now
void FileExplorerManager::showCreateFolderDialog() {
    // For now, create a simple folder with timestamp
    String folderName = "NewFolder_" + String(Hardware::Device::getMillis());
    createDirectory(folderName);
}

void FileExplorerManager::showDeleteConfirmation(const FileItem* item) {
    // For now, directly delete (in production, show confirmation dialog)
    ESP_LOGW(TAG, "Deleting item without confirmation: %s", item->name.c_str());
    deleteItem(item->fullPath);
}

void FileExplorerManager::showProperties(const FileItem* item) {
    ESP_LOGI(TAG, "Properties for: %s", item->name.c_str());
    ESP_LOGI(TAG, "  Type: %s", item->isDirectory ? "Directory" : "File");
    ESP_LOGI(TAG, "  Size: %s", item->sizeString.c_str());
    ESP_LOGI(TAG, "  Path: %s", item->fullPath.c_str());
}

void FileExplorerManager::addItem(const FileItem& item) {
    try {
        if (item.name.isEmpty()) {
            ESP_LOGW(TAG, "Attempting to add item with empty name, skipping");
            return;
        }
        currentItems.push_back(item);
    } catch (...) {
        ESP_LOGE(TAG, "Exception occurred while adding item to list");
    }
}

// Helper function to reset button state and operation flag
void FileExplorerManager::resetButtonState() {
    ESP_LOGI(TAG, "=== BUTTON STATE === Resetting operation state and phantom filter");

    // Reset old operation tracking
    g_operationInProgress = false;

    // Reset phantom filter state
    g_newButtonTracker.operationActive = false;

    // Re-enable and restore New button
    if (btnNewFolder) {
        lv_obj_remove_state(btnNewFolder, LV_STATE_DISABLED);
        lv_obj_t* label = lv_obj_get_child(btnNewFolder, 0);
        if (label) {
            lv_label_set_text(label, "New");
        }
        ESP_LOGI(TAG, "New button re-enabled, phantom filter reset");
    }
}

}  // namespace FileExplorer
}  // namespace Application
