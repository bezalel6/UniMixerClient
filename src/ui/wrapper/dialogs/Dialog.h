#pragma once

#include "../base/WidgetBase.h"
#include <functional>

namespace UI {
namespace Wrapper {

// =============================================================================
// DIALOG SYSTEM
// =============================================================================

class Dialog : public WidgetBase<Dialog> {
   protected:
    std::string title;
    std::string message;
    std::vector<std::string> buttons;
    std::function<void(int)> onButtonClick;

   public:
    Dialog() = default;
    Dialog(const std::string& id) { setId(id); }
    virtual ~Dialog() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Dialog configuration
    Dialog& setTitle(const std::string& newTitle);
    Dialog& setMessage(const std::string& newMessage);
    Dialog& setButtons(const std::vector<std::string>& newButtons);
    Dialog& setOnButtonClick(std::function<void(int)> callback) {
        onButtonClick = callback;
        return *this;
    }

    // Static dialog methods
    static void showInfo(const std::string& title, const std::string& message);
    static void showWarning(const std::string& title, const std::string& message);
    static void showError(const std::string& title, const std::string& message);
    static void showConfirm(const std::string& title, const std::string& message,
                            std::function<void(bool)> onConfirm);

    // Getters
    const std::string& getTitle() const { return title; }
    const std::string& getMessage() const { return message; }
    const std::vector<std::string>& getButtons() const { return buttons; }
};

}  // namespace Wrapper
}  // namespace UI
