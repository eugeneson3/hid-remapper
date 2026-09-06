#include "activity_led.h"
#include <bsp/board_api.h>
#ifdef FEATHER_HOST_BOARD
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#endif
#include <hardware/timer.h>

static uint64_t pulse_until = 0;
static uint64_t running_until = 0;
static uint64_t phase_start = 0;
#ifdef FEATHER_HOST_BOARD
static bool pwm_ready = false;
#endif

static void write_level(uint16_t level) {
#ifdef FEATHER_HOST_BOARD
    if (!pwm_ready) {
        gpio_set_function(PICO_DEFAULT_LED_PIN, GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(PICO_DEFAULT_LED_PIN);
        pwm_set_wrap(slice, 255);
        pwm_set_clkdiv(slice, 16.0f);
        pwm_set_enabled(slice, true);
        pwm_ready = true;
    }
    pwm_set_gpio_level(PICO_DEFAULT_LED_PIN, level);
#else
    board_led_write(level != 0);
#endif
}

void activity_led_set_auto_attack(bool running, unsigned short ttl_ms) {
    uint64_t now = time_us_64();
    if (!running) {
        running_until = pulse_until = 0;
        write_level(0);
        return;
    }
    if (!running_until || now >= running_until) phase_start = now;
    if (!ttl_ms || ttl_ms > 10000) ttl_ms = 5000;
    running_until = now + (uint64_t)ttl_ms * 1000;
}

void activity_led_on() {
    if (!running_until) { pulse_until = time_us_64() + 50000; write_level(255); }
}

void activity_led_off_maybe() {
    uint64_t now = time_us_64();
    if (running_until && now >= running_until) { activity_led_set_auto_attack(false, 0); return; }
    if (running_until) {
        uint32_t phase = ((now - phase_start) / 4000) % 512;
        uint32_t linear = phase < 256 ? phase : 511 - phase;
        write_level((linear * linear) / 255);
    } else if (pulse_until && now >= pulse_until) { pulse_until = 0; write_level(0); }
}
