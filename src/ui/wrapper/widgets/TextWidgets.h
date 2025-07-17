#pragma once

#include "../base/WidgetBase.h"

namespace UI {
namespace Wrapper {

// =============================================================================
// TEXT WIDGETS
// =============================================================================

class Label : public WidgetBase<Label> {
   protected:
    std::string text;
    lv_text_align_t textAlign = LV_TEXT_ALIGN_LEFT;
    bool longMode = false;
    lv_label_long_mode_t longModeType = LV_LABEL_LONG_WRAP;
    const lv_font_t* font = &lv_font_montserrat_16;

   public:
    Label() = default;
    Label(const std::string& id) { setId(id); }
    Label(const std::string& id, const char* initialText) {
        setId(id);
        text = initialText ? initialText : "";
    }
    virtual ~Label() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Text content
    Label& setText(const std::string& newText);
    Label& setText(const char* newText);
    Label& appendText(const std::string& append);

    // Style presets
    Label& setHeadingStyle();
    Label& setBodyStyle();
    Label& setCaptionStyle();

    Label& setTextAlign(lv_text_align_t align);
    Label& setLongMode(lv_label_long_mode_t mode);

    // Getters
    const std::string& getText() const { return text; }
    lv_text_align_t getTextAlign() const { return textAlign; }
    bool isLongMode() const { return longMode; }
    lv_label_long_mode_t getLongModeType() const { return longModeType; }
};

class RichText : public WidgetBase<RichText> {
   protected:
    std::string content;
    const lv_font_t* font = &lv_font_montserrat_16;
    lv_text_align_t textAlign = LV_TEXT_ALIGN_LEFT;

   public:
    RichText() = default;
    RichText(const std::string& id) { setId(id); }
    virtual ~RichText() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Rich text manipulation
    RichText& setContent(const std::string& newContent);
    RichText& addText(const std::string& text);
    RichText& addColoredText(const std::string& text, lv_color_t color);
    RichText& addBoldText(const std::string& text);
    RichText& addItalicText(const std::string& text);
    RichText& addLineBreak();

    RichText& setFont(const lv_font_t* newFont);
    RichText& setTextAlign(lv_text_align_t align);

    // Getters
    const std::string& getContent() const { return content; }
    const lv_font_t* getFont() const { return font; }
    lv_text_align_t getTextAlign() const { return textAlign; }
};

}  // namespace Wrapper
}  // namespace UI
