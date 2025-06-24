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

// Button debouncing
static const unsigned long BUTTON_DEBOUNCE_MS = 500;
static unsigned long g_lastNewButtonTime = 0;

// Static callback function for listDirectory - now thread-safe
static void directoryListingCallback(const char* name, bool isDir, size_t size) {
    // Take mutex to ensure thread safety
    if (!g_callbackMutex || xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take callback mutex");
        return;
    }

    // Check if callback is still active
    if (!g_callbackActive) {
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    // Defensive validation
    if (!name || strlen(name) == 0 || strlen(name) > 255 || !g_manager) {
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    if (g_loadingPath.isEmpty() || g_loadingPath.length() > 200) {
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    // Check available heap
    if (ESP.getFreeHeap() < 8192) {
        ESP_LOGW(TAG, "Low memory, skipping file item: %s", name);
        xSemaphoreGive(g_callbackMutex);
        return;
    }

    try {
        FileItem item;
        item.name.reserve(strlen(name) + 1);
        item.name = String(name);

        // Build full path
        size_t pathLen = g_loadingPath.length() + strlen(name) + 2;
        if (pathLen > 255) {
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
        item.sizeString.reserve(16);
        item.sizeString = g_manager->formatFileSize(size);

        g_manager->addItem(item);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in callback for %s: %s", name, e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in directory listing callback for: %s", name);
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
    selectedListItem = nullptr;
    uiCreated = false;

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
    fileViewerDialog = nullptr;

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
    uiCreated = false;

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
    } catch (...) {
        ESP_LOGE(TAG, "Exception while accessing path.c_str()");
        state = FE_STATE_ERROR;
        return false;
    }

    if (!pathCStr) {
        ESP_LOGE(TAG, "path.c_str() returned NULL");
        state = FE_STATE_ERROR;
        return false;
    }

    if (loadDirectory(path)) {
        currentPath = path;
        updateUI();
        return true;
    }

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

    bool success = Hardware::SD::createDirectory(fullPath.c_str());

    if (success) {
        refreshCurrentDirectory();
    } else {
        ESP_LOGE(TAG, "Failed to create directory: %s", fullPath.c_str());
    }

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
        return;
    }

    // Create UI only once
    if (!uiCreated) {
        createDynamicUI();
        uiCreated = true;
    } else if (!contentPanel || !actionPanel) {
        destroyDynamicUI();
        createDynamicUI();
        uiCreated = true;
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

void FileExplorerManager::updateButtonStates() {
    // Enable/disable buttons based on selection state
    if (btnDelete) {
        if (selectedItem) {
            lv_obj_remove_state(btnDelete, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btnDelete, LV_STATE_DISABLED);
        }
    }

    if (btnProperties) {
        if (selectedItem) {
            lv_obj_remove_state(btnProperties, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btnProperties, LV_STATE_DISABLED);
        }
    }
}

void FileExplorerManager::updateFileList() {
    if (!fileList) {
        return;
    }

    // Clear existing items and selection
    lv_obj_clean(fileList);
    selectedItem = nullptr;
    selectedListItem = nullptr;
    updateButtonStates();

    // Add ".." entry if not at root
    if (currentPath != "/") {
        lv_obj_t* item = lv_list_add_button(fileList, LV_SYMBOL_DIRECTORY, "..");
        lv_obj_set_user_data(item, (void*)(-1));  // Special marker for parent directory

        // Add event callback for ".." button
        lv_obj_add_event_cb(item, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                FileExplorerManager& manager = FileExplorerManager::getInstance();
                if (manager.getCurrentPath() != "/") {
                    ESP_LOGI("FileExplorerManager", "Navigating up from: %s", manager.getCurrentPath().c_str());
                    manager.navigateUp();
                }
            } }, LV_EVENT_CLICKED, nullptr);
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
            
            if (!e || !item) {
                return;
            }
            
            int index = (int)(intptr_t)lv_obj_get_user_data(item);
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            
            const auto& currentItems = manager.getCurrentItems();
            
            if (code == LV_EVENT_CLICKED) {
                if (index >= 0 && index < (int)currentItems.size()) {
                    const FileItem& fileItem = currentItems[index];
                    if (!fileItem.name.isEmpty() && !fileItem.fullPath.isEmpty()) {
                        // Clear previous selection visual feedback
                        if (manager.selectedListItem) {
                            lv_obj_set_style_bg_color(manager.selectedListItem, lv_color_white(), LV_PART_MAIN);
                        }
                        
                        // Update selection and visual feedback
                        manager.selectedItem = &fileItem;
                        manager.selectedListItem = item;
                        lv_obj_set_style_bg_color(item, lv_color_make(200, 220, 255), LV_PART_MAIN);
                        manager.updateButtonStates();
                        manager.onFileItemClicked(&fileItem);
                    }
                }
            } else if (code == LV_EVENT_LONG_PRESSED) {
                if (index >= 0 && index < (int)currentItems.size()) {
                    const FileItem& fileItem = currentItems[index];
                    if (!fileItem.name.isEmpty()) {
                        // Clear previous selection visual feedback
                        if (manager.selectedListItem) {
                            lv_obj_set_style_bg_color(manager.selectedListItem, lv_color_white(), LV_PART_MAIN);
                        }
                        
                        // Update selection and visual feedback
                        manager.selectedItem = &fileItem;
                        manager.selectedListItem = item;
                        lv_obj_set_style_bg_color(item, lv_color_make(200, 220, 255), LV_PART_MAIN);
                        manager.updateButtonStates();
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

    // Create New button with debouncing
    btnNewFolder = lv_button_create(actionPanel);
    lv_obj_t* lblNewFolder = lv_label_create(btnNewFolder);
    lv_label_set_text(lblNewFolder, "New");

    lv_obj_add_event_cb(btnNewFolder, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            unsigned long currentTime = Hardware::Device::getMillis();
            if (currentTime - g_lastNewButtonTime > BUTTON_DEBOUNCE_MS) {
                g_lastNewButtonTime = currentTime;
                FileExplorerManager::getInstance().onNewFolderClicked();
            }
        } }, LV_EVENT_CLICKED, nullptr);

    // Create other buttons
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

    // Initialize button states (disable delete and properties until item selected)
    lv_obj_add_state(btnDelete, LV_STATE_DISABLED);
    lv_obj_add_state(btnProperties, LV_STATE_DISABLED);
}

void FileExplorerManager::destroyDynamicUI() {
    // Remove event callbacks before destroying objects
    if (btnNewFolder) {
        lv_obj_remove_event_cb(btnNewFolder, nullptr);
    }
    if (btnRefresh) {
        lv_obj_remove_event_cb(btnRefresh, nullptr);
    }
    if (btnProperties) {
        lv_obj_remove_event_cb(btnProperties, nullptr);
    }
    if (btnDelete) {
        lv_obj_remove_event_cb(btnDelete, nullptr);
    }

    // Clean up modal first
    if (modalOverlay) {
        lv_obj_del(modalOverlay);
        modalOverlay = nullptr;
    }

    // Clean up main panels
    if (contentPanel) {
        lv_obj_del(contentPanel);
        contentPanel = nullptr;
    }
    if (actionPanel) {
        lv_obj_del(actionPanel);
        actionPanel = nullptr;
    }

    // Reset all pointers
    fileList = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;
    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;
    fileViewerDialog = nullptr;

    uiCreated = false;
}

bool FileExplorerManager::loadDirectory(const String& path) {
    if (!Hardware::SD::isMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    if (path.isEmpty() || path.length() > 200) {
        ESP_LOGW(TAG, "Invalid path: %s", path.c_str());
        return false;
    }

    // Check available memory
    if (ESP.getFreeHeap() < 10240) {
        ESP_LOGW(TAG, "Insufficient memory to load directory");
        state = FE_STATE_ERROR;
        return false;
    }

    state = FE_STATE_LOADING;
    clearItems();

    const char* pathStr = path.c_str();

    if (!pathStr) {
        ESP_LOGE(TAG, "path.c_str() returned NULL");
        state = FE_STATE_ERROR;
        return false;
    }

    if (strlen(pathStr) == 0 || strlen(pathStr) > 200) {
        ESP_LOGE(TAG, "Invalid path length: %d", strlen(pathStr));
        state = FE_STATE_ERROR;
        return false;
    }

    // Check if directory exists
    if (!Hardware::SD::directoryExists(pathStr)) {
        ESP_LOGE(TAG, "Directory does not exist: %s", pathStr);
        state = FE_STATE_ERROR;
        return false;
    }

    // Thread-safe callback setup
    if (!g_callbackMutex || xSemaphoreTake(g_callbackMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire callback mutex");
        state = FE_STATE_ERROR;
        return false;
    }

    g_callbackActive = false;
    Hardware::Device::delay(10);

    g_loadingPath = path;
    g_manager = this;
    g_callbackActive = true;

    xSemaphoreGive(g_callbackMutex);

    // Call the SD listing function
    bool success = false;
    try {
        success = Hardware::SD::listDirectory(pathStr, directoryListingCallback);
    } catch (...) {
        ESP_LOGE(TAG, "Exception during Hardware::SD::listDirectory()");
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
        } catch (...) {
            ESP_LOGE(TAG, "Exception while sorting directory items");
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
    selectedListItem = nullptr;
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
    if (!item || item->name.isEmpty() || item->fullPath.isEmpty()) {
        return;
    }

    if (state == FE_STATE_LOADING) {
        return;
    }

    selectedItem = item;

    if (item->isDirectory) {
        if (item->fullPath.length() > 200) {
            return;
        }

        if (ESP.getFreeHeap() < 8192) {
            return;
        }

        String targetPath = item->fullPath;
        navigateToPath(targetPath);
    } else {
        // File selected - show viewer for text files
        ESP_LOGI(TAG, "Selected file: %s", item->name.c_str());
        String fileName = item->name;
        fileName.toLowerCase();
        if (fileName.endsWith(".txt") || fileName.endsWith(".log") || fileName.endsWith(".json") || fileName.endsWith(".cfg")) {
            showFileViewer(item);
        }
    }
}

void FileExplorerManager::onFileItemDoubleClicked(const FileItem* item) {
    onFileItemClicked(item);
}

void FileExplorerManager::onBackButtonClicked() {
    // This will be handled by the event handler in UiEventHandlers.cpp
}

void FileExplorerManager::onRefreshClicked() {
    refreshCurrentDirectory();
}

void FileExplorerManager::onNewFolderClicked() {
    showCreateFolderDialog();
}

void FileExplorerManager::onDeleteClicked() {
    if (selectedItem) {
        showDeleteConfirmation(selectedItem);
    }
}

void FileExplorerManager::onPropertiesClicked() {
    if (selectedItem) {
        showProperties(selectedItem);
    }
}

// Proper dialog implementations
void FileExplorerManager::showCreateFolderDialog() {
    if (!ui_screenFileExplorer) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 128, LV_PART_MAIN);

    // Create input dialog
    inputDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(inputDialog, lv_pct(80), lv_pct(40));
    lv_obj_set_align(inputDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(inputDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(inputDialog);
    lv_label_set_text(title, "Create New Folder");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 10);

    // Text area for folder name
    lv_obj_t* textArea = lv_textarea_create(inputDialog);
    lv_obj_set_size(textArea, lv_pct(80), 40);
    lv_obj_set_align(textArea, LV_ALIGN_CENTER);
    lv_obj_set_y(textArea, -10);
    lv_textarea_set_placeholder_text(textArea, "Folder name...");

    // Button panel
    lv_obj_t* btnPanel = lv_obj_create(inputDialog);
    lv_obj_set_size(btnPanel, lv_pct(100), 50);
    lv_obj_set_align(btnPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(btnPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(btnPanel, LV_OBJ_FLAG_SCROLLABLE);

    // OK button
    lv_obj_t* btnOK = lv_button_create(btnPanel);
    lv_obj_t* lblOK = lv_label_create(btnOK);
    lv_label_set_text(lblOK, "Create");
    lv_obj_add_event_cb(btnOK, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            lv_obj_t* dialog = manager.inputDialog;
            if (dialog) {
                // Find the text area (second child of dialog)
                lv_obj_t* textArea = lv_obj_get_child(dialog, 1);
                if (textArea) {
                    const char* text = lv_textarea_get_text(textArea);
                    if (text && strlen(text) > 0) {
                        String folderName = String(text);
                        // Validate folder name
                        if (folderName.indexOf('/') >= 0 || folderName.indexOf('\\') >= 0 || 
                            folderName.indexOf(':') >= 0 || folderName.indexOf('*') >= 0 ||
                            folderName.indexOf('?') >= 0 || folderName.indexOf('"') >= 0 ||
                            folderName.indexOf('<') >= 0 || folderName.indexOf('>') >= 0 ||
                            folderName.indexOf('|') >= 0) {
                            ESP_LOGW("FileExplorerManager", "Invalid folder name: %s", folderName.c_str());
                            return; // Don't close dialog, let user fix name
                        }
                        manager.createDirectory(folderName);
                    }
                }
            }
            manager.closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);

    // Cancel button
    lv_obj_t* btnCancel = lv_button_create(btnPanel);
    lv_obj_t* lblCancel = lv_label_create(btnCancel);
    lv_label_set_text(lblCancel, "Cancel");
    lv_obj_add_event_cb(btnCancel, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::showDeleteConfirmation(const FileItem* item) {
    if (!ui_screenFileExplorer || !item) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 128, LV_PART_MAIN);

    // Create confirm dialog
    confirmDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(confirmDialog, lv_pct(80), lv_pct(35));
    lv_obj_set_align(confirmDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(confirmDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(confirmDialog);
    lv_label_set_text(title, "Confirm Delete");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 10);

    // Message
    lv_obj_t* message = lv_label_create(confirmDialog);
    String msgText = "Delete '" + item->name + "'?";
    lv_label_set_text(message, msgText.c_str());
    lv_obj_set_align(message, LV_ALIGN_CENTER);
    lv_obj_set_y(message, -10);

    // Button panel
    lv_obj_t* btnPanel = lv_obj_create(confirmDialog);
    lv_obj_set_size(btnPanel, lv_pct(100), 50);
    lv_obj_set_align(btnPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(btnPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(btnPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Store the item path for the callback
    static String itemToDelete;
    itemToDelete = item->fullPath;

    // Delete button
    lv_obj_t* btnDelete = lv_button_create(btnPanel);
    lv_obj_t* lblDelete = lv_label_create(btnDelete);
    lv_label_set_text(lblDelete, "Delete");
    lv_obj_set_style_bg_color(btnDelete, lv_color_make(255, 0, 0), LV_PART_MAIN);
    lv_obj_add_event_cb(btnDelete, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            manager.deleteItem(itemToDelete);
            manager.closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);

    // Cancel button
    lv_obj_t* btnCancel = lv_button_create(btnPanel);
    lv_obj_t* lblCancel = lv_label_create(btnCancel);
    lv_label_set_text(lblCancel, "Cancel");
    lv_obj_add_event_cb(btnCancel, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::showProperties(const FileItem* item) {
    if (!ui_screenFileExplorer || !item) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 128, LV_PART_MAIN);

    // Create properties dialog
    propertiesDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(propertiesDialog, lv_pct(85), lv_pct(50));
    lv_obj_set_align(propertiesDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(propertiesDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(propertiesDialog);
    lv_label_set_text(title, "Properties");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 10);

    // Content area
    lv_obj_t* content = lv_obj_create(propertiesDialog);
    lv_obj_set_size(content, lv_pct(90), lv_pct(60));
    lv_obj_set_align(content, LV_ALIGN_CENTER);
    lv_obj_set_y(content, -10);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Name
    lv_obj_t* lblName = lv_label_create(content);
    String nameText = "Name: " + item->name;
    lv_label_set_text(lblName, nameText.c_str());
    lv_obj_set_align(lblName, LV_ALIGN_TOP_LEFT);

    // Type
    lv_obj_t* lblType = lv_label_create(content);
    String typeText = "Type: " + String(item->isDirectory ? "Directory" : "File");
    lv_label_set_text(lblType, typeText.c_str());
    lv_obj_set_align(lblType, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblType, 25);

    // Size
    lv_obj_t* lblSize = lv_label_create(content);
    String sizeText = "Size: " + item->sizeString;
    lv_label_set_text(lblSize, sizeText.c_str());
    lv_obj_set_align(lblSize, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblSize, 50);

    // Path
    lv_obj_t* lblPath = lv_label_create(content);
    String pathText = "Path: " + item->fullPath;
    lv_label_set_text(lblPath, pathText.c_str());
    lv_obj_set_align(lblPath, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblPath, 75);

    // Close button
    lv_obj_t* btnClose = lv_button_create(propertiesDialog);
    lv_obj_t* lblClose = lv_label_create(btnClose);
    lv_label_set_text(lblClose, "Close");
    lv_obj_set_align(btnClose, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(btnClose, -10);
    lv_obj_add_event_cb(btnClose, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::showFileViewer(const FileItem* item) {
    if (!ui_screenFileExplorer || !item || item->isDirectory) {
        return;
    }

    // Read file content
    String content;
    if (!readTextFile(item->fullPath, content)) {
        ESP_LOGE(TAG, "Failed to read file: %s", item->fullPath.c_str());
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 128, LV_PART_MAIN);

    // Create file viewer dialog
    fileViewerDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(fileViewerDialog, lv_pct(90), lv_pct(80));
    lv_obj_set_align(fileViewerDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(fileViewerDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(fileViewerDialog);
    String titleText = "File: " + item->name;
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 10);

    // Content area with scrollable text
    lv_obj_t* contentArea = lv_textarea_create(fileViewerDialog);
    lv_obj_set_size(contentArea, lv_pct(90), lv_pct(75));
    lv_obj_set_align(contentArea, LV_ALIGN_CENTER);
    lv_obj_set_y(contentArea, 5);
    lv_textarea_set_text(contentArea, content.c_str());
    lv_obj_add_state(contentArea, LV_STATE_DISABLED);  // Read-only

    // Close button
    lv_obj_t* btnClose = lv_button_create(fileViewerDialog);
    lv_obj_t* lblClose = lv_label_create(btnClose);
    lv_label_set_text(lblClose, "Close");
    lv_obj_set_align(btnClose, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(btnClose, -10);
    lv_obj_add_event_cb(btnClose, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::closeDialog() {
    if (modalOverlay) {
        lv_obj_del(modalOverlay);
        modalOverlay = nullptr;
        inputDialog = nullptr;
        confirmDialog = nullptr;
        propertiesDialog = nullptr;
        fileViewerDialog = nullptr;
    }
}

void FileExplorerManager::addItem(const FileItem& item) {
    try {
        if (item.name.isEmpty()) {
            return;
        }
        currentItems.push_back(item);
    } catch (...) {
        ESP_LOGE(TAG, "Exception while adding item to list");
    }
}

}  // namespace FileExplorer
}  // namespace Application
