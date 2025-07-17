#pragma once

#include "../base/WidgetBase.h"
#include <functional>

namespace UI {
namespace Wrapper {

// =============================================================================
// INPUT WIDGETS
// =============================================================================

class NumberInput : public WidgetBase<NumberInput> {
   protected:
    std::string value;
    int minValue = 0;
    int maxValue = 999;
    int step = 1;
    std::function<void(int)> onChangeCallback;

   public:
    NumberInput() = default;
    NumberInput(const std::string& id, const std::string& initialValue = "0") : value(initialValue) { setId(id); }
    virtual ~NumberInput() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Value manipulation
    NumberInput& setValue(int newValue);
    NumberInput& setValue(const std::string& newValue);
    NumberInput& increment();
    NumberInput& decrement();

    // Configuration
    NumberInput& setRange(int min, int max) {
        minValue = min;
        maxValue = max;
        return *this;
    }

    NumberInput& setStep(int newStep) {
        step = newStep;
        return *this;
    }

    NumberInput& setOnChange(std::function<void(int)> callback) {
        onChangeCallback = callback;
        return *this;
    }

    // Getters
    const std::string& getValue() const { return value; }
    int getIntValue() const { return std::stoi(value); }
    int getMinValue() const { return minValue; }
    int getMaxValue() const { return maxValue; }
    int getStep() const { return step; }
};

class ToggleButton : public WidgetBase<ToggleButton> {
   protected:
    std::string text;
    bool isToggled = false;
    std::function<void(bool)> onToggleCallback;

   public:
    ToggleButton() = default;
    ToggleButton(const std::string& id, const std::string& text) : text(text) { setId(id); }
    virtual ~ToggleButton() = default;

    virtual bool init(lv_obj_t* parentObj = nullptr);
    virtual void destroy();
    virtual void update();

    // Toggle manipulation
    ToggleButton& setToggled(bool toggled);
    ToggleButton& toggle();
    ToggleButton& setText(const std::string& newText);

    // Configuration
    ToggleButton& setOnToggle(std::function<void(bool)> callback) {
        onToggleCallback = callback;
        return *this;
    }

    // Getters
    const std::string& getText() const { return text; }
    bool getToggled() const { return isToggled; }
};

}  // namespace Wrapper
}  // namespace UI
