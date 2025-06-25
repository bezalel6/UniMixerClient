#ifndef FILE_EXPLORER_MANAGER_H
#define FILE_EXPLORER_MANAGER_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "../logo/LogoManager.h"

namespace Application {
namespace FileExplorer {

// Enhanced file/directory item structure with logo support
typedef struct {
    String name;
    String fullPath;
    bool isDirectory;
    size_t size;
    String sizeString;

    // Logo-specific properties
    bool isLogoFile;             // True if this is a logo binary file
    bool isLogoMetadata;         // True if this is a logo metadata file (deprecated)
    bool hasLogoMetadata;        // True if corresponding metadata exists
    bool logoVerified;           // True if logo is verified
    bool logoFlagged;            // True if logo is flagged as incorrect
    String processNameFromFile;  // Process name extracted from filename
} FileItem;

// Enhanced file explorer state with logo states
typedef enum {
    FE_STATE_IDLE,
    FE_STATE_LOADING,
    FE_STATE_ERROR,
    FE_STATE_CREATING_FOLDER,
    FE_STATE_DELETING,
    FE_STATE_SHOWING_PROPERTIES,
    FE_STATE_SHOWING_LOGO_PROPERTIES,
    FE_STATE_ASSIGNING_LOGO,
    FE_STATE_MANAGING_PATTERNS
} FileExplorerState;

class FileExplorerManager {
   public:
    static FileExplorerManager& getInstance();

    // Initialization and cleanup
    bool init();
    void deinit();

    // Navigation
    bool navigateToPath(const String& path);
    bool navigateUp();
    bool navigateToRoot();
    bool navigateToLogosRoot();  // Navigate to /logos directory
    void refreshCurrentDirectory();

    // Directory operations
    bool createDirectory(const String& name);
    bool deleteItem(const String& path);

    // File operations
    bool createTextFile(const String& name, const String& content = "");
    bool readTextFile(const String& path, String& content);
    bool writeTextFile(const String& path, const String& content);

    // Logo-specific operations
    bool assignLogoToProcess(const String& logoFileName, const String& processName);
    bool flagLogoIncorrect(const String& logoFileName, bool incorrect = true);
    bool markLogoVerified(const String& logoFileName, bool verified = true);
    bool addLogoPattern(const String& logoFileName, const String& pattern);
    bool deleteLogoAndMetadata(const String& logoFileName);

    // UI Management
    void updateUI();
    void updateSDStatus();
    void showProperties(const FileItem* item);
    void showLogoProperties(const FileItem* item);           // Enhanced logo properties
    void showLogoAssignmentDialog(const FileItem* item);     // Assign logo to process
    void showPatternManagementDialog(const FileItem* item);  // Manage fuzzy patterns
    void showCreateFolderDialog();
    void showDeleteConfirmation(const FileItem* item);
    void showFileViewer(const FileItem* item);
    void closeDialog();

    // State getters
    const String& getCurrentPath() const { return currentPath; }
    const std::vector<FileItem>& getCurrentItems() const { return currentItems; }
    FileExplorerState getState() const { return state; }
    const FileItem* getSelectedItem() const { return selectedItem; }
    bool isInLogosDirectory() const;

    // Utility methods
    String formatFileSize(size_t bytes);
    void addItem(const FileItem& item);
    bool isLogoDirectory(const String& path) const;
    String extractProcessNameFromLogoFile(const String& filename) const;

    // Event handling
    void onFileItemClicked(const FileItem* item);
    void onFileItemDoubleClicked(const FileItem* item);
    void onBackButtonClicked();
    void onRefreshClicked();
    void onNewFolderClicked();
    void onDeleteClicked();
    void onPropertiesClicked();

    // Logo-specific event handling
    void onLogoAssignClicked();
    void onLogoFlagClicked();
    void onLogoVerifyClicked();
    void onLogoPatternsClicked();

    void enhanceItemWithLogoInfo(FileItem& item);

   private:
    FileExplorerManager() = default;
    ~FileExplorerManager() = default;

    // Private methods
    bool loadDirectory(const String& path);
    void clearItems();
    void updatePathDisplay();
    void updateFileList();
    void updateButtonStates();
    void createDynamicUI();
    void destroyDynamicUI();

    // Logo-specific private methods
    void createLogoSpecificButtons();
    void updateLogoButtonStates();
    String getLogoDisplayText(const FileItem& item);
    const char* getLogoIcon(const FileItem& item);

    // State
    String currentPath;
    std::vector<FileItem> currentItems;
    FileExplorerState state;
    const FileItem* selectedItem;
    lv_obj_t* selectedListItem;  // Track selected UI item for visual feedback
    bool initialized;
    bool uiCreated;  // Track UI creation state

    // Dynamic UI components
    lv_obj_t* contentPanel;
    lv_obj_t* fileList;
    lv_obj_t* actionPanel;
    lv_obj_t* btnNewFolder;
    lv_obj_t* btnRefresh;
    lv_obj_t* btnProperties;
    lv_obj_t* btnDelete;

    // Logo-specific UI components
    lv_obj_t* logoActionPanel;   // Additional panel for logo operations
    lv_obj_t* btnLogoAssign;     // Assign logo to process
    lv_obj_t* btnLogoFlag;       // Flag as incorrect
    lv_obj_t* btnLogoVerify;     // Mark as verified
    lv_obj_t* btnLogoPatterns;   // Manage fuzzy patterns
    lv_obj_t* btnNavigateLogos;  // Quick navigation to logos directory

    // Dialog components
    lv_obj_t* modalOverlay;
    lv_obj_t* inputDialog;
    lv_obj_t* confirmDialog;
    lv_obj_t* propertiesDialog;
    lv_obj_t* fileViewerDialog;
    lv_obj_t* logoPropertiesDialog;
    lv_obj_t* logoAssignmentDialog;
    lv_obj_t* patternManagementDialog;
};

}  // namespace FileExplorer
}  // namespace Application

#endif  // FILE_EXPLORER_MANAGER_H
