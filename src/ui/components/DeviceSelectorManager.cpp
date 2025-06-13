#include "DeviceSelectorManager.h"
#include "../../application/AudioStatusManager.h"
#include <esp_log.h>

static const char* TAG = "DeviceSelectorManager";

namespace UI {
namespace Components {

// Use the AudioLevel from Application::Audio namespace
using AudioLevel = Application::Audio::AudioLevel;

DeviceSelectorManager::DeviceSelectorManager() : initialized(false) {
}

DeviceSelectorManager::~DeviceSelectorManager() {
    deinitialize();
}

bool DeviceSelectorManager::initialize(lv_obj_t* mainDropdown, lv_obj_t* balanceLeftDropdown, lv_obj_t* balanceRightDropdown) {
    if (initialized) {
        ESP_LOGW(TAG, "DeviceSelectorManager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing DeviceSelectorManager");

    // Create main selector
    mainSelector = std::make_unique<MainDeviceSelector>(mainDropdown);
    if (!mainSelector) {
        ESP_LOGE(TAG, "Failed to create main device selector");
        return false;
    }

    // Create balance selectors
    balanceLeftSelector = std::make_unique<BalanceDeviceSelector>(balanceLeftDropdown);
    balanceRightSelector = std::make_unique<BalanceDeviceSelector>(balanceRightDropdown);

    if (!balanceLeftSelector || !balanceRightSelector) {
        ESP_LOGE(TAG, "Failed to create balance device selectors");
        return false;
    }

    // Set up mutual exclusivity between balance selectors
    balanceLeftSelector->setOtherSelector(balanceRightSelector.get());
    balanceRightSelector->setOtherSelector(balanceLeftSelector.get());

    initialized = true;
    ESP_LOGI(TAG, "DeviceSelectorManager initialized successfully");
    return true;
}

void DeviceSelectorManager::deinitialize() {
    if (!initialized) return;

    ESP_LOGI(TAG, "Deinitializing DeviceSelectorManager");

    mainSelector.reset();
    balanceLeftSelector.reset();
    balanceRightSelector.reset();

    initialized = false;
}

void DeviceSelectorManager::setMainSelection(const String& deviceName) {
    if (!initialized || !mainSelector) return;
    mainSelector->setSelection(deviceName);
}

String DeviceSelectorManager::getMainSelection() const {
    if (!initialized || !mainSelector) return "";
    return mainSelector->getSelection();
}

void DeviceSelectorManager::clearMainSelection() {
    if (!initialized || !mainSelector) return;
    mainSelector->clearSelection();
}

void DeviceSelectorManager::setBalanceLeftSelection(const String& deviceName) {
    if (!initialized || !balanceLeftSelector) return;
    balanceLeftSelector->setSelection(deviceName);
}

void DeviceSelectorManager::setBalanceRightSelection(const String& deviceName) {
    if (!initialized || !balanceRightSelector) return;
    balanceRightSelector->setSelection(deviceName);
}

String DeviceSelectorManager::getBalanceLeftSelection() const {
    if (!initialized || !balanceLeftSelector) return "";
    return balanceLeftSelector->getSelection();
}

String DeviceSelectorManager::getBalanceRightSelection() const {
    if (!initialized || !balanceRightSelector) return "";
    return balanceRightSelector->getSelection();
}

void DeviceSelectorManager::clearBalanceSelections() {
    if (!initialized) return;

    if (balanceLeftSelector) balanceLeftSelector->clearSelection();
    if (balanceRightSelector) balanceRightSelector->clearSelection();
}

void DeviceSelectorManager::setDropdownSelection(lv_obj_t* dropdown, const String& deviceName) {
    if (!initialized) return;

    if (mainSelector && mainSelector->getDropdown() == dropdown) {
        setMainSelection(deviceName);
    } else if (balanceLeftSelector && balanceLeftSelector->getDropdown() == dropdown) {
        setBalanceLeftSelection(deviceName);
    } else if (balanceRightSelector && balanceRightSelector->getDropdown() == dropdown) {
        setBalanceRightSelection(deviceName);
    }
}

String DeviceSelectorManager::getDropdownSelection(lv_obj_t* dropdown) const {
    if (!initialized) return "";

    if (mainSelector && mainSelector->getDropdown() == dropdown) {
        return getMainSelection();
    } else if (balanceLeftSelector && balanceLeftSelector->getDropdown() == dropdown) {
        return getBalanceLeftSelection();
    } else if (balanceRightSelector && balanceRightSelector->getDropdown() == dropdown) {
        return getBalanceRightSelection();
    }

    return "";
}

void DeviceSelectorManager::clearAllSelections() {
    if (!initialized) return;

    clearMainSelection();
    clearBalanceSelections();
}

void DeviceSelectorManager::refreshAllDropdowns(const std::vector<AudioLevel>& audioLevels) {
    if (!initialized) return;

    if (mainSelector) {
        mainSelector->refresh(audioLevels);
    }
    if (balanceLeftSelector) {
        balanceLeftSelector->refresh(audioLevels);
    }
    if (balanceRightSelector) {
        balanceRightSelector->refresh(audioLevels);
    }
}

void DeviceSelectorManager::refreshDropdown(lv_obj_t* dropdown, const std::vector<AudioLevel>& audioLevels) {
    if (!initialized) return;

    if (mainSelector && mainSelector->getDropdown() == dropdown) {
        mainSelector->refresh(audioLevels);
    } else if (balanceLeftSelector && balanceLeftSelector->getDropdown() == dropdown) {
        balanceLeftSelector->refresh(audioLevels);
    } else if (balanceRightSelector && balanceRightSelector->getDropdown() == dropdown) {
        balanceRightSelector->refresh(audioLevels);
    }
}

bool DeviceSelectorManager::isAvailableFor(const String& deviceName, lv_obj_t* dropdown) const {
    if (!initialized) return false;

    if (mainSelector && mainSelector->getDropdown() == dropdown) {
        return mainSelector->isAvailableFor(deviceName);
    } else if (balanceLeftSelector && balanceLeftSelector->getDropdown() == dropdown) {
        return balanceLeftSelector->isAvailableFor(deviceName);
    } else if (balanceRightSelector && balanceRightSelector->getDropdown() == dropdown) {
        return balanceRightSelector->isAvailableFor(deviceName);
    }

    return false;
}

void DeviceSelectorManager::initializeBalanceSelections(const std::vector<AudioLevel>& audioLevels) {
    if (!initialized || !balanceLeftSelector || !balanceRightSelector) return;

    // Check if both balance dropdowns are currently unselected
    String balanceLeft = getBalanceLeftSelection();
    String balanceRight = getBalanceRightSelection();

    // If both are empty or both have the same selection, initialize them with different devices
    if ((balanceLeft.isEmpty() && balanceRight.isEmpty()) ||
        (!balanceLeft.isEmpty() && balanceLeft == balanceRight)) {
        ESP_LOGI(TAG, "Initializing balance dropdown selections to ensure mutual exclusivity");

        // Find two different non-stale devices
        String firstDevice = "";
        String secondDevice = "";

        for (const auto& level : audioLevels) {
            if (!level.stale) {
                if (firstDevice.isEmpty()) {
                    firstDevice = level.processName;
                } else if (secondDevice.isEmpty() && level.processName != firstDevice) {
                    secondDevice = level.processName;
                    break;
                }
            }
        }

        // If we couldn't find two non-stale devices, try with all devices
        if (secondDevice.isEmpty() && audioLevels.size() >= 2) {
            firstDevice = audioLevels[0].processName;
            secondDevice = audioLevels[1].processName;
        }

        // Set the selections if we found different devices
        if (!firstDevice.isEmpty() && !secondDevice.isEmpty()) {
            setBalanceLeftSelection(firstDevice);
            setBalanceRightSelection(secondDevice);

            // Refresh the UI
            refreshAllDropdowns(audioLevels);

            ESP_LOGI(TAG, "Initialized balance selections: Left=%s, Right=%s",
                     firstDevice.c_str(), secondDevice.c_str());
        } else if (!firstDevice.isEmpty()) {
            // Only one device available - set one dropdown and leave the other empty
            setBalanceLeftSelection(firstDevice);
            setBalanceRightSelection("");

            // Refresh the UI
            refreshAllDropdowns(audioLevels);

            ESP_LOGI(TAG, "Only one device available - initialized left balance selection: %s",
                     firstDevice.c_str());
        }
    }
}

}  // namespace Components
}  // namespace UI