#include "FileExplorerManager.h"
#include "../hardware/SDManager.h"
#include "../hardware/DeviceManager.h"
#include "LVGLMessageHandler.h"
#include "../logo/LogoManager.h"
#include "../logo/LogoStorage.h"
#include "../ui/UniversalDialog.h"
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

// Static callback functions for folder creation dialog
static void folderCreationKeyboardCallback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        // User pressed the checkmark (OK) button on keyboard
        FileExplorerManager& manager = FileExplorerManager::getInstance();
        lv_obj_t* keyboard = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* textArea = lv_keyboard_get_textarea(keyboard);
        lv_obj_t* errorLabel = (lv_obj_t*)lv_obj_get_user_data(keyboard);

        if (!textArea || !errorLabel) {
            ESP_LOGE(TAG, "Could not find text area or error label from keyboard");
            return;
        }

        // Helper function to show validation errors
        auto showValidationError = [errorLabel](const char* message) {
            lv_label_set_text(errorLabel, message);
            lv_obj_remove_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);
        };

        const char* text = lv_textarea_get_text(textArea);
        if (!text || strlen(text) == 0) {
            showValidationError("Please enter a folder name");
            return;
        }

        String folderName = String(text);
        folderName.trim();

        // Validate folder name
        if (folderName.length() == 0) {
            showValidationError("Folder name cannot be empty");
            return;
        }

        if (folderName.length() > 50) {
            showValidationError("Folder name too long (max 50 characters)");
            return;
        }

        // Check for invalid characters
        const char* invalidChars = "/\\:*?\"<>|";
        for (int i = 0; invalidChars[i]; i++) {
            if (folderName.indexOf(invalidChars[i]) >= 0) {
                showValidationError("Invalid character found");
                return;
            }
        }

        // Check for reserved names
        if (folderName.equalsIgnoreCase("con") || folderName.equalsIgnoreCase("prn") ||
            folderName.equalsIgnoreCase("aux") || folderName.equalsIgnoreCase("nul")) {
            showValidationError("Reserved name not allowed");
            return;
        }

        // Validation passed - create the folder
        if (manager.createDirectory(folderName)) {
            manager.closeDialog();
        } else {
            showValidationError("Failed to create folder");
        }
    } else if (code == LV_EVENT_CANCEL) {
        // User pressed the cancel button on keyboard
        FileExplorerManager::getInstance().closeDialog();
    }
}

