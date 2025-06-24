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

// Static callback function for listDirectory - now thread-safe
static void directoryListingCallback(const char* name, bool isDir, size_t size) {
    // Take mutex to ensure thread safety
    if (!g_callbackMutex || xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take callback mutex, skipping item");
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
        ESP_LOGE(TAG, "Exception in callback for %s: %s", name, e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception occurred in directory listing callback for: %s", name);
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

    destroyDynamicUI();
    clearItems();
    initialized = false;

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

    ESP_LOGI(TAG, "Navigating to path: %s", path.c_str());

    if (loadDirectory(path)) {
        ESP_LOGI(TAG, "Successfully loaded directory: %s", path.c_str());
        currentPath = path;
        updateUI();
        return true;
    }

    ESP_LOGE(TAG, "Failed to navigate to path: %s", path.c_str());
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
        return false;
    }

    String fullPath = currentPath;
    if (!fullPath.endsWith("/")) {
        fullPath += "/";
    }
    fullPath += name;

    ESP_LOGI(TAG, "Creating directory: %s", fullPath.c_str());

    if (Hardware::SD::createDirectory(fullPath.c_str())) {
        refreshCurrentDirectory();
        return true;
    }

    return false;
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
        return;
    }

    // Create dynamic UI if not exists
    if (!contentPanel) {
        createDynamicUI();
    }

    updatePathDisplay();
    updateSDStatus();
    updateFileList();
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

        // Add event callback
        lv_obj_add_event_cb(listItem, [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
            int index = (int)(intptr_t)lv_obj_get_user_data(item);
            
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            
            if (code == LV_EVENT_CLICKED) {
                if (index == -1) {
                    // Parent directory
                    manager.navigateUp();
                } else if (index >= 0 && index < manager.getCurrentItems().size()) {
                    manager.onFileItemClicked(&manager.getCurrentItems()[index]);
                }
            } else if (code == LV_EVENT_LONG_PRESSED) {
                if (index >= 0 && index < manager.getCurrentItems().size()) {
                    manager.showProperties(&manager.getCurrentItems()[index]);
                }
            } }, LV_EVENT_ALL, nullptr);
    }
}

void FileExplorerManager::createDynamicUI() {
    if (!ui_screenFileExplorer) {
        ESP_LOGW(TAG, "File explorer screen not available");
        return;
    }

    ESP_LOGI(TAG, "Creating dynamic UI components");

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

    // Create action buttons
    btnNewFolder = lv_button_create(actionPanel);
    lv_obj_t* lblNewFolder = lv_label_create(btnNewFolder);
    lv_label_set_text(lblNewFolder, "New");
    lv_obj_add_event_cb(btnNewFolder, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onNewFolderClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    btnRefresh = lv_button_create(actionPanel);
    lv_obj_t* lblRefresh = lv_label_create(btnRefresh);
    lv_label_set_text(lblRefresh, LV_SYMBOL_REFRESH);
    lv_obj_add_event_cb(btnRefresh, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onRefreshClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    btnProperties = lv_button_create(actionPanel);
    lv_obj_t* lblProperties = lv_label_create(btnProperties);
    lv_label_set_text(lblProperties, "Info");
    lv_obj_add_event_cb(btnProperties, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onPropertiesClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    btnDelete = lv_button_create(actionPanel);
    lv_obj_t* lblDelete = lv_label_create(btnDelete);
    lv_label_set_text(lblDelete, LV_SYMBOL_TRASH);
    lv_obj_add_event_cb(btnDelete, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onDeleteClicked();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::destroyDynamicUI() {
    if (modalOverlay) {
        lv_obj_del(modalOverlay);
        modalOverlay = nullptr;
    }
    if (contentPanel) {
        lv_obj_del(contentPanel);
        contentPanel = nullptr;
    }
    if (actionPanel) {
        lv_obj_del(actionPanel);
        actionPanel = nullptr;
    }

    // Reset pointers
    fileList = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;
    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;
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

    // Check if directory exists before trying to list it
    if (!Hardware::SD::directoryExists(path.c_str())) {
        ESP_LOGW(TAG, "Directory does not exist: %s", path.c_str());
        state = FE_STATE_ERROR;
        return false;
    }

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

    // Call the SD listing function
    bool success = false;
    try {
        success = Hardware::SD::listDirectory(path.c_str(), directoryListingCallback);
    } catch (...) {
        ESP_LOGE(TAG, "Exception occurred during directory listing");
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
    selectedItem = item;

    if (item->isDirectory) {
        ESP_LOGI(TAG, "Clicking directory: %s, fullPath: %s", item->name.c_str(), item->fullPath.c_str());
        navigateToPath(item->fullPath);
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
    ESP_LOGI(TAG, "Refresh clicked");
    refreshCurrentDirectory();
}

void FileExplorerManager::onNewFolderClicked() {
    ESP_LOGI(TAG, "New folder clicked");
    showCreateFolderDialog();
}

void FileExplorerManager::onDeleteClicked() {
    ESP_LOGI(TAG, "Delete clicked");
    if (selectedItem) {
        showDeleteConfirmation(selectedItem);
    }
}

void FileExplorerManager::onPropertiesClicked() {
    ESP_LOGI(TAG, "Properties clicked");
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

}  // namespace FileExplorer
}  // namespace Application
