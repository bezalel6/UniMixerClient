#include "Dialog.h"
#include <esp_log.h>

static const char* TAG = "Dialog";

namespace UI {
namespace Wrapper {

// =============================================================================
// DIALOG IMPLEMENTATIONS
// =============================================================================

bool Dialog::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "Dialog already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_obj_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create dialog widget");
        return false;
    }

    // Create dialog structure
    lv_obj_set_size(widget, 300, 200);
    lv_obj_center(widget);
    lv_obj_set_style_bg_color(widget, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(widget, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(widget, 12, 0);
    lv_obj_set_style_shadow_width(widget, 8, 0);
    lv_obj_set_style_shadow_color(widget, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(widget, 30, 0);
    lv_obj_set_style_pad_all(widget, 16, 0);

    // Create title label
    lv_obj_t* titleLabel = lv_label_create(widget);
    lv_label_set_text(titleLabel, title.c_str());
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x2C3E50), 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 10);

    // Create message label
    lv_obj_t* messageLabel = lv_label_create(widget);
    lv_label_set_text(messageLabel, message.c_str());
    lv_obj_set_style_text_font(messageLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(messageLabel, lv_color_hex(0x34495E), 0);
    lv_obj_align(messageLabel, LV_ALIGN_CENTER, 0, 0);

    // Create buttons
    for (size_t i = 0; i < buttons.size(); i++) {
        lv_obj_t* btn = lv_btn_create(widget);
        lv_obj_t* btnLabel = lv_label_create(btn);
        lv_label_set_text(btnLabel, buttons[i].c_str());
        lv_obj_center(btnLabel);

        // Position buttons
        int btnWidth = 80;
        int btnHeight = 32;
        int totalWidth = buttons.size() * btnWidth + (buttons.size() - 1) * 10;
        int startX = -totalWidth / 2;

        lv_obj_set_size(btn, btnWidth, btnHeight);
        lv_obj_set_pos(btn, startX + i * (btnWidth + 10), 120);

        // Add click event
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto dialog = static_cast<Dialog*>(lv_event_get_user_data(e));
            if (dialog && dialog->onButtonClick) {
                // Find button index
                lv_obj_t* clicked =static_cast<lv_obj_t*>(lv_event_get_target(e));
                for (size_t i = 0; i < dialog->buttons.size(); i++) {
                    if (lv_obj_get_child(dialog->widget, i + 2) == clicked) { // +2 for title and message
                        dialog->onButtonClick(i);
                        break;
                    }
                }
            } }, LV_EVENT_CLICKED, this);
    }

    markInitialized();
    ESP_LOGD(TAG, "Dialog created successfully: %s", widgetId.c_str());
    return true;
}

void Dialog::destroy() {
    WidgetBase<Dialog>::destroy();
}

void Dialog::update() {
    // Dialog doesn't need regular updates
}

Dialog& Dialog::setTitle(const std::string& newTitle) {
    title = newTitle;
    if (widget) {
        lv_obj_t* titleLabel = lv_obj_get_child(widget, 0);
        if (titleLabel) {
            lv_label_set_text(titleLabel, title.c_str());
        }
    }
    return *this;
}

Dialog& Dialog::setMessage(const std::string& newMessage) {
    message = newMessage;
    if (widget) {
        lv_obj_t* messageLabel = lv_obj_get_child(widget, 1);
        if (messageLabel) {
            lv_label_set_text(messageLabel, message.c_str());
        }
    }
    return *this;
}

Dialog& Dialog::setButtons(const std::vector<std::string>& newButtons) {
    buttons = newButtons;
    // Note: This would require recreating the dialog to update buttons
    // For simplicity, we'll just store the new buttons
    return *this;
}

// Static dialog methods
void Dialog::showInfo(const std::string& title, const std::string& message) {
    Dialog dialog("info_dialog");
    dialog.setTitle(title);
    dialog.setMessage(message);
    dialog.setButtons({"OK"});
    dialog.init();
}

void Dialog::showWarning(const std::string& title, const std::string& message) {
    Dialog dialog("warning_dialog");
    dialog.setTitle(title);
    dialog.setMessage(message);
    dialog.setButtons({"OK"});
    dialog.init();
}

void Dialog::showError(const std::string& title, const std::string& message) {
    Dialog dialog("error_dialog");
    dialog.setTitle(title);
    dialog.setMessage(message);
    dialog.setButtons({"OK"});
    dialog.init();
}

void Dialog::showConfirm(const std::string& title, const std::string& message, std::function<void(bool)> onConfirm) {
    Dialog dialog("confirm_dialog");
    dialog.setTitle(title);
    dialog.setMessage(message);
    dialog.setButtons({"Cancel", "OK"});
    dialog.setOnButtonClick([onConfirm](int buttonIndex) {
        onConfirm(buttonIndex == 1);  // OK is index 1
    });
    dialog.init();
}

}  // namespace Wrapper
}  // namespace UI
