/* USB device descriptors for the Jarvis C0 boot-keyboard-only target. */

#include <stddef.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#define USB_VID 0x046D
#define USB_PID 0xC52B

static tusb_desc_device_t const device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &device_descriptor;
}

static uint8_t const keyboard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return keyboard_report_descriptor;
}

enum {
    ITF_NUM_KEYBOARD,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_KEYBOARD_IN 0x81

static uint8_t const configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LENGTH, 0, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(keyboard_report_descriptor), EPNUM_KEYBOARD_IN,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return configuration_descriptor;
}

enum {
    STRING_ID_LANGUAGE = 0,
    STRING_ID_MANUFACTURER,
    STRING_ID_PRODUCT,
    STRING_ID_SERIAL,
};

static char const *string_descriptors[] = {
    (char const[]) {0x09, 0x04},
    "Jarvis",
    "Jarvis C0 Keyboard Passthrough",
    NULL,
};

static uint16_t string_descriptor[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    (void) language_id;

    size_t character_count;
    if (index == STRING_ID_LANGUAGE) {
        memcpy(&string_descriptor[1], string_descriptors[0], 2);
        character_count = 1;
    } else if (index == STRING_ID_SERIAL) {
        character_count = board_usb_get_serial(string_descriptor + 1, 32);
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
            return NULL;
        }

        char const *text = string_descriptors[index];
        character_count = strlen(text);
        if (character_count > 32) {
            character_count = 32;
        }

        for (size_t i = 0; i < character_count; ++i) {
            string_descriptor[1 + i] = (uint8_t) text[i];
        }
    }

    string_descriptor[0] = (uint16_t) ((TUSB_DESC_STRING << 8) |
                                        (2 * character_count + 2));
    return string_descriptor;
}