static void folderCreationTextAreaCallback(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* textArea = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* errorLabel = (lv_obj_t*)lv_obj_get_user_data(textArea);
        if (errorLabel) {
            lv_obj_add_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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

        // Enhance with logo information if in logos directory
        g_manager->enhanceItemWithLogoInfo(item);

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

    // Initialize state
    currentPath = "/";
    state = FE_STATE_IDLE;
    selectedItem = nullptr;
    selectedListItem = nullptr;
    persistentUICreated = false;
    currentScrollPosition = 0;
    lastSelectedItemName = "";

    // Initialize UI components to nullptr
    contentPanel = nullptr;
    fileList = nullptr;
    actionPanel = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;

    // Initialize logo-specific UI components
    logoActionPanel = nullptr;
    btnLogoAssign = nullptr;
    btnLogoFlag = nullptr;
    btnLogoVerify = nullptr;
    btnLogoPatterns = nullptr;
    btnLogoPreview = nullptr;
    btnNavigateLogos = nullptr;

    modalOverlay = nullptr;
    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;
    fileViewerDialog = nullptr;
    logoPropertiesDialog = nullptr;
    logoAssignmentDialog = nullptr;
    patternManagementDialog = nullptr;
    logoPreviewDialog = nullptr;

    // Clear navigation history
    navigationHistory.clear();

    // Create persistent UI immediately
    createPersistentUI();

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

    // Destroy persistent UI
    destroyPersistentUI();
    clearItems();
    clearNavigationHistory();

    initialized = false;
    persistentUICreated = false;

    if (g_callbackMutex) {
        vSemaphoreDelete(g_callbackMutex);  // Fixed typo: removed extra "ek"
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

    // Save current state before navigating
    if (!currentPath.isEmpty() && currentPath != path) {
        saveCurrentState();
        pushNavigationState(currentPath);
    }

    if (loadDirectory(path)) {
        currentPath = path;
        updateContent();

        // Clear selection when navigating to new directory
        selectedItem = nullptr;
        selectedListItem = nullptr;
        currentScrollPosition = 0;
        lastSelectedItemName = "";

        return true;
    }

    return false;
}

bool FileExplorerManager::navigateUp() {
    ESP_LOGI(TAG, "navigateUp() called, currentPath: %s", currentPath.c_str());

    if (currentPath == "/") {
        ESP_LOGW(TAG, "Already at root, cannot navigate up");
        return false;  // Already at root
    }

    // Find parent directory
    int lastSlash = currentPath.lastIndexOf('/');
    ESP_LOGI(TAG, "Last slash found at position: %d", lastSlash);

    if (lastSlash <= 0) {
        ESP_LOGI(TAG, "Navigating to root directory");
        return navigateToPath("/");
    }

    String parentPath = currentPath.substring(0, lastSlash);
    if (parentPath.isEmpty()) {
        parentPath = "/";
    }

    ESP_LOGI(TAG, "Navigating to parent path: %s", parentPath.c_str());
    return navigateToPath(parentPath);
}

bool FileExplorerManager::navigateToRoot() {
    return navigateToPath("/");
}

void FileExplorerManager::refreshCurrentDirectory() {
    if (loadDirectory(currentPath)) {
        updateContent();
    }
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

void FileExplorerManager::updateContent() {
    if (!initialized || !persistentUICreated) {
        return;
    }

    updatePathDisplay();
    updateSDStatus();
    updateFileList();
    updateLogoPanelVisibility();
    updateButtonStates();
}

// Legacy method for compatibility - now just calls updateContent
void FileExplorerManager::updateUI() {
    updateContent();
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

    // Update logo-specific button states
    if (isInLogosDirectory()) {
        updateLogoButtonStates();
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
        lv_obj_t* parentItem = lv_list_add_button(fileList, LV_SYMBOL_DIRECTORY, "..");
        lv_obj_set_user_data(parentItem, (void*)(-1));  // Special marker for parent directory

        // Add dedicated event callback for ".." button navigation
        lv_obj_add_event_cb(parentItem, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                FileExplorerManager& manager = FileExplorerManager::getInstance();
                ESP_LOGI("FileExplorerManager", ".. button clicked, navigating up from: %s", manager.getCurrentPath().c_str());
                
                // Clear any selection when navigating up
                if (manager.selectedListItem) {
                    lv_obj_set_style_bg_color(manager.selectedListItem, lv_color_white(), LV_PART_MAIN);
                }
                manager.selectedItem = nullptr;
                manager.selectedListItem = nullptr;
                manager.updateButtonStates();
                
                // Try history-based navigation first, fallback to regular up navigation
                if (!manager.navigateBack()) {
                    manager.navigateUp();
                }
            } }, LV_EVENT_CLICKED, nullptr);
    }

    // Add directory items first, then files
    for (size_t i = 0; i < currentItems.size(); i++) {
        const FileItem& item = currentItems[i];

        // Use logo-aware icon and display text
        const char* icon = getLogoIcon(item);
        String displayText = getLogoDisplayText(item);

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

void FileExplorerManager::createPersistentUI() {
    if (!ui_screenFileExplorer) {
        ESP_LOGW(TAG, "File explorer screen not available");
        return;
    }

    if (persistentUICreated) {
        ESP_LOGW(TAG, "Persistent UI already created");
        return;
    }

    ESP_LOGI(TAG, "Creating persistent File Explorer UI");

    createBaseLayout();
    createActionPanels();
    setupEventHandlers();

    persistentUICreated = true;
    ESP_LOGI(TAG, "Persistent File Explorer UI created successfully");
}

void FileExplorerManager::createBaseLayout() {
    // Create main content panel (adjusting for two potential action panels)
    contentPanel = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_width(contentPanel, lv_pct(100));
    lv_obj_set_height(contentPanel, lv_pct(75));  // Reduced from 60% to make room for two action panels
    lv_obj_set_align(contentPanel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(contentPanel, 0);
    lv_obj_remove_flag(contentPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Create file list
    fileList = lv_list_create(contentPanel);
    lv_obj_set_size(fileList, lv_pct(100), lv_pct(100));
    lv_obj_set_align(fileList, LV_ALIGN_CENTER);
}

void FileExplorerManager::createActionPanels() {
    // Create main action panel (positioned above logo panel)
    actionPanel = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_width(actionPanel, lv_pct(100));
    lv_obj_set_height(actionPanel, 50);  // Fixed height instead of percentage
    lv_obj_set_align(actionPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(actionPanel, -60);  // Leave space for logo panel below
    lv_obj_set_flex_flow(actionPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(actionPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Create main action buttons
    btnNewFolder = lv_button_create(actionPanel);
    lv_obj_t* lblNewFolder = lv_label_create(btnNewFolder);
    lv_label_set_text(lblNewFolder, "New");

    btnRefresh = lv_button_create(actionPanel);
    lv_obj_t* lblRefresh = lv_label_create(btnRefresh);
    lv_label_set_text(lblRefresh, LV_SYMBOL_REFRESH);

    btnProperties = lv_button_create(actionPanel);
    lv_obj_t* lblProperties = lv_label_create(btnProperties);
    lv_label_set_text(lblProperties, "Info");

    btnDelete = lv_button_create(actionPanel);
    lv_obj_t* lblDelete = lv_label_create(btnDelete);
    lv_label_set_text(lblDelete, LV_SYMBOL_TRASH);

    btnNavigateLogos = lv_button_create(actionPanel);
    lv_obj_t* lblNavigateLogos = lv_label_create(btnNavigateLogos);
    lv_label_set_text(lblNavigateLogos, "Logos");

    // Initialize button states (disable delete and properties until item selected)
    lv_obj_add_state(btnDelete, LV_STATE_DISABLED);
    lv_obj_add_state(btnProperties, LV_STATE_DISABLED);

    // Create logo-specific buttons
    createLogoSpecificButtons();
}

void FileExplorerManager::setupEventHandlers() {
    // Setup event handlers for main action buttons
    lv_obj_add_event_cb(btnNewFolder, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            unsigned long currentTime = Hardware::Device::getMillis();
            if (currentTime - g_lastNewButtonTime > BUTTON_DEBOUNCE_MS) {
                g_lastNewButtonTime = currentTime;
                FileExplorerManager::getInstance().onNewFolderClicked();
            }
        } }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(btnRefresh, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onRefreshClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(btnProperties, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onPropertiesClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(btnDelete, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onDeleteClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(btnNavigateLogos, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().navigateToLogosRoot();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::destroyPersistentUI() {
    if (!persistentUICreated) {
        return;
    }

    ESP_LOGI(TAG, "Destroying persistent File Explorer UI");

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
    if (btnNavigateLogos) {
        lv_obj_remove_event_cb(btnNavigateLogos, nullptr);
    }

    // Remove logo-specific event callbacks
    if (btnLogoAssign) {
        lv_obj_remove_event_cb(btnLogoAssign, nullptr);
    }
    if (btnLogoFlag) {
        lv_obj_remove_event_cb(btnLogoFlag, nullptr);
    }
    if (btnLogoVerify) {
        lv_obj_remove_event_cb(btnLogoVerify, nullptr);
    }
    if (btnLogoPatterns) {
        lv_obj_remove_event_cb(btnLogoPatterns, nullptr);
    }
    if (btnLogoPreview) {
        lv_obj_remove_event_cb(btnLogoPreview, nullptr);
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

    // Clean up logo-specific panels
    if (logoActionPanel) {
        lv_obj_del(logoActionPanel);
        logoActionPanel = nullptr;
    }

    // Reset all pointers
    fileList = nullptr;
    btnNewFolder = nullptr;
    btnRefresh = nullptr;
    btnProperties = nullptr;
    btnDelete = nullptr;
    btnNavigateLogos = nullptr;

    // Reset logo-specific pointers
    btnLogoAssign = nullptr;
    btnLogoFlag = nullptr;
    btnLogoVerify = nullptr;
    btnLogoPatterns = nullptr;
    btnLogoPreview = nullptr;

    inputDialog = nullptr;
    confirmDialog = nullptr;
    propertiesDialog = nullptr;
    fileViewerDialog = nullptr;
    logoPropertiesDialog = nullptr;
    logoAssignmentDialog = nullptr;
    patternManagementDialog = nullptr;
    logoPreviewDialog = nullptr;

    persistentUICreated = false;
    ESP_LOGI(TAG, "Persistent File Explorer UI destroyed");
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
    // Use the full dialog with virtual keyboard
    showCreateFolderDialog();
}

void FileExplorerManager::onDeleteClicked() {
    if (selectedItem) {
        showDeleteConfirmation(selectedItem);
    }
}

void FileExplorerManager::onPropertiesClicked() {
    if (selectedItem) {
        // Show logo-specific properties if it's a logo file, otherwise show regular properties
        if (selectedItem->isLogoFile || selectedItem->isLogoMetadata) {
            showLogoProperties(selectedItem);
        } else {
            showProperties(selectedItem);
        }
    }
}

// Modern folder creation dialog
void FileExplorerManager::showCreateFolderDialog() {
    if (!ui_screenFileExplorer) {
        ESP_LOGW(TAG, "Cannot show folder dialog: ui_screenFileExplorer is null");
        return;
    }

    ESP_LOGW(TAG, "Opening folder creation dialog");

    // Create modal overlay with smooth fade effect
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 160, LV_PART_MAIN);
    lv_obj_set_style_radius(modalOverlay, 0, LV_PART_MAIN);

    // Create main dialog container with modern styling
    inputDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(inputDialog, lv_pct(92), lv_pct(85));
    lv_obj_set_align(inputDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(inputDialog, lv_color_make(248, 249, 250), LV_PART_MAIN);
    lv_obj_set_style_border_width(inputDialog, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(inputDialog, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(inputDialog, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(inputDialog, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(inputDialog, 80, LV_PART_MAIN);
    lv_obj_set_style_pad_all(inputDialog, 20, LV_PART_MAIN);

    // Header section with icon and title
    lv_obj_t* headerSection = lv_obj_create(inputDialog);
    lv_obj_set_size(headerSection, lv_pct(100), 80);
    lv_obj_set_align(headerSection, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_opa(headerSection, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(headerSection, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(headerSection, 0, LV_PART_MAIN);

    // Folder icon
    lv_obj_t* folderIcon = lv_label_create(headerSection);
    lv_label_set_text(folderIcon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_color(folderIcon, lv_color_make(52, 152, 219), LV_PART_MAIN);
    lv_obj_set_style_text_font(folderIcon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_align(folderIcon, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(folderIcon, 10);

    // Title with better typography
    lv_obj_t* title = lv_label_create(headerSection);
    lv_label_set_text(title, "Create New Folder");
    lv_obj_set_style_text_color(title, lv_color_make(44, 62, 80), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_align(title, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(title, 35, 12);

    // Subtitle/instruction
    lv_obj_t* subtitle = lv_label_create(headerSection);
    lv_label_set_text(subtitle, "Enter a name for the new folder");
    lv_obj_set_style_text_color(subtitle, lv_color_make(127, 140, 141), LV_PART_MAIN);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_align(subtitle, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(subtitle, 35, 38);

    // Input section with label
    lv_obj_t* inputSection = lv_obj_create(inputDialog);
    lv_obj_set_size(inputSection, lv_pct(100), 80);
    lv_obj_set_align(inputSection, LV_ALIGN_TOP_MID);
    lv_obj_set_y(inputSection, 90);
    lv_obj_set_style_bg_opa(inputSection, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(inputSection, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(inputSection, 0, LV_PART_MAIN);

    // Input label
    lv_obj_t* inputLabel = lv_label_create(inputSection);
    lv_label_set_text(inputLabel, "Folder Name:");
    lv_obj_set_style_text_color(inputLabel, lv_color_make(52, 73, 94), LV_PART_MAIN);
    lv_obj_set_style_text_font(inputLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    // lv_obj_set_align(inputLabel, LV_ALIGN_TOP_LEFT);

    // Modern text area with better styling
    lv_obj_t* textArea = lv_textarea_create(inputSection);
    lv_obj_set_size(textArea, lv_pct(100), 50);
    lv_obj_set_align(textArea, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(textArea, 25);
    lv_textarea_set_placeholder_text(textArea, "My Folder");
    lv_textarea_set_one_line(textArea, true);

    // Enhanced text area styling
    lv_obj_set_style_bg_color(textArea, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(textArea, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(textArea, lv_color_make(189, 195, 199), LV_PART_MAIN);
    lv_obj_set_style_border_color(textArea, lv_color_make(52, 152, 219), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(textArea, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(textArea, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(textArea, lv_color_make(44, 62, 80), LV_PART_MAIN);

    // Error message label (initially hidden)
    lv_obj_t* errorLabel = lv_label_create(inputSection);
    lv_label_set_text(errorLabel, "");
    lv_obj_set_style_text_color(errorLabel, lv_color_make(231, 76, 60), LV_PART_MAIN);
    lv_obj_set_style_text_font(errorLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_align(errorLabel, LV_ALIGN_TOP_RIGHT);
    // lv_obj_set_y(errorLabel, 80);
    lv_obj_add_flag(errorLabel, LV_OBJ_FLAG_HIDDEN);

    // Virtual keyboard with built-in action buttons
    lv_obj_t* keyboard = lv_keyboard_create(inputDialog);
    lv_obj_set_size(keyboard, lv_pct(100), lv_pct(50));  // Give keyboard more space
    lv_obj_set_align(keyboard, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(keyboard, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(keyboard, lv_color_make(229, 231, 235), LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard, 8, LV_PART_MAIN);

    // Associate keyboard with text area
    lv_keyboard_set_textarea(keyboard, textArea);

    // Store error label in user data for callback access
    lv_obj_set_user_data(keyboard, errorLabel);
    lv_obj_set_user_data(textArea, errorLabel);

    // Handle keyboard events (built-in OK/Cancel buttons)
    lv_obj_add_event_cb(keyboard, folderCreationKeyboardCallback, LV_EVENT_ALL, nullptr);

    // Clear error message when user starts typing
    lv_obj_add_event_cb(textArea, folderCreationTextAreaCallback, LV_EVENT_VALUE_CHANGED, nullptr);

    // Focus the text area and select all text for easy replacement
    lv_obj_add_state(textArea, LV_STATE_FOCUSED);
    lv_textarea_set_cursor_pos(textArea, 0);
}

void FileExplorerManager::showDeleteConfirmation(const FileItem* item) {
    if (!ui_screenFileExplorer || !item) {
        return;
    }

    // Store the item path for the callback
    static String itemToDelete;
    itemToDelete = item->fullPath;

    // Use Universal Dialog system for consistent UI
    String message = "Delete '" + item->name + "'?\n\nThis action cannot be undone.";

    UI::Dialog::UniversalDialog::showConfirm(
        "Confirm Delete",
        message,
        []() {
            // Delete confirmed
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            manager.deleteItem(itemToDelete);
        },
        nullptr,  // No special action on cancel
        UI::Dialog::DialogSize::MEDIUM);
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

// =============================================================================
// LOGO-SPECIFIC FUNCTIONALITY
// =============================================================================

bool FileExplorerManager::navigateToLogosRoot() {
    return navigateToPath("/logos/files");
}

bool FileExplorerManager::isInLogosDirectory() const {
    return isLogoDirectory(currentPath);
}

bool FileExplorerManager::isLogoDirectory(const String& path) const {
    // Show logo panel in logos directory and its subdirectories,
    // or when logo files are present in any directory
    return path.startsWith("/logos") || path.contains("logo") ||
           (currentItems.size() > 0 && std::any_of(currentItems.begin(), currentItems.end(),
                                                   [](const FileItem& item) {
                                                       return item.name.endsWith(".bin") || item.name.endsWith(".png") ||
                                                              item.name.toLowerCase().contains("logo");
                                                   }));
}

String FileExplorerManager::extractProcessNameFromLogoFile(const String& filename) const {
    if (filename.endsWith(".bin") || filename.endsWith(".png")) {
        // New system: try to find matching process mapping
        Logo::LogoStorage& storage = Logo::LogoStorage::getInstance();

        // Check all mapped processes to find one that maps to this file
        std::vector<String> processes = storage.listMappedProcesses();
        for (const String& processName : processes) {
            String mappedFile = storage.getProcessMapping(processName);
            if (mappedFile == filename) {
                return processName;
            }
        }

        // Fallback: try to extract from filename (remove extension and _vX suffixes)
        String baseName = filename;
        if (baseName.endsWith(".bin")) {
            baseName = baseName.substring(0, baseName.length() - 4);  // Remove .bin
        } else if (baseName.endsWith(".png")) {
            baseName = baseName.substring(0, baseName.length() - 4);  // Remove .png
        }

        // Remove version suffixes (_v1, _v2, etc.)
        int versionPos = baseName.lastIndexOf("_v");
        if (versionPos > 0) {
            baseName = baseName.substring(0, versionPos);
        }

        // Add .exe extension if not present
        if (!baseName.endsWith(".exe") && !baseName.endsWith(".app")) {
            baseName += ".exe";
        }

        return baseName;
    }
    return "";
}

void FileExplorerManager::enhanceItemWithLogoInfo(FileItem& item) {
    // Initialize logo flags
    item.isLogoFile = false;
    item.isLogoMetadata = false;
    item.hasLogoMetadata = false;
    item.processNameFromFile = "";

    if (item.isDirectory || !isInLogosDirectory()) {
        return;
    }

    // Check if it's a logo file (any .bin or .png file in logos directory)
    if (item.name.endsWith(".bin") || item.name.endsWith(".png")) {
        item.isLogoFile = true;

        // Extract process name from filename or mapping
        item.processNameFromFile = extractProcessNameFromLogoFile(item.name);

        if (!item.processNameFromFile.isEmpty()) {
            // Get logo info from new LogoManager
            auto logoInfo = Logo::LogoManager::getInstance().getLogoInfo(item.processNameFromFile.c_str());

            if (!logoInfo.processName.isEmpty()) {
                item.hasLogoMetadata = true;
                item.logoVerified = logoInfo.verified;
                item.logoFlagged = logoInfo.flagged;
            }
        }
    }

    // Check for metadata files (.json files in metadata subdirectories)
    if (item.name.endsWith(".json") && currentPath.startsWith("/logos/metadata")) {
        item.isLogoMetadata = true;
        // Extract process name from json filename
        String processName = item.name.substring(0, item.name.length() - 5);  // Remove .json
        item.processNameFromFile = processName;
    }
}

String FileExplorerManager::getLogoDisplayText(const FileItem& item) {
    String displayText = item.name;

    if (item.isLogoFile) {
        displayText += " (" + item.sizeString + ")";

        if (item.hasLogoMetadata) {
            // Add simplified flags
            String flags = " [";
            bool hasFlag = false;

            if (item.logoVerified) {
                flags += "V";  // Use 'V' instead of checkmark
                hasFlag = true;
            }
            if (item.logoFlagged) {
                if (hasFlag) flags += ",";
                flags += "F";  // Use 'F' instead of X mark
                hasFlag = true;
            }

            flags += "]";

            if (hasFlag) {
                displayText += flags;
            }
        }
    } else if (!item.isDirectory) {
        displayText += " (" + item.sizeString + ")";
    }

    return displayText;
}

const char* FileExplorerManager::getLogoIcon(const FileItem& item) {
    if (item.isDirectory) {
        return LV_SYMBOL_DIRECTORY;
    } else if (item.isLogoFile) {
        if (item.logoVerified) {
            return LV_SYMBOL_OK;  // Verified logo
        } else if (item.logoFlagged) {
            return LV_SYMBOL_WARNING;  // Flagged as incorrect
        } else if (item.name.endsWith(".png")) {
            return LV_SYMBOL_IMAGE;  // PNG image file
        } else {
            return LV_SYMBOL_FILE;  // Binary logo file
        }
    } else {
        return LV_SYMBOL_FILE;  // Regular file
    }
}

// Logo-specific operations
bool FileExplorerManager::assignLogoToProcess(const String& logoFileName, const String& processName) {
    if (!Logo::LogoManager::getInstance().isInitialized()) {
        return false;
    }

    String logoProcessName = extractProcessNameFromLogoFile(logoFileName);
    if (logoProcessName.isEmpty()) {
        return false;
    }

    // For the new system, this would be copying the logo file with a new process name
    // For now, just return true as the basic save/load functionality handles assignment
    ESP_LOGI(TAG, "Logo assignment noted: %s -> %s", processName.c_str(), logoProcessName.c_str());
    refreshCurrentDirectory();  // Refresh to show updated flags

    return true;
}

bool FileExplorerManager::flagLogoIncorrect(const String& logoFileName, bool incorrect) {
    if (!Logo::LogoManager::getInstance().isInitialized()) {
        return false;
    }

    String processName = extractProcessNameFromLogoFile(logoFileName);
    if (processName.isEmpty()) {
        return false;
    }

    bool success = Logo::LogoManager::getInstance().flagAsIncorrect(processName.c_str(), incorrect);

    if (success) {
        ESP_LOGI(TAG, "Logo flagged as %s: %s", incorrect ? "incorrect" : "correct", processName.c_str());
        refreshCurrentDirectory();  // Refresh to show updated flags
    }

    return success;
}

bool FileExplorerManager::markLogoVerified(const String& logoFileName, bool verified) {
    if (!Logo::LogoManager::getInstance().isInitialized()) {
        return false;
    }

    String processName = extractProcessNameFromLogoFile(logoFileName);
    if (processName.isEmpty()) {
        return false;
    }

    bool success = Logo::LogoManager::getInstance().markAsVerified(processName.c_str(), verified);

    if (success) {
        ESP_LOGI(TAG, "Logo marked as %s: %s", verified ? "verified" : "unverified", processName.c_str());
        refreshCurrentDirectory();  // Refresh to show updated flags
    }

    return success;
}

bool FileExplorerManager::addLogoPattern(const String& logoFileName, const String& pattern) {
    if (!Logo::LogoManager::getInstance().isInitialized()) {
        return false;
    }

    String processName = extractProcessNameFromLogoFile(logoFileName);
    if (processName.isEmpty()) {
        return false;
    }

    // Pattern management is simplified in the new system
    // For now, just log the pattern - can be extended later
    ESP_LOGI(TAG, "Pattern noted for %s: %s", processName.c_str(), pattern.c_str());

    return true;
}

bool FileExplorerManager::deleteLogoAndMetadata(const String& logoFileName) {
    if (!Logo::LogoManager::getInstance().isInitialized()) {
        return false;
    }

    String processName = extractProcessNameFromLogoFile(logoFileName);
    if (processName.isEmpty()) {
        return false;
    }

    bool success = Logo::LogoManager::getInstance().deleteLogo(processName.c_str());

    if (success) {
        ESP_LOGI(TAG, "Logo deleted: %s", processName.c_str());
        refreshCurrentDirectory();  // Refresh to remove deleted items
    }

    return success;
}

// Logo-specific event handlers
void FileExplorerManager::onLogoAssignClicked() {
    if (selectedItem && (selectedItem->isLogoFile || selectedItem->isLogoMetadata)) {
        showLogoAssignmentDialog(selectedItem);
    }
}

void FileExplorerManager::onLogoFlagClicked() {
    if (selectedItem && selectedItem->isLogoFile) {
        // Toggle incorrect flag
        bool currentlyIncorrect = selectedItem->logoFlagged;
        flagLogoIncorrect(selectedItem->name, !currentlyIncorrect);
    }
}

void FileExplorerManager::onLogoVerifyClicked() {
    if (selectedItem && selectedItem->isLogoFile) {
        // Toggle verified flag
        bool currentlyVerified = selectedItem->logoVerified;
        markLogoVerified(selectedItem->name, !currentlyVerified);
    }
}

void FileExplorerManager::onLogoPatternsClicked() {
    if (selectedItem && (selectedItem->isLogoFile || selectedItem->isLogoMetadata)) {
        showPatternManagementDialog(selectedItem);
    }
}

void FileExplorerManager::onLogoPreviewClicked() {
    if (selectedItem && selectedItem->isLogoFile) {
        showLogoPreview(selectedItem);
    }
}

// Logo-specific UI creation
void FileExplorerManager::createLogoSpecificButtons() {
    if (!ui_screenFileExplorer) {
        return;
    }

    // Create logo action panel (positioned at the very bottom)
    logoActionPanel = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_width(logoActionPanel, lv_pct(100));
    lv_obj_set_height(logoActionPanel, 50);  // Fixed height to match main action panel
    lv_obj_set_align(logoActionPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(logoActionPanel, 0);  // At the very bottom
    lv_obj_set_flex_flow(logoActionPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(logoActionPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(logoActionPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Initially hide the logo action panel - it will be shown when in logos directory
    lv_obj_add_flag(logoActionPanel, LV_OBJ_FLAG_HIDDEN);

    // Quick navigation to logos root
    btnNavigateLogos = lv_button_create(logoActionPanel);
    lv_obj_t* lblNavigateLogos = lv_label_create(btnNavigateLogos);
    lv_label_set_text(lblNavigateLogos, "LOGOS");  // Use text instead of unsupported emoji
    lv_obj_add_event_cb(btnNavigateLogos, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().navigateToLogosRoot();
        } }, LV_EVENT_CLICKED, nullptr);

    // Assign logo button
    btnLogoAssign = lv_button_create(logoActionPanel);
    lv_obj_t* lblLogoAssign = lv_label_create(btnLogoAssign);
    lv_label_set_text(lblLogoAssign, "Assign");
    lv_obj_add_event_cb(btnLogoAssign, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onLogoAssignClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    // Flag logo button
    btnLogoFlag = lv_button_create(logoActionPanel);
    lv_obj_t* lblLogoFlag = lv_label_create(btnLogoFlag);
    lv_label_set_text(lblLogoFlag, "Flag");
    lv_obj_add_event_cb(btnLogoFlag, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onLogoFlagClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    // Verify logo button
    btnLogoVerify = lv_button_create(logoActionPanel);
    lv_obj_t* lblLogoVerify = lv_label_create(btnLogoVerify);
    lv_label_set_text(lblLogoVerify, "Verify");
    lv_obj_add_event_cb(btnLogoVerify, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onLogoVerifyClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    // Patterns button
    btnLogoPatterns = lv_button_create(logoActionPanel);
    lv_obj_t* lblLogoPatterns = lv_label_create(btnLogoPatterns);
    lv_label_set_text(lblLogoPatterns, "Patterns");
    lv_obj_add_event_cb(btnLogoPatterns, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onLogoPatternsClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    // Preview button
    btnLogoPreview = lv_button_create(logoActionPanel);
    lv_obj_t* lblLogoPreview = lv_label_create(btnLogoPreview);
    lv_label_set_text(lblLogoPreview, "Preview");
    lv_obj_add_event_cb(btnLogoPreview, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().onLogoPreviewClicked();
        } }, LV_EVENT_CLICKED, nullptr);

    // Initially disable logo-specific buttons until logo item is selected
    updateLogoButtonStates();
}

void FileExplorerManager::updateLogoButtonStates() {
    if (!logoActionPanel) {
        return;
    }

    bool logoItemSelected = selectedItem &&
                            (selectedItem->isLogoFile || selectedItem->isLogoMetadata);

    // Enable/disable buttons based on selection
    if (btnLogoAssign) {
        if (logoItemSelected) {
            lv_obj_remove_state(btnLogoAssign, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btnLogoAssign, LV_STATE_DISABLED);
        }
    }

    if (btnLogoFlag) {
        if (logoItemSelected) {
            lv_obj_remove_state(btnLogoFlag, LV_STATE_DISABLED);
            // Update button text based on current flag state
            lv_obj_t* label = lv_obj_get_child(btnLogoFlag, 0);
            if (selectedItem->logoFlagged) {
                lv_label_set_text(label, "Unflag");
            } else {
                lv_label_set_text(label, "Flag");
            }
        } else {
            lv_obj_add_state(btnLogoFlag, LV_STATE_DISABLED);
        }
    }

    if (btnLogoVerify) {
        if (logoItemSelected) {
            lv_obj_remove_state(btnLogoVerify, LV_STATE_DISABLED);
            // Update button text based on current verification state
            lv_obj_t* label = lv_obj_get_child(btnLogoVerify, 0);
            if (selectedItem->logoVerified) {
                lv_label_set_text(label, "Unverify");
            } else {
                lv_label_set_text(label, "Verify");
            }
        } else {
            lv_obj_add_state(btnLogoVerify, LV_STATE_DISABLED);
        }
    }

    if (btnLogoPatterns) {
        if (logoItemSelected) {
            lv_obj_remove_state(btnLogoPatterns, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btnLogoPatterns, LV_STATE_DISABLED);
        }
    }

    if (btnLogoPreview) {
        if (logoItemSelected && selectedItem->isLogoFile) {
            lv_obj_remove_state(btnLogoPreview, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btnLogoPreview, LV_STATE_DISABLED);
        }
    }
}

void FileExplorerManager::updateLogoPanelVisibility() {
    if (!logoActionPanel) {
        ESP_LOGW(TAG, "Logo action panel not created yet");
        return;
    }

    // Show/hide logo action panel based on current directory
    if (isInLogosDirectory()) {
        lv_obj_remove_flag(logoActionPanel, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Showing logo action panel for logos directory: %s", currentPath.c_str());

        // Adjust main action panel position when logo panel is visible
        if (actionPanel) {
            lv_obj_set_y(actionPanel, -60);  // Move up to make room for logo panel
        }

        // Adjust content panel to account for two action panels
        if (contentPanel) {
            lv_obj_set_height(contentPanel, lv_pct(70));  // Smaller to accommodate both panels
        }
    } else {
        lv_obj_add_flag(logoActionPanel, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Hiding logo action panel for non-logos directory: %s", currentPath.c_str());

        // Reset main action panel position when logo panel is hidden
        if (actionPanel) {
            lv_obj_set_y(actionPanel, -10);  // Move closer to bottom edge
        }

        // Expand content panel when logo panel is hidden
        if (contentPanel) {
            lv_obj_set_height(contentPanel, lv_pct(80));  // Larger when only one action panel
        }
    }
}

// Logo-specific dialogs
void FileExplorerManager::showLogoProperties(const FileItem* item) {
    if (!ui_screenFileExplorer || !item || (!item->isLogoFile && !item->isLogoMetadata)) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 128, LV_PART_MAIN);

    // Create logo properties dialog
    logoPropertiesDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(logoPropertiesDialog, lv_pct(90), lv_pct(70));
    lv_obj_set_align(logoPropertiesDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(logoPropertiesDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(logoPropertiesDialog);
    lv_label_set_text(title, "Logo Properties");
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 10);

    // Scrollable content area
    lv_obj_t* content = lv_obj_create(logoPropertiesDialog);
    lv_obj_set_size(content, lv_pct(90), lv_pct(75));
    lv_obj_set_align(content, LV_ALIGN_CENTER);
    lv_obj_set_y(content, -5);

    int yPos = 0;
    const int lineHeight = 25;

    // Basic file info
    lv_obj_t* lblName = lv_label_create(content);
    String nameText = "Name: " + item->name;
    lv_label_set_text(lblName, nameText.c_str());
    lv_obj_set_align(lblName, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblName, yPos);
    yPos += lineHeight;

    lv_obj_t* lblType = lv_label_create(content);
    String typeText = "Type: ";
    if (item->isLogoFile) {
        if (item->name.endsWith(".png")) {
            typeText += "Logo PNG";
        } else if (item->name.endsWith(".bin")) {
            typeText += "Logo Binary";
        } else {
            typeText += "Logo File";
        }
    } else {
        typeText += "Logo Metadata";
    }
    lv_label_set_text(lblType, typeText.c_str());
    lv_obj_set_align(lblType, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblType, yPos);
    yPos += lineHeight;

    lv_obj_t* lblSize = lv_label_create(content);
    String sizeText = "Size: " + item->sizeString;
    lv_label_set_text(lblSize, sizeText.c_str());
    lv_obj_set_align(lblSize, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblSize, yPos);
    yPos += lineHeight;

    lv_obj_t* lblProcess = lv_label_create(content);
    String processText = "Process: " + item->processNameFromFile;
    lv_label_set_text(lblProcess, processText.c_str());
    lv_obj_set_align(lblProcess, LV_ALIGN_TOP_LEFT);
    lv_obj_set_y(lblProcess, yPos);
    yPos += lineHeight;

    // Logo metadata (simplified)
    if (item->hasLogoMetadata) {
        yPos += 5;  // spacing

        lv_obj_t* lblMetaTitle = lv_label_create(content);
        lv_label_set_text(lblMetaTitle, "--- Logo Status ---");
        lv_obj_set_align(lblMetaTitle, LV_ALIGN_TOP_LEFT);
        lv_obj_set_y(lblMetaTitle, yPos);
        yPos += lineHeight;

        // Simplified flags display
        lv_obj_t* lblFlags = lv_label_create(content);
        String flagsText = "Status: ";
        if (item->logoVerified) {
            flagsText += "Verified ";
        }
        if (item->logoFlagged) {
            flagsText += "Flagged ";
        }
        if (!item->logoVerified && !item->logoFlagged) {
            flagsText += "Unverified ";
        }
        lv_label_set_text(lblFlags, flagsText.c_str());
        lv_obj_set_align(lblFlags, LV_ALIGN_TOP_LEFT);
        lv_obj_set_y(lblFlags, yPos);
        yPos += lineHeight;

        // Format info (detect from file extension)
        lv_obj_t* lblFormat = lv_label_create(content);
        String formatText = "Format: ";
        if (item->name.endsWith(".png")) {
            formatText += "PNG Image";
        } else if (item->name.endsWith(".bin")) {
            formatText += "LVGL Binary";
        } else {
            formatText += "Unknown";
        }
        lv_label_set_text(lblFormat, formatText.c_str());
        lv_obj_set_align(lblFormat, LV_ALIGN_TOP_LEFT);
        lv_obj_set_y(lblFormat, yPos);
        yPos += lineHeight;
    }

    // Close button
    lv_obj_t* btnClose = lv_button_create(logoPropertiesDialog);
    lv_obj_t* lblClose = lv_label_create(btnClose);
    lv_label_set_text(lblClose, "Close");
    lv_obj_set_align(btnClose, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(btnClose, -10);
    lv_obj_add_event_cb(btnClose, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);
}

void FileExplorerManager::showLogoAssignmentDialog(const FileItem* item) {
    if (!ui_screenFileExplorer || !item || (!item->isLogoFile && !item->isLogoMetadata)) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 160, LV_PART_MAIN);

    // Create assignment dialog
    logoAssignmentDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(logoAssignmentDialog, lv_pct(85), lv_pct(60));
    lv_obj_set_align(logoAssignmentDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(logoAssignmentDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(logoAssignmentDialog);
    String titleText = "Assign Logo: " + item->processNameFromFile;
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 15);

    // Instructions
    lv_obj_t* instructions = lv_label_create(logoAssignmentDialog);
    lv_label_set_text(instructions, "Enter process name to assign this logo to:");
    lv_obj_set_align(instructions, LV_ALIGN_TOP_MID);
    lv_obj_set_y(instructions, 45);

    // Text area for process name input
    lv_obj_t* textArea = lv_textarea_create(logoAssignmentDialog);
    lv_obj_set_size(textArea, lv_pct(80), 50);
    lv_obj_set_align(textArea, LV_ALIGN_CENTER);
    lv_obj_set_y(textArea, -20);
    lv_textarea_set_placeholder_text(textArea, "Enter process name (e.g., chrome.exe)");
    lv_textarea_set_one_line(textArea, true);

    // Button panel
    lv_obj_t* btnPanel = lv_obj_create(logoAssignmentDialog);
    lv_obj_set_size(btnPanel, lv_pct(100), 60);
    lv_obj_set_align(btnPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(btnPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(btnPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Store item info for callback
    static String itemNameForAssignment;
    itemNameForAssignment = item->name;

    // Assign button
    lv_obj_t* btnAssign = lv_button_create(btnPanel);
    lv_obj_t* lblAssign = lv_label_create(btnAssign);
    lv_label_set_text(lblAssign, "Assign");
    lv_obj_set_user_data(btnAssign, textArea);  // Store text area reference
    lv_obj_add_event_cb(btnAssign, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_obj_t* textArea = (lv_obj_t*)lv_obj_get_user_data(( lv_obj_t*)lv_event_get_target(e));
            const char* processName = lv_textarea_get_text(textArea);
            
            if (processName && strlen(processName) > 0) {
                FileExplorerManager& manager = FileExplorerManager::getInstance();
                if (manager.assignLogoToProcess(itemNameForAssignment, String(processName))) {
                    ESP_LOGI("FileExplorer", "Logo assignment successful");
                } else {
                    ESP_LOGE("FileExplorer", "Logo assignment failed");
                }
                manager.closeDialog();
            }
        } }, LV_EVENT_CLICKED, nullptr);

    // Cancel button
    lv_obj_t* btnCancel = lv_button_create(btnPanel);
    lv_obj_t* lblCancel = lv_label_create(btnCancel);
    lv_label_set_text(lblCancel, "Cancel");
    lv_obj_add_event_cb(btnCancel, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);

    // Focus the text area
    lv_obj_add_state(textArea, LV_STATE_FOCUSED);
}

void FileExplorerManager::showPatternManagementDialog(const FileItem* item) {
    if (!ui_screenFileExplorer || !item || (!item->isLogoFile && !item->isLogoMetadata)) {
        return;
    }

    // Create modal overlay
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 160, LV_PART_MAIN);

    // Create pattern management dialog
    patternManagementDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(patternManagementDialog, lv_pct(90), lv_pct(75));
    lv_obj_set_align(patternManagementDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(patternManagementDialog, lv_color_white(), LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(patternManagementDialog);
    String titleText = "Manage Patterns: " + item->processNameFromFile;
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(title, 15);

    // Current patterns display
    lv_obj_t* currentLabel = lv_label_create(patternManagementDialog);
    lv_label_set_text(currentLabel, "Current Patterns:");
    lv_obj_set_align(currentLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(currentLabel, 20, 50);

    lv_obj_t* patternsDisplay = lv_textarea_create(patternManagementDialog);
    lv_obj_set_size(patternsDisplay, lv_pct(85), 80);
    lv_obj_set_align(patternsDisplay, LV_ALIGN_TOP_MID);
    lv_obj_set_y(patternsDisplay, 75);

    String currentPatterns = "";  // Patterns simplified in new system
    lv_textarea_set_text(patternsDisplay, currentPatterns.c_str());
    lv_obj_add_state(patternsDisplay, LV_STATE_DISABLED);  // Read-only

    // New pattern input
    lv_obj_t* newLabel = lv_label_create(patternManagementDialog);
    lv_label_set_text(newLabel, "Add New Pattern:");
    lv_obj_set_align(newLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(newLabel, 20, 170);

    lv_obj_t* newPatternInput = lv_textarea_create(patternManagementDialog);
    lv_obj_set_size(newPatternInput, lv_pct(85), 50);
    lv_obj_set_align(newPatternInput, LV_ALIGN_TOP_MID);
    lv_obj_set_y(newPatternInput, 195);
    lv_textarea_set_placeholder_text(newPatternInput, "Enter regex pattern (e.g., chrome.*|google.*chrome)");
    lv_textarea_set_one_line(newPatternInput, true);

    // Button panel
    lv_obj_t* btnPanel = lv_obj_create(patternManagementDialog);
    lv_obj_set_size(btnPanel, lv_pct(100), 60);
    lv_obj_set_align(btnPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(btnPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(btnPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Store item info for callback
    static String itemNameForPatterns;
    itemNameForPatterns = item->name;

    // Add Pattern button
    lv_obj_t* btnAdd = lv_button_create(btnPanel);
    lv_obj_t* lblAdd = lv_label_create(btnAdd);
    lv_label_set_text(lblAdd, "Add");
    lv_obj_set_user_data(btnAdd, newPatternInput);
    lv_obj_add_event_cb(btnAdd, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_obj_t* input = (lv_obj_t*)lv_obj_get_user_data(( lv_obj_t*)lv_event_get_target(e));
            const char* pattern = lv_textarea_get_text(input);
            
            if (pattern && strlen(pattern) > 0) {
                FileExplorerManager& manager = FileExplorerManager::getInstance();
                if (manager.addLogoPattern(itemNameForPatterns, String(pattern))) {
                    ESP_LOGI("FileExplorer", "Pattern added successfully");
                    lv_textarea_set_text(input, "");  // Clear input
                    // Close and reopen to refresh display
                    manager.closeDialog();
                    const FileItem* selectedItem = manager.getSelectedItem();
                    if (selectedItem) {
                        manager.showPatternManagementDialog(selectedItem);
                    }
                } else {
                    ESP_LOGE("FileExplorer", "Failed to add pattern");
                }
            }
        } }, LV_EVENT_CLICKED, nullptr);

    // Close button
    lv_obj_t* btnClose = lv_button_create(btnPanel);
    lv_obj_t* lblClose = lv_label_create(btnClose);
    lv_label_set_text(lblClose, "Close");
    lv_obj_add_event_cb(btnClose, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);

    // Focus the input
    lv_obj_add_state(newPatternInput, LV_STATE_FOCUSED);
}

void FileExplorerManager::showLogoPreview(const FileItem* item) {
    if (!ui_screenFileExplorer || !item || !item->isLogoFile) {
        return;
    }

    ESP_LOGI(TAG, "Opening logo preview for: %s", item->name.c_str());

    // Get the logo path for LVGL
    // stoopid
    // String logoPath = Logo::LogoManager::getInstance().getLogoPath(item->processNameFromFile.c_str());

    String logoPath = item->fullPath;
    if (logoPath.isEmpty()) {
        ESP_LOGW(TAG, "No logo path found for: %s", item->processNameFromFile.c_str());
        return;
    }

    // Create modal overlay with darker background for better contrast
    modalOverlay = lv_obj_create(ui_screenFileExplorer);
    lv_obj_set_size(modalOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modalOverlay, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modalOverlay, 200, LV_PART_MAIN);

    // Create preview dialog
    logoPreviewDialog = lv_obj_create(modalOverlay);
    lv_obj_set_size(logoPreviewDialog, lv_pct(95), lv_pct(90));
    lv_obj_set_align(logoPreviewDialog, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(logoPreviewDialog, lv_color_make(248, 249, 250), LV_PART_MAIN);
    lv_obj_set_style_border_width(logoPreviewDialog, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(logoPreviewDialog, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_radius(logoPreviewDialog, 8, LV_PART_MAIN);

    // Title header
    lv_obj_t* titlePanel = lv_obj_create(logoPreviewDialog);
    lv_obj_set_size(titlePanel, lv_pct(100), 60);
    lv_obj_set_align(titlePanel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(titlePanel, lv_color_make(52, 73, 94), LV_PART_MAIN);
    lv_obj_set_style_radius(titlePanel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(titlePanel, 10, LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(titlePanel);
    String titleText = "Logo Preview: " + item->processNameFromFile;
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_align(title, LV_ALIGN_LEFT_MID);

    // File info in title
    lv_obj_t* fileInfo = lv_label_create(titlePanel);
    String infoText = item->name + " (" + item->sizeString + ")";
    lv_label_set_text(fileInfo, infoText.c_str());
    lv_obj_set_style_text_color(fileInfo, lv_color_make(189, 195, 199), LV_PART_MAIN);
    lv_obj_set_style_text_font(fileInfo, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_align(fileInfo, LV_ALIGN_RIGHT_MID);

    // Preview area with neutral backgrounds for testing
    lv_obj_t* previewContainer = lv_obj_create(logoPreviewDialog);
    lv_obj_set_size(previewContainer, lv_pct(95), lv_pct(75));
    lv_obj_set_align(previewContainer, LV_ALIGN_CENTER);
    lv_obj_set_y(previewContainer, 10);
    lv_obj_set_style_bg_opa(previewContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(previewContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(previewContainer, 5, LV_PART_MAIN);

    // Create multiple preview panels with different backgrounds for debugging
    const int panelWidth = lv_pct(30);
    const int panelHeight = lv_pct(45);

    // Panel 1: White background (default)
    lv_obj_t* whitePanel = lv_obj_create(previewContainer);
    lv_obj_set_size(whitePanel, panelWidth, panelHeight);
    lv_obj_set_align(whitePanel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_bg_color(whitePanel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(whitePanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(whitePanel, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_radius(whitePanel, 4, LV_PART_MAIN);

    lv_obj_t* whiteLabel = lv_label_create(whitePanel);
    lv_label_set_text(whiteLabel, "White BG");
    lv_obj_set_align(whiteLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(whiteLabel, 5);
    lv_obj_set_style_text_font(whiteLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t* whiteLogo = lv_image_create(whitePanel);
    lv_obj_set_align(whiteLogo, LV_ALIGN_CENTER);
    lv_image_set_src(whiteLogo, logoPath.c_str());

    // Panel 2: Dark background
    lv_obj_t* darkPanel = lv_obj_create(previewContainer);
    lv_obj_set_size(darkPanel, panelWidth, panelHeight);
    lv_obj_set_align(darkPanel, LV_ALIGN_TOP_MID);
    lv_obj_set_style_bg_color(darkPanel, lv_color_make(44, 62, 80), LV_PART_MAIN);
    lv_obj_set_style_border_width(darkPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(darkPanel, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_radius(darkPanel, 4, LV_PART_MAIN);

    lv_obj_t* darkLabel = lv_label_create(darkPanel);
    lv_label_set_text(darkLabel, "Dark BG");
    lv_obj_set_align(darkLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(darkLabel, 5);
    lv_obj_set_style_text_color(darkLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(darkLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t* darkLogo = lv_image_create(darkPanel);
    lv_obj_set_align(darkLogo, LV_ALIGN_CENTER);
    lv_image_set_src(darkLogo, logoPath.c_str());

    // Panel 3: Gray background (neutral)
    lv_obj_t* grayPanel = lv_obj_create(previewContainer);
    lv_obj_set_size(grayPanel, panelWidth, panelHeight);
    lv_obj_set_align(grayPanel, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_style_bg_color(grayPanel, lv_color_make(127, 140, 141), LV_PART_MAIN);
    lv_obj_set_style_border_width(grayPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(grayPanel, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_radius(grayPanel, 4, LV_PART_MAIN);

    lv_obj_t* grayLabel = lv_label_create(grayPanel);
    lv_label_set_text(grayLabel, "Gray BG");
    lv_obj_set_align(grayLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(grayLabel, 5);
    lv_obj_set_style_text_color(grayLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(grayLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t* grayLogo = lv_image_create(grayPanel);
    lv_obj_set_align(grayLogo, LV_ALIGN_CENTER);
    lv_image_set_src(grayLogo, logoPath.c_str());

    // Panel 4: Checkerboard pattern background
    lv_obj_t* checkerPanel = lv_obj_create(previewContainer);
    lv_obj_set_size(checkerPanel, panelWidth, panelHeight);
    lv_obj_set_align(checkerPanel, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_bg_color(checkerPanel, lv_color_make(245, 245, 245), LV_PART_MAIN);
    lv_obj_set_style_border_width(checkerPanel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(checkerPanel, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_set_style_radius(checkerPanel, 4, LV_PART_MAIN);

    lv_obj_t* checkerLabel = lv_label_create(checkerPanel);
    lv_label_set_text(checkerLabel, "Light Gray BG");
    lv_obj_set_align(checkerLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(checkerLabel, 5);
    lv_obj_set_style_text_font(checkerLabel, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t* checkerLogo = lv_image_create(checkerPanel);
    lv_obj_set_align(checkerLogo, LV_ALIGN_CENTER);
    lv_image_set_src(checkerLogo, logoPath.c_str());

    // Panel 5: Large preview panel
    lv_obj_t* largePanel = lv_obj_create(previewContainer);
    lv_obj_set_size(largePanel, lv_pct(65), panelHeight);
    lv_obj_set_align(largePanel, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_style_bg_color(largePanel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(largePanel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(largePanel, lv_color_make(52, 152, 219), LV_PART_MAIN);
    lv_obj_set_style_radius(largePanel, 4, LV_PART_MAIN);

    lv_obj_t* largeLabel = lv_label_create(largePanel);
    lv_label_set_text(largeLabel, "Large Preview");
    lv_obj_set_align(largeLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(largeLabel, 5);
    lv_obj_set_style_text_font(largeLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(largeLabel, lv_color_make(52, 152, 219), LV_PART_MAIN);

    lv_obj_t* largeLogo = lv_image_create(largePanel);
    lv_obj_set_align(largeLogo, LV_ALIGN_CENTER);
    lv_image_set_src(largeLogo, logoPath.c_str());

    // Button panel at bottom
    lv_obj_t* buttonPanel = lv_obj_create(logoPreviewDialog);
    lv_obj_set_size(buttonPanel, lv_pct(100), 50);
    lv_obj_set_align(buttonPanel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_opa(buttonPanel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(buttonPanel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(buttonPanel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Close button
    lv_obj_t* btnClose = lv_button_create(buttonPanel);
    lv_obj_t* lblClose = lv_label_create(btnClose);
    lv_label_set_text(lblClose, "Close");
    lv_obj_set_style_bg_color(btnClose, lv_color_make(149, 165, 166), LV_PART_MAIN);
    lv_obj_add_event_cb(btnClose, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager::getInstance().closeDialog();
        } }, LV_EVENT_CLICKED, nullptr);

    // Properties button for detailed info
    lv_obj_t* btnInfo = lv_button_create(buttonPanel);
    lv_obj_t* lblInfo = lv_label_create(btnInfo);
    lv_label_set_text(lblInfo, "Properties");
    lv_obj_set_style_bg_color(btnInfo, lv_color_make(52, 152, 219), LV_PART_MAIN);
    lv_obj_add_event_cb(btnInfo, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            FileExplorerManager& manager = FileExplorerManager::getInstance();
            const FileItem* selectedItem = manager.getSelectedItem();
            if (selectedItem) {
                manager.closeDialog();
                manager.showLogoProperties(selectedItem);
            }
        } }, LV_EVENT_CLICKED, nullptr);

    ESP_LOGI(TAG, "Logo preview opened successfully");
}

// =============================================================================
// NAVIGATION HISTORY AND STATE MANAGEMENT
// =============================================================================

bool FileExplorerManager::canNavigateBack() const {
    return !navigationHistory.empty();
}

bool FileExplorerManager::navigateBack() {
    if (navigationHistory.empty()) {
        ESP_LOGW(TAG, "No navigation history available");
        return false;
    }

    NavigationState prevState = popNavigationState();

    ESP_LOGI(TAG, "Navigating back to: %s", prevState.path.c_str());

    // Load the previous directory without saving current state
    if (loadDirectory(prevState.path)) {
        currentPath = prevState.path;

        // Restore previous UI state
        currentScrollPosition = prevState.scrollPosition;
        lastSelectedItemName = prevState.selectedItemName;

        updateContent();
        restoreUIState();

        return true;
    }

    return false;
}

void FileExplorerManager::clearNavigationHistory() {
    navigationHistory.clear();
    ESP_LOGI(TAG, "Navigation history cleared");
}

void FileExplorerManager::saveCurrentState() {
    if (!fileList) {
        return;
    }

    // Save current scroll position
    currentScrollPosition = lv_obj_get_scroll_y(fileList);

    // Save current selection
    if (selectedItem) {
        lastSelectedItemName = selectedItem->name;
    } else {
        lastSelectedItemName = "";
    }

    ESP_LOGD(TAG, "Saved UI state - scroll: %d, selected: %s",
             currentScrollPosition, lastSelectedItemName.c_str());
}

void FileExplorerManager::preserveUIState() {
    saveCurrentState();
}

void FileExplorerManager::restoreUIState() {
    if (!fileList) {
        return;
    }

    // Restore scroll position
    lv_obj_scroll_to_y(fileList, currentScrollPosition, LV_ANIM_OFF);

    // Restore selection if the item still exists
    if (!lastSelectedItemName.isEmpty()) {
        // Find and select the previously selected item
        for (size_t i = 0; i < currentItems.size(); i++) {
            if (currentItems[i].name == lastSelectedItemName) {
                // Find the corresponding UI list item
                uint32_t childCount = lv_obj_get_child_count(fileList);
                for (uint32_t j = 0; j < childCount; j++) {
                    lv_obj_t* listItem = lv_obj_get_child(fileList, j);
                    int index = (int)(intptr_t)lv_obj_get_user_data(listItem);

                    if (index == (int)i) {
                        // Restore selection
                        selectedItem = &currentItems[i];
                        selectedListItem = listItem;
                        lv_obj_set_style_bg_color(listItem, lv_color_make(200, 220, 255), LV_PART_MAIN);
                        updateButtonStates();
                        break;
                    }
                }
                break;
            }
        }
    }

    ESP_LOGD(TAG, "Restored UI state - scroll: %d, selected: %s",
             currentScrollPosition, lastSelectedItemName.c_str());
}

void FileExplorerManager::pushNavigationState(const String& path) {
    NavigationState state;
    state.path = path;
    state.scrollPosition = currentScrollPosition;
    state.selectedItemName = lastSelectedItemName;

    navigationHistory.push_back(state);

    // Limit history size
    if (navigationHistory.size() > MAX_HISTORY_SIZE) {
        navigationHistory.erase(navigationHistory.begin());
    }

    ESP_LOGD(TAG, "Pushed navigation state: %s (history size: %d)",
             path.c_str(), navigationHistory.size());
}

NavigationState FileExplorerManager::popNavigationState() {
    if (navigationHistory.empty()) {
        return NavigationState();
    }

    NavigationState state = navigationHistory.back();
    navigationHistory.pop_back();

    ESP_LOGD(TAG, "Popped navigation state: %s (history size: %d)",
             state.path.c_str(), navigationHistory.size());

    return state;
}

}  // namespace FileExplorer
}  // namespace Application
