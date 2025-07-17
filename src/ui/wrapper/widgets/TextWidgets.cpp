#include "TextWidgets.h"
#include <esp_log.h>

static const char* TAG = "TextWidgets";

namespace UI {
namespace Wrapper {

// =============================================================================
// LABEL IMPLEMENTATIONS
// =============================================================================

bool Label::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "Label already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_label_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create label widget");
        return false;
    }

    // Apply text configuration
    if (!text.empty()) {
        lv_label_set_text(widget, text.c_str());
    }
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_align(widget, textAlign, 0);
    lv_label_set_long_mode(widget, longModeType);

    markInitialized();
    ESP_LOGD(TAG, "Label created successfully: %s", widgetId.c_str());
    return true;
}

void Label::destroy() {
    WidgetBase<Label>::destroy();
}

void Label::update() {
    if (!isReady()) return;

    lv_label_set_text(widget, text.c_str());
    lv_label_set_long_mode(widget, longModeType);
    lv_obj_set_style_text_align(widget, textAlign, 0);
}

Label& Label::setText(const std::string& newText) {
    text = newText;
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, text.c_str()));
    return *this;
}

Label& Label::setText(const char* newText) {
    return setText(std::string(newText ? newText : ""));
}

Label& Label::appendText(const std::string& append) {
    text += append;
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, text.c_str()));
    return *this;
}

Label& Label::setHeadingStyle() {
    if (!widget) return *this;

    font = &lv_font_montserrat_24;
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_color(widget, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_text_align(widget, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_bottom(widget, 8, 0);
    return *this;
}

Label& Label::setBodyStyle() {
    if (!widget) return *this;

    font = &lv_font_montserrat_16;
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_color(widget, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_text_align(widget, LV_TEXT_ALIGN_LEFT, 0);
    return *this;
}

Label& Label::setCaptionStyle() {
    if (!widget) return *this;

    font = &lv_font_montserrat_12;
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_color(widget, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_align(widget, LV_TEXT_ALIGN_LEFT, 0);
    return *this;
}

Label& Label::setTextAlign(lv_text_align_t align) {
    textAlign = align;
    SAFE_WIDGET_OP(widget, lv_obj_set_style_text_align(widget, textAlign, 0));
    return *this;
}

Label& Label::setLongMode(lv_label_long_mode_t mode) {
    longModeType = mode;
    longMode = (mode != LV_LABEL_LONG_WRAP);
    SAFE_WIDGET_OP(widget, lv_label_set_long_mode(widget, longModeType));
    return *this;
}

// =============================================================================
// RICH TEXT IMPLEMENTATIONS
// =============================================================================

bool RichText::init(lv_obj_t* parentObj) {
    if (isInitialized) {
        ESP_LOGW(TAG, "RichText already initialized");
        return true;
    }

    parent = parentObj ? parentObj : lv_scr_act();
    widget = lv_label_create(parent);

    if (!widget) {
        ESP_LOGE(TAG, "Failed to create rich text widget");
        return false;
    }

    // Apply configuration
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_align(widget, textAlign, 0);
    lv_label_set_text(widget, content.c_str());

    markInitialized();
    ESP_LOGD(TAG, "RichText created successfully: %s", widgetId.c_str());
    return true;
}

void RichText::destroy() {
    WidgetBase<RichText>::destroy();
}

void RichText::update() {
    if (!isReady()) return;

    lv_label_set_text(widget, content.c_str());
    lv_obj_set_style_text_font(widget, font, 0);
    lv_obj_set_style_text_align(widget, textAlign, 0);
}

RichText& RichText::setContent(const std::string& newContent) {
    content = newContent;
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::addText(const std::string& text) {
    content += text;
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::addColoredText(const std::string& text, lv_color_t color) {
    // For now, just add the text. In a full implementation, you'd use LVGL's rich text features
    content += text;
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::addBoldText(const std::string& text) {
    content += "**" + text + "**";
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::addItalicText(const std::string& text) {
    content += "*" + text + "*";
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::addLineBreak() {
    content += "\n";
    SAFE_WIDGET_OP(widget, lv_label_set_text(widget, content.c_str()));
    return *this;
}

RichText& RichText::setFont(const lv_font_t* newFont) {
    font = newFont;
    SAFE_WIDGET_OP(widget, lv_obj_set_style_text_font(widget, font, 0));
    return *this;
}

RichText& RichText::setTextAlign(lv_text_align_t align) {
    textAlign = align;
    SAFE_WIDGET_OP(widget, lv_obj_set_style_text_align(widget, textAlign, 0));
    return *this;
}

}  // namespace Wrapper
}  // namespace UI
