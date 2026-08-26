#pragma once

#include <stdint.h>

void jarvis_diag_mount(uint8_t dev_addr, uint8_t instance, uint8_t protocol,
                       uint8_t const* descriptor, uint16_t descriptor_length);
void jarvis_diag_unmount(uint8_t dev_addr, uint8_t instance);
void jarvis_diag_raw_report(uint8_t dev_addr, uint8_t instance,
                            uint8_t const* report, uint16_t report_length);
void jarvis_diag_parsed_report(uint8_t encoded_interface,
                               uint8_t const* keyboard_bits,
                               uint8_t keyboard_bits_length);
void jarvis_injection_update(void);
uint8_t jarvis_injection_row(uint8_t row);
void jarvis_injection_clear(void);
