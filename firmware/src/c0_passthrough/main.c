/*
 * Jarvis C0 keyboard passthrough.
 *
 * This target intentionally implements only one path:
 * a directly connected USB boot keyboard -> a PC boot keyboard report.
 * It has no remapping, configuration, injection, report parser, queue,
 * watchdog, heartbeat, or persistent state.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "pico/time.h"
#include "pio_usb.h"
#include "tusb.h"

static repeating_timer_t host_sof_timer;

static bool active_keyboard;
static uint8_t active_dev_addr;
static uint8_t active_instance;

static hid_keyboard_report_t physical_report;
static bool report_pending = true;

static bool __no_inline_not_in_flash_func(host_sof_callback)(repeating_timer_t *timer) {
    (void) timer;
    pio_usb_host_frame();
    return true;
}

static void configure_pio_usb_host(void) {
    pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    config.skip_alarm_pool = true;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config);

    add_repeating_timer_us(-1000, host_sof_callback, NULL, &host_sof_timer);
}

static void set_physical_report(hid_keyboard_report_t const *report) {
    if (memcmp(&physical_report, report, sizeof(physical_report)) != 0) {
        physical_report = *report;
        report_pending = true;
    }
}

static void clear_physical_report(void) {
    hid_keyboard_report_t const released = {0};
    set_physical_report(&released);
}

static void send_pending_report(void) {
    if (!report_pending || !tud_hid_ready()) {
        return;
    }

    if (tud_hid_keyboard_report(0, physical_report.modifier, physical_report.keycode)) {
        report_pending = false;
    }
}

int main(void) {
    board_init();
    configure_pio_usb_host();
    tusb_init();

    while (true) {
        tuh_task();
        tud_task();
        send_pending_report();
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *report_descriptor, uint16_t descriptor_length) {
    (void) report_descriptor;
    (void) descriptor_length;

    if (active_keyboard ||
        tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_KEYBOARD) {
        return;
    }

    active_keyboard = true;
    active_dev_addr = dev_addr;
    active_instance = instance;
    clear_physical_report();
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (!active_keyboard || dev_addr != active_dev_addr || instance != active_instance) {
        return;
    }

    active_keyboard = false;
    active_dev_addr = 0;
    active_instance = 0;
    clear_physical_report();
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t length) {
    if (!active_keyboard || dev_addr != active_dev_addr || instance != active_instance) {
        return;
    }

    if (length == sizeof(hid_keyboard_report_t)) {
        hid_keyboard_report_t next_report;
        memcpy(&next_report, report, sizeof(next_report));
        set_physical_report(&next_report);
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tud_mount_cb(void) {
    report_pending = true;
}

void tud_resume_cb(void) {
    report_pending = true;
}

void tud_umount_cb(void) {
}

void tud_suspend_cb(bool remote_wakeup_enabled) {
    (void) remote_wakeup_enabled;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t requested_length) {
    (void) instance;
    (void) report_id;

    if (report_type != HID_REPORT_TYPE_INPUT || requested_length < sizeof(physical_report)) {
        return 0;
    }

    memcpy(buffer, &physical_report, sizeof(physical_report));
    return sizeof(physical_report);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t buffer_size) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) buffer_size;
}
