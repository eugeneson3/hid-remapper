#ifndef JARVIS_C0_TUSB_CONFIG_H_
#define JARVIS_C0_TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID 1
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_HID_EP_BUFSIZE 16

#define BOARD_TUH_RHPORT 1
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// C0 supports one directly connected keyboard and deliberately excludes hubs.
// A keyboard may expose several HID interfaces, so leave four interface slots
// available while accepting only the first boot-keyboard interface.
#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_HID 4
#define CFG_TUH_CDC 0
#define CFG_TUH_MSC 0
#define CFG_TUH_VENDOR 0
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#define CFG_TUH_RPI_PIO_USB 1

#endif
