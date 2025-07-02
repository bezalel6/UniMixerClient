#include "UniversalDialog.h"
#include "ManagerMacros.h"
#include "UIConstants.h"
#include <esp_log.h>
#include <Arduino.h>

static const char* TAG = "UniversalDialog";

namespace UI {
namespace Dialog {
#define HEX(clr) lv_color_hex(clr)

// Static member definitions
lv_obj_t* UniversalDialog::currentDialog = nullptr;
lv_obj_t* UniversalDialog::currentOverlay = nullptr;
lv_obj_t* UniversalDialog::currentProgressBar = nullptr;
lv_obj_t* UniversalDialog::currentStatusLabel = nullptr;
lv_obj_t* UniversalDialog::currentInputField = nullptr;
std::vector<UniversalDialog*> DialogManager::activeDialogs;

// Global settings
static DialogTheme defaultTheme = DialogTheme::LIGHT;
static bool animationEnabled = true;
static bool modalBackground = true;

// Theme color definitions following material design principles
lv_color_t UniversalDialog::getThemeColor(DialogTheme theme, bool isBackground) {
    switch (theme) {
        case DialogTheme::LIGHT:
            return isBackground ? HEX(0xF8F9FA) : HEX(0x495057);
        case DialogTheme::DARK:
            return isBackground ? HEX(0x2C3E50) : HEX(0xECF0F1);
        case DialogTheme::SUCCESS:
            return isBackground ? HEX(0xD4EDDA) : HEX(0x155724);
        case DialogTheme::WARNING:
            return isBackground ? HEX(0xFFF3CD) : HEX(0x856404);
        case DialogTheme::ERROR:
            return isBackground ? HEX(0xF8D7DA) : HEX(0x721C24);
        case DialogTheme::INFO:
            return isBackground ? HEX(0xD1ECF1) : HEX(0x0C5460);
        default:
            return HEX(0xFFFFFF);
    }
}

void UniversalDialog::applyTheme(lv_obj_t* obj, DialogTheme theme) {
    if (!obj) return;

    lv_color_t bgColor = getThemeColor(theme, true);
    lv_color_t textColor = getThemeColor(theme, false);

    lv_obj_set_style_bg_color(obj, bgColor, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, textColor, LV_PART_MAIN);

    // Add subtle border for better definition
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_mix(bgColor, textColor, 200), LV_PART_MAIN);
}

// Dialog size helper
static void getDialogDimensions(DialogSize size, int* width, int* height) {
    switch (size) {
        case DialogSize::SMALL:
            *width = 300;
            *height = 200;
            break;
        case DialogSize::MEDIUM:
            *width = 400;
            *height = 300;
            break;
        case DialogSize::LARGE:
            *width = 500;
            *height = 400;
            break;
        case DialogSize::EXTRA_LARGE:
            *width = 600;
            *height = 500;
            break;
        case DialogSize::FULLSCREEN:
            *width = LV_PCT(90);
            *height = LV_PCT(90);
            break;
        default:
            *width = 400;
            *height = 300;
    }
}

lv_obj_t* UniversalDialog::createOverlay(lv_obj_t* parent) {
    if (!parent) {
        parent = lv_scr_act();
    }

    lv_obj_t* overlay = lv_obj_create(parent);
    LVGL_SET_SIZE_POS(overlay, LV_PCT(100), LV_PCT(100), 0, 0);

    // Modal background styling
    if (modalBackground) {
        lv_obj_set_style_bg_color(overlay, HEX(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, 128, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    return overlay;
}

lv_obj_t* UniversalDialog::createDialogContainer(lv_obj_t* parent, DialogSize size, DialogTheme theme) {
    int width, height;
    getDialogDimensions(size, &width, &height);

    lv_obj_t* dialog = lv_obj_create(parent);
    LVGL_SET_SIZE_ALIGN(dialog, width, height, LV_ALIGN_CENTER);

    // Modern dialog styling with shadows and rounded corners using new approach
    applyTheme(dialog, theme);
    lv_obj_set_style_radius(dialog, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dialog, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(dialog, HEX(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dialog, 100, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(dialog, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 24, LV_PART_MAIN);

    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    return dialog;
}

lv_obj_t* UniversalDialog::createTitle(lv_obj_t* parent, const String& title, DialogTheme theme) {
    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, title.c_str());
    lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(titleLabel, 0);

    // Title styling with theme-based color - simplified approach
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(titleLabel, getThemeColor(theme, false), LV_PART_MAIN);
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    return titleLabel;
}

lv_obj_t* UniversalDialog::createMessage(lv_obj_t* parent, const String& message, DialogTheme theme) {
    lv_obj_t* messageLabel = lv_label_create(parent);
    lv_label_set_text(messageLabel, message.c_str());
    lv_obj_set_align(messageLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(messageLabel, -20);

    // Message styling with theme-based color - simplified approach
    lv_obj_set_style_text_font(messageLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(messageLabel, getThemeColor(theme, false), LV_PART_MAIN);
    lv_obj_set_style_text_align(messageLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(messageLabel, LV_PCT(90));

    return messageLabel;
}

lv_obj_t* UniversalDialog::createButtonPanel(lv_obj_t* parent, const std::vector<DialogButton>& buttons) {
    lv_obj_t* panel = lv_obj_create(parent);
    LVGL_SET_SIZE_ALIGN(panel, LV_PCT(100), 60, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(panel, 0);

    // Use new panel styling macro
    LVGL_STYLE_PANEL(panel, LV_OPA_TRANSP, LV_OPA_TRANSP);

    // Setup as flex container using new macro
    LVGL_SETUP_FLEX_CONTAINER(panel, LV_FLEX_FLOW_ROW,
                              LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create buttons
    for (const auto& buttonConfig : buttons) {
        lv_obj_t* btn = lv_btn_create(panel);
        lv_obj_set_size(btn, 100, 40);

        // Use new button styling based on properties
        if (buttonConfig.isDefault) {
            // Primary button styling
            switch (buttonConfig.theme) {
                case DialogTheme::SUCCESS:
                    LVGL_STYLE_BUTTON(btn, 0x28A745, 0xFFFFFF);
                    break;
                case DialogTheme::ERROR:
                    LVGL_STYLE_BUTTON(btn, 0xDC3545, 0xFFFFFF);
                    break;
                case DialogTheme::WARNING:
                    LVGL_STYLE_BUTTON(btn, 0xFFC107, 0x000000);
                    break;
                default:
                    LVGL_STYLE_BUTTON(btn, 0x007BFF, 0xFFFFFF);
                    break;
            }
        } else {
            // Secondary button styling
            LVGL_STYLE_BUTTON(btn, 0x6C757D, 0xFFFFFF);
        }

        // Button label
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, buttonConfig.text.c_str());
        lv_obj_center(label);
        LVGL_STYLE_LABEL(label, &lv_font_montserrat_14, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);

        // Store callback in user data and add event handler
        static std::vector<std::function<void()>> callbacks;
        callbacks.push_back(buttonConfig.callback);
        lv_obj_set_user_data(btn, &callbacks.back());

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                auto* callback = static_cast<std::function<void()>*>(lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e)));
                if (callback && *callback) {
                    (*callback)();
                }
            } }, LV_EVENT_CLICKED, nullptr);
    }

    return panel;
}

lv_obj_t* UniversalDialog::createProgressSection(lv_obj_t* parent, const ProgressConfig& config) {
    // Progress bar
    currentProgressBar = lv_bar_create(parent);
    LVGL_SET_SIZE_ALIGN(currentProgressBar, LV_PCT(80), 20, LV_ALIGN_CENTER);
    lv_obj_set_y(currentProgressBar, -10);

    // Use new progress bar styling macro
    LVGL_STYLE_PROGRESS_BAR(currentProgressBar, 0xE9ECEF, 0x007BFF);

    // Set initial value
    lv_bar_set_range(currentProgressBar, 0, config.max);
    if (config.indeterminate) {
        lv_bar_set_value(currentProgressBar, 50, LV_ANIM_ON);
        // TODO: Add indeterminate animation
    } else {
        lv_bar_set_value(currentProgressBar, config.value, LV_ANIM_OFF);
    }

    // Status label
    currentStatusLabel = lv_label_create(parent);
    lv_label_set_text(currentStatusLabel, config.message.c_str());
    lv_obj_set_align(currentStatusLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(currentStatusLabel, 20);
    LVGL_STYLE_LABEL(currentStatusLabel, &lv_font_montserrat_12, 0x000000, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(currentStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(currentStatusLabel, LV_PCT(90));

    return currentProgressBar;
}

lv_obj_t* UniversalDialog::createInputSection(lv_obj_t* parent, const InputConfig& config) {
    // Input label
    lv_obj_t* inputLabel = lv_label_create(parent);
    lv_label_set_text(inputLabel, config.message.c_str());
    lv_obj_set_align(inputLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(inputLabel, -40);
    LVGL_STYLE_LABEL(inputLabel, &lv_font_montserrat_14, 0x000000, LV_TEXT_ALIGN_CENTER);

    // Input field
    if (config.multiline) {
        currentInputField = lv_textarea_create(parent);
        LVGL_SET_SIZE_ALIGN(currentInputField, LV_PCT(80), 100, LV_ALIGN_CENTER);
    } else {
        currentInputField = lv_textarea_create(parent);
        LVGL_SET_SIZE_ALIGN(currentInputField, LV_PCT(80), 40, LV_ALIGN_CENTER);
        lv_textarea_set_one_line(currentInputField, true);
    }

    lv_obj_set_y(currentInputField, 0);

    // Use new input field styling macro
    LVGL_STYLE_INPUT_FIELD(currentInputField, 0xFFFFFF, 0xCED4DA, 0x007BFF);

    // Set placeholder and default value
    if (!config.placeholder.isEmpty()) {
        lv_textarea_set_placeholder_text(currentInputField, config.placeholder.c_str());
    }
    if (!config.defaultValue.isEmpty()) {
        lv_textarea_set_text(currentInputField, config.defaultValue.c_str());
    }

    // Password mode
    if (config.isPassword) {
        lv_textarea_set_password_mode(currentInputField, true);
    }

    // Max length
    if (config.maxLength > 0) {
        lv_textarea_set_max_length(currentInputField, config.maxLength);
    }

    return currentInputField;
}

// Public interface implementations
void UniversalDialog::showInfo(const String& title, const String& message, std::function<void()> onOK, DialogSize size) {
    std::vector<DialogButton> buttons;
    buttons.emplace_back("OK", [onOK]() {
        closeDialog();
        if (onOK) onOK(); }, true, DialogTheme::INFO);

    showCustom(title, message, buttons, DialogTheme::INFO, size);
}

void UniversalDialog::showWarning(const String& title, const String& message, std::function<void()> onOK, std::function<void()> onCancel, DialogSize size) {
    std::vector<DialogButton> buttons;
    buttons.emplace_back("OK", [onOK]() {
        closeDialog();
        if (onOK) onOK(); }, true, DialogTheme::WARNING);

    if (onCancel) {
        buttons.emplace_back("Cancel", [onCancel]() {
            closeDialog();
            onCancel();
        });
    }

    showCustom(title, message, buttons, DialogTheme::WARNING, size);
}

void UniversalDialog::showError(const String& title, const String& message, std::function<void()> onOK, DialogSize size) {
    std::vector<DialogButton> buttons;
    buttons.emplace_back("OK", [onOK]() {
        closeDialog();
        if (onOK) onOK(); }, true, DialogTheme::ERROR);

    showCustom(title, message, buttons, DialogTheme::ERROR, size);
}

void UniversalDialog::showConfirm(const String& title, const String& message, std::function<void()> onYes, std::function<void()> onNo, DialogSize size) {
    std::vector<DialogButton> buttons;
    buttons.emplace_back("Yes", [onYes]() {
        closeDialog();
        if (onYes) onYes(); }, true, DialogTheme::SUCCESS);

    buttons.emplace_back("No", [onNo]() {
        closeDialog();
        if (onNo) onNo();
    });

    showCustom(title, message, buttons, DialogTheme::LIGHT, size);
}

// Progress dialog
void UniversalDialog::showProgress(const ProgressConfig& config, DialogSize size) {
    closeDialog(false);  // Close any existing dialog

    currentOverlay = createOverlay(lv_scr_act());
    currentDialog = createDialogContainer(currentOverlay, size, DialogTheme::LIGHT);

    // Title
    if (!config.title.isEmpty()) {
        createTitle(currentDialog, config.title, DialogTheme::LIGHT);
    }

    // Progress section
    createProgressSection(currentDialog, config);

    // Cancel button if cancellable
    if (config.cancellable && config.cancelCallback) {
        std::vector<DialogButton> buttons;
        buttons.emplace_back("Cancel", [config]() {
            closeDialog();
            if (config.cancelCallback) config.cancelCallback();
        });
        createButtonPanel(currentDialog, buttons);
    }

    ESP_LOGI(TAG, "Progress dialog shown: %s", config.title.c_str());
}

void UniversalDialog::updateProgress(int value, const String& message) {
    if (currentProgressBar) {
        lv_bar_set_value(currentProgressBar, value, LV_ANIM_ON);
    }
    if (currentStatusLabel && !message.isEmpty()) {
        lv_label_set_text(currentStatusLabel, message.c_str());
    }
}

void UniversalDialog::setProgressIndeterminate(bool indeterminate) {
    if (currentProgressBar) {
        if (indeterminate) {
            // Create continuous animation
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, currentProgressBar);
            lv_anim_set_values(&anim, 0, 100);
            lv_anim_set_time(&anim, 2000);
            lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_bar_set_value);
            lv_anim_start(&anim);
        } else {
            lv_anim_delete(currentProgressBar, nullptr);
        }
    }
}

// Input dialog
void UniversalDialog::showInput(const InputConfig& config, DialogSize size) {
    closeDialog(false);  // Close any existing dialog

    currentOverlay = createOverlay(lv_scr_act());
    currentDialog = createDialogContainer(currentOverlay, size, DialogTheme::LIGHT);

    // Title
    if (!config.title.isEmpty()) {
        createTitle(currentDialog, config.title, DialogTheme::LIGHT);
    }

    // Input section
    createInputSection(currentDialog, config);

    // Buttons
    std::vector<DialogButton> buttons;
    buttons.emplace_back("OK", [config]() {
        String inputValue = "";
        if (currentInputField) {
            inputValue = lv_textarea_get_text(currentInputField);
        }
        closeDialog();
        if (config.onConfirm) config.onConfirm(inputValue); }, true, DialogTheme::SUCCESS);

    buttons.emplace_back("Cancel", [config]() {
        closeDialog();
        if (config.onCancel) config.onCancel();
    });

    createButtonPanel(currentDialog, buttons);

    ESP_LOGI(TAG, "Input dialog shown: %s", config.title.c_str());
}

void UniversalDialog::showCustom(const String& title, const String& message, const std::vector<DialogButton>& buttons, DialogTheme theme, DialogSize size) {
    closeDialog(false);  // Close any existing dialog

    currentOverlay = createOverlay(lv_scr_act());
    currentDialog = createDialogContainer(currentOverlay, size, theme);

    // Title
    if (!title.isEmpty()) {
        createTitle(currentDialog, title, theme);
    }

    // Message
    if (!message.isEmpty()) {
        createMessage(currentDialog, message, theme);
    }

    // Buttons
    if (!buttons.empty()) {
        createButtonPanel(currentDialog, buttons);
    }

    ESP_LOGI(TAG, "Custom dialog shown: %s", title.c_str());
}

// Quick convenience methods
void UniversalDialog::showQuickInfo(const String& message) {
    showInfo("Information", message);
}

void UniversalDialog::showQuickError(const String& message) {
    showError("Error", message);
}

void UniversalDialog::showQuickSuccess(const String& message) {
    showInfo("Success", message, nullptr, DialogSize::SMALL);
}

// Dialog management
bool UniversalDialog::isDialogOpen() {
    return currentDialog != nullptr && lv_obj_is_valid(currentDialog);
}

void UniversalDialog::closeDialog(bool animated) {
    if (!currentDialog || !lv_obj_is_valid(currentDialog)) {
        return;
    }

    if (currentOverlay && lv_obj_is_valid(currentOverlay)) {
        lv_obj_del(currentOverlay);
    }
    currentDialog = nullptr;
    currentOverlay = nullptr;
    currentProgressBar = nullptr;
    currentStatusLabel = nullptr;
    currentInputField = nullptr;

    ESP_LOGI(TAG, "Dialog closed");
}

void UniversalDialog::closeAll() {
    closeDialog(false);
    DialogManager::closeAllDialogs();
}

// Global settings
void UniversalDialog::setDefaultTheme(DialogTheme theme) {
    defaultTheme = theme;
}

void UniversalDialog::setAnimationEnabled(bool enabled) {
    animationEnabled = enabled;
}

void UniversalDialog::setModalBackground(bool modal) {
    modalBackground = modal;
}

// Dialog Manager implementation
void DialogManager::registerDialog(UniversalDialog* dialog) {
    activeDialogs.push_back(dialog);
}

void DialogManager::unregisterDialog(UniversalDialog* dialog) {
    activeDialogs.erase(std::remove(activeDialogs.begin(), activeDialogs.end(), dialog), activeDialogs.end());
}

void DialogManager::closeAllDialogs() {
    activeDialogs.clear();
}

size_t DialogManager::getActiveDialogCount() {
    return activeDialogs.size();
}

}  // namespace Dialog
}  // namespace UI
