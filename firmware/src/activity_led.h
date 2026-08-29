#ifndef _ACTIVITY_LED_H_
#define _ACTIVITY_LED_H_

#include <stdint.h>

void activity_led_init();
void activity_led_on();
void activity_led_off_maybe();
void activity_led_set_auto_attack_running(bool running);
void activity_led_set_auto_attack_state(bool running, uint16_t ttl_ms);
void activity_led_clear_auto_attack_state();

#endif
