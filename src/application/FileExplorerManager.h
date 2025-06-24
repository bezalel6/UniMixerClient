#ifndef FILE_EXPLORER_MANAGER_H
#define FILE_EXPLORER_MANAGER_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>

namespace Application {
namespace FileExplorer {

// File/Directory item structure
typedef struct {
    String name;
    String fullPath;
    bool isDirectory;
    size_t size;
    String sizeString;
} FileItem;

// File explorer state
typedef enum {
    FE_STATE_IDLE,
    FE_STATE_LOADING,
    FE_STATE_ERROR,
    FE_STATE_CREATING_FOLDER,
    FE_STATE_DELETING,
    FE_STATE_SHOWING_PROPERTIES
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
    void refreshCurrentDirectory();

    // Directory operations
    bool createDirectory(const String& name);
    bool deleteItem(const String& path);

    // File operations
    bool createTextFile(const String& name, const String& content = "");
    bool readTextFile(const String& path, String& content);
    bool writeTextFile(const String& path, const String& content);

    // UI Management
    void updateUI();
    void updateSDStatus();
    void showProperties(const FileItem* item);
    void showCreateFolderDialog();
    void showSimpleFolderDialog();  // Fallback without keyboard
    void showDeleteConfirmation(const FileItem* item);
    void showFileViewer(const FileItem* item);
    void closeDialog();

    // State getters
    const String& getCurrentPath() const { return currentPath; }
    const std::vector<FileItem>& getCurrentItems() const { return currentItems; }
    FileExplorerState getState() const { return state; }
    const FileItem* getSelectedItem() const { return selectedItem; }

    // Utility methods
    String formatFileSize(size_t bytes);
    void addItem(const FileItem& item);

    // Event handling
    void onFileItemClicked(const FileItem* item);
    void onFileItemDoubleClicked(const FileItem* item);
    void onBackButtonClicked();
    void onRefreshClicked();
    void onNewFolderClicked();
    void onDeleteClicked();
    void onPropertiesClicked();

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

    // Dialog components
    lv_obj_t* modalOverlay;
    lv_obj_t* inputDialog;
    lv_obj_t* confirmDialog;
    lv_obj_t* propertiesDialog;
    lv_obj_t* fileViewerDialog;
};

}  // namespace FileExplorer
}  // namespace Application

#endif  // FILE_EXPLORER_MANAGER_H
