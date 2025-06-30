#ifndef UNIVERSAL_DIALOG_H
#define UNIVERSAL_DIALOG_H

#include <lvgl.h>
#include <functional>
#include <vector>
#include <memory>

namespace UI {
namespace Dialog {

// Dialog types for consistent styling and behavior
enum class DialogType {
    INFO,      // Information dialog with OK button
    WARNING,   // Warning dialog with OK/Cancel
    ERROR,     // Error dialog with red theme
    CONFIRM,   // Confirmation dialog with Yes/No
    PROGRESS,  // Progress dialog with progress bar
    INPUT,     // Input dialog with text field
    CUSTOM     // Custom dialog for specialized use
};

// Dialog themes for consistent visual design
enum class DialogTheme {
    LIGHT,    // Light theme (default)
    DARK,     // Dark theme
    SUCCESS,  // Green theme for success messages
    WARNING,  // Orange theme for warnings
    ERROR,    // Red theme for errors
    INFO      // Blue theme for information
};

// Dialog size presets
enum class DialogSize {
    SMALL,        // 300x200
    MEDIUM,       // 400x300
    LARGE,        // 500x400
    EXTRA_LARGE,  // 600x500
    FULLSCREEN    // 90% of screen
};

// Button configuration
struct DialogButton {
    String text;
    std::function<void()> callback;
    DialogTheme theme = DialogTheme::LIGHT;
    bool isDefault = false;  // Primary button styling

    DialogButton(const String& txt, std::function<void()> cb, bool def = false, DialogTheme th = DialogTheme::LIGHT)
        : text(txt), callback(cb), isDefault(def), theme(th) {}
};

// Progress dialog configuration
struct ProgressConfig {
    String title;
    String message;
    int value = 0;
    int max = 100;
    bool indeterminate = false;
    bool cancellable = false;
    std::function<void()> cancelCallback = nullptr;
};

// Input dialog configuration
struct InputConfig {
    String title;
    String message;
    String placeholder;
    String defaultValue;
    bool isPassword = false;
    bool multiline = false;
    int maxLength = 256;
    std::function<void(const String&)> onConfirm = nullptr;
    std::function<void()> onCancel = nullptr;
};

// Main Universal Dialog class
class UniversalDialog {
   private:
    static lv_obj_t* currentDialog;
    static lv_obj_t* currentOverlay;
    static lv_obj_t* currentProgressBar;
    static lv_obj_t* currentStatusLabel;
    static lv_obj_t* currentInputField;

    // Theme color schemes
    static lv_color_t getThemeColor(DialogTheme theme, bool isBackground = false);
    static void applyTheme(lv_obj_t* obj, DialogTheme theme);

    // Dialog creation helpers
    static lv_obj_t* createOverlay(lv_obj_t* parent);
    static lv_obj_t* createDialogContainer(lv_obj_t* parent, DialogSize size, DialogTheme theme);
    static lv_obj_t* createTitle(lv_obj_t* parent, const String& title, DialogTheme theme);
    static lv_obj_t* createMessage(lv_obj_t* parent, const String& message, DialogTheme theme);
    static lv_obj_t* createButtonPanel(lv_obj_t* parent, const std::vector<DialogButton>& buttons);
    static lv_obj_t* createProgressSection(lv_obj_t* parent, const ProgressConfig& config);
    static lv_obj_t* createInputSection(lv_obj_t* parent, const InputConfig& config);

    // Animation helpers
    static void animateIn(lv_obj_t* dialog);
    static void animateOut(lv_obj_t* dialog, std::function<void()> onComplete = nullptr);

   public:
    // Standard dialog types
    static void showInfo(const String& title, const String& message,
                         std::function<void()> onOK = nullptr,
                         DialogSize size = DialogSize::MEDIUM);

    static void showWarning(const String& title, const String& message,
                            std::function<void()> onOK = nullptr,
                            std::function<void()> onCancel = nullptr,
                            DialogSize size = DialogSize::MEDIUM);

    static void showError(const String& title, const String& message,
                          std::function<void()> onOK = nullptr,
                          DialogSize size = DialogSize::MEDIUM);

    static void showConfirm(const String& title, const String& message,
                            std::function<void()> onYes,
                            std::function<void()> onNo = nullptr,
                            DialogSize size = DialogSize::MEDIUM);

    // Progress dialog
    static void showProgress(const ProgressConfig& config, DialogSize size = DialogSize::MEDIUM);
    static void updateProgress(int value, const String& message = "");
    static void setProgressIndeterminate(bool indeterminate);

    // Input dialog
    static void showInput(const InputConfig& config, DialogSize size = DialogSize::MEDIUM);

    // Custom dialog builder
    static void showCustom(const String& title, const String& message,
                           const std::vector<DialogButton>& buttons,
                           DialogTheme theme = DialogTheme::LIGHT,
                           DialogSize size = DialogSize::MEDIUM);

    // Quick convenience methods
    static void showQuickInfo(const String& message);
    static void showQuickError(const String& message);
    static void showQuickSuccess(const String& message);

    // Dialog management
    static bool isDialogOpen();
    static void closeDialog(bool animated = true);
    static void closeAll();

    // Global settings
    static void setDefaultTheme(DialogTheme theme);
    static void setAnimationEnabled(bool enabled);
    static void setModalBackground(bool modal);
};

// Dialog manager for automatic cleanup and management
class DialogManager {
   private:
    static std::vector<UniversalDialog*> activeDialogs;

   public:
    static void registerDialog(UniversalDialog* dialog);
    static void unregisterDialog(UniversalDialog* dialog);
    static void closeAllDialogs();
    static size_t getActiveDialogCount();
};

}  // namespace Dialog
}  // namespace UI

#endif  // UNIVERSAL_DIALOG_H
