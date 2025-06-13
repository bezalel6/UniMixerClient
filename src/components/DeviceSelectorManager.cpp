#include "DeviceSelectorManager.h"
#include <algorithm>
#include "../include/UIConstants.h"

namespace UI {
namespace Components {

DeviceSelectorManager::DeviceSelectorManager()
    : mainDropdown(nullptr), balanceDropdown1(nullptr), balanceDropdown2(nullptr) {}

DeviceSelectorManager::~DeviceSelectorManager() {
    deinitialize();
}

bool DeviceSelectorManager::initialize(lv_obj_t* main, lv_obj_t* bal1, lv_obj_t* bal2) {
    mainDropdown = main;
    balanceDropdown1 = bal1;
    balanceDropdown2 = bal2;
    mainSelection = UI_LABEL_EMPTY;
    balanceSelection1 = UI_LABEL_EMPTY;
    balanceSelection2 = UI_LABEL_EMPTY;
    return true;
}

void DeviceSelectorManager::deinitialize() {
    mainDropdown = nullptr;
    balanceDropdown1 = nullptr;
    balanceDropdown2 = nullptr;
    mainSelection = UI_LABEL_EMPTY;
    balanceSelection1 = UI_LABEL_EMPTY;
    balanceSelection2 = UI_LABEL_EMPTY;
}

void DeviceSelectorManager::setMainSelection(const String& deviceName) {
    mainSelection = deviceName;
    if (mainDropdown) setDropdownSelection(mainDropdown, deviceName);
}

String DeviceSelectorManager::getMainSelection() const {
    return mainSelection;
}

void DeviceSelectorManager::setBalanceSelections(const String& device1, const String& device2) {
    balanceSelection1 = device1;
    balanceSelection2 = device2;
    if (balanceDropdown1) setDropdownSelection(balanceDropdown1, device1);
    if (balanceDropdown2) setDropdownSelection(balanceDropdown2, device2);
    ensureBalanceExclusivity();
}

String DeviceSelectorManager::getBalanceSelection1() const {
    return balanceSelection1;
}

String DeviceSelectorManager::getBalanceSelection2() const {
    return balanceSelection2;
}

String DeviceSelectorManager::getSelectedDeviceForTab(int tabIndex) const {
    switch (tabIndex) {
        case 0:  // MASTER tab - return main selection
            return mainSelection;
        case 1:  // SINGLE tab - return main selection
            return mainSelection;
        case 2:  // BALANCE tab - return first balance selection
            return balanceSelection1;
        default:
            return "";
    }
}

void DeviceSelectorManager::setSelectedDeviceForTab(int tabIndex, const String& deviceName) {
    switch (tabIndex) {
        case 0:  // MASTER tab
        case 1:  // SINGLE tab
            setMainSelection(deviceName);
            break;
        case 2:  // BALANCE tab
            setBalanceSelections(deviceName, balanceSelection2);
            break;
    }
}

void DeviceSelectorManager::refreshAllDropdowns(const std::vector<AudioLevel>& audioLevels) {
    if (mainDropdown) updateDropdownOptions(mainDropdown, audioLevels);
    if (balanceDropdown1) updateDropdownOptions(balanceDropdown1, audioLevels);
    if (balanceDropdown2) updateDropdownOptions(balanceDropdown2, audioLevels);
    ensureBalanceExclusivity();
}

void DeviceSelectorManager::setDropdownSelection(lv_obj_t* dropdown, const String& deviceName) {
    if (!dropdown) return;

    // Get the options string and parse it
    const char* options = lv_dropdown_get_options(dropdown);
    if (!options) return;

    String optionsStr = String(options);
    int optionIndex = 0;
    int startPos = 0;

    while (startPos < optionsStr.length()) {
        int endPos = optionsStr.indexOf('\n', startPos);
        if (endPos == -1) endPos = optionsStr.length();

        String option = optionsStr.substring(startPos, endPos);
        if (extractDeviceNameFromDropdownText(option) == deviceName) {
            lv_dropdown_set_selected(dropdown, optionIndex);
            break;
        }

        startPos = endPos + 1;
        optionIndex++;
    }
}

String DeviceSelectorManager::getDropdownSelection(lv_obj_t* dropdown) const {
    if (!dropdown) return "";
    char buf[128];
    lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
    return extractDeviceNameFromDropdownText(String(buf));
}

void DeviceSelectorManager::initializeBalanceSelections(const std::vector<AudioLevel>& audioLevels) {
    if (audioLevels.size() < 2) return;
    setBalanceSelections(audioLevels[0].processName, audioLevels[1].processName);
}

void DeviceSelectorManager::updateDropdownOptions(lv_obj_t* dropdown, const std::vector<AudioLevel>& audioLevels) {
    if (!dropdown) return;
    lv_dropdown_clear_options(dropdown);
    for (const auto& level : audioLevels) {
        String displayName = level.stale ? String("(!) ") + level.processName : level.processName;
        lv_dropdown_add_option(dropdown, displayName.c_str(), LV_DROPDOWN_POS_LAST);
    }
}

void DeviceSelectorManager::ensureBalanceExclusivity() {
    // Prevent the same device from being selected in both balance dropdowns
    if (balanceSelection1 == balanceSelection2 && !balanceSelection1.isEmpty()) {
        // If both are the same, clear the second
        balanceSelection2 = "";
        if (balanceDropdown2) lv_dropdown_set_selected(balanceDropdown2, 0);
    }
}

String DeviceSelectorManager::extractDeviceNameFromDropdownText(const String& dropdownText) const {
    String s = dropdownText;
    if (s.startsWith("(!) ")) s = s.substring(4);
    int idx = s.indexOf(" (");
    if (idx > 0) s = s.substring(0, idx);
    return s;
}

}  // namespace Components
}  // namespace UI