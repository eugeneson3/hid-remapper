#ifndef _ACTIVITY_LED_H_
#define _ACTIVITY_LED_H_

void activity_led_on();
void activity_led_set_auto_attack(bool running, unsigned short ttl_ms);
void activity_led_off_maybe();

#endif
