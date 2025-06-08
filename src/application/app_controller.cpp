#include "app_controller.h"
#include "../display/display_manager.h"
#include "../hardware/device_manager.h"
#include "../hardware/network_manager.h"
#include "../events/ui_event_handlers.h"
#include <ui/ui.h>

// Private variables
static unsigned long next_update_millis = 0;

bool app_controller_init(void) {
    // Initialize hardware/device manager
    if (!device_manager_init()) {
        return false;
    }

    // Initialize network manager
    if (!network_manager_init()) {
        return false;
    }

    // Initialize display manager
    if (!display_manager_init()) {
        return false;
    }

    // Setup UI components
    app_controller_setup_ui_components();

    // Initialize timing
    next_update_millis = device_get_millis() + APP_UPDATE_INTERVAL_MS;

    return true;
}

void app_controller_deinit(void) {
    network_manager_deinit();
    display_manager_deinit();
    device_manager_deinit();
}

void app_controller_run(void) {
    unsigned long now = device_get_millis();

    // Update network manager
    network_manager_update();

    // Update periodic data
    if (now >= next_update_millis) {
        app_controller_update_periodic_data();
        app_controller_update_network_status();
        next_update_millis = now + APP_UPDATE_INTERVAL_MS;
    }

#ifdef BOARD_HAS_RGB_LED
    // Update LED colors
    device_led_cycle_colors();
#endif

    // Update display
    display_manager_update();
}

void app_controller_setup_ui_components(void) {
    // Create QR code component
    display_create_qr_code(ui_scrMain, APP_QR_CODE_DATA, APP_QR_CODE_SIZE);

    // Set display to 180 degrees rotation
    display_set_rotation(DISPLAY_ROTATION_180);
}

void app_controller_update_periodic_data(void) {
    unsigned long now = device_get_millis();

    // Update milliseconds counter
    display_update_label_uint32(ui_lblMillisecondsValue, now);

#ifdef BOARD_HAS_CDS
    // Update light sensor reading
    uint32_t light_sensor_mv = device_read_light_sensor_mv();
    display_update_label_uint32(ui_lblCdrValue, light_sensor_mv);
#endif
}

void app_controller_update_network_status(void) {
    // Get network status
    const char* wifi_status = network_get_wifi_status_string();
    bool is_connected = network_is_connected();
    const char* ssid = network_get_ssid();
    const char* ip_address = network_get_ip_address();

    // Update WiFi status and indicator
    display_update_wifi_status(ui_lblWifiStatus, ui_objWifiIndicator, wifi_status, is_connected);

    // Update network information
    display_update_network_info(ui_lblSSIDValue, ui_lblIPValue, ssid, ip_address);
}