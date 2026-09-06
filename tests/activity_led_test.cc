#include <cassert>
#include <cstdio>
#include "activity_led.h"
#include <stdint.h>

static uint64_t now=1;
static uint16_t brightness=0;
uint64_t time_us_64() { return now; }
void board_led_write(bool) {}
void gpio_set_function(unsigned pin, unsigned) { assert(pin==13); }
unsigned pwm_gpio_to_slice_num(unsigned pin) { assert(pin==13); return 6; }
void pwm_set_wrap(unsigned, unsigned wrap) { assert(wrap==255); }
void pwm_set_clkdiv(unsigned, float) {}
void pwm_set_enabled(unsigned, bool) {}
void pwm_set_gpio_level(unsigned pin, uint16_t level) { assert(pin==13); assert(level<=255); brightness=level; }

int main() {
    activity_led_off_maybe(); assert(brightness==0);
    activity_led_on(); assert(brightness==255);
    now+=50001; activity_led_off_maybe(); assert(brightness==0);
    activity_led_set_auto_attack(true,5000);
    now+=1020000; activity_led_off_maybe(); assert(brightness==255);
    activity_led_set_auto_attack(true,5000); activity_led_off_maybe(); assert(brightness==255);
    now+=5010000; activity_led_off_maybe(); assert(brightness==0);
    activity_led_set_auto_attack(true,5000); now+=1020000; activity_led_off_maybe(); assert(brightness==255);
    activity_led_set_auto_attack(false,0); assert(brightness==0);
    now+=100000; activity_led_off_maybe(); assert(brightness==0);
    std::puts("PASS D13 reset OFF, 50ms pulse, PWM breathing, heartbeat phase, TTL and explicit OFF");
}
