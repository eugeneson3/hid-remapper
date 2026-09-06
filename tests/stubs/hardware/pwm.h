#pragma once
#include <stdint.h>
unsigned pwm_gpio_to_slice_num(unsigned);
void pwm_set_wrap(unsigned, unsigned);
void pwm_set_clkdiv(unsigned, float);
void pwm_set_enabled(unsigned, bool);
void pwm_set_gpio_level(unsigned, uint16_t);
