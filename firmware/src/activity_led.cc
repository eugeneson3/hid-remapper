#include "activity_led.h"

#include <bsp/board_api.h>

#include <hardware/timer.h>

#ifdef JARVIS_D13_STATUS_LED
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/platform.h>
#endif

static bool led_state = false;
static uint64_t turn_led_off_after = 0;

#ifdef JARVIS_D13_STATUS_LED
static_assert(PICO_DEFAULT_LED_PIN == 13, "Jarvis D13 status LED requires GPIO13");

static const uint16_t LED_PWM_TOP = 999;
static const uint16_t LED_PWM_MAX_LEVEL = 1000;
static const uint64_t LED_BREATH_HALF_PERIOD_US = 1000000;
static const uint64_t LED_BREATH_PERIOD_US = 2 * LED_BREATH_HALF_PERIOD_US;
static const uint64_t LED_UPDATE_INTERVAL_US = 10000;
static const uint16_t AUTO_ATTACK_DEFAULT_TTL_MS = 5000;
static const uint16_t AUTO_ATTACK_MIN_TTL_MS = 1000;
static const uint16_t AUTO_ATTACK_MAX_TTL_MS = 30000;

static bool auto_attack_running = false;
static uint64_t auto_attack_expires_at = 0;
static uint64_t breathing_started_at = 0;
static uint64_t next_led_update_at = 0;

static void set_led_level(uint16_t level) {
    pwm_set_gpio_level(PICO_DEFAULT_LED_PIN, level);
}

static uint16_t breathing_level(uint64_t now) {
    const uint64_t phase = (now - breathing_started_at) % LED_BREATH_PERIOD_US;
    const bool descending = phase < LED_BREATH_HALF_PERIOD_US;
    const uint64_t half_phase = descending ? phase : phase - LED_BREATH_HALF_PERIOD_US;
    const uint32_t position = half_phase * 1000 / LED_BREATH_HALF_PERIOD_US;
    const uint32_t smoothstep =
        (uint64_t) position * position * (3000 - 2 * position) / 1000000;

    return descending ? LED_PWM_MAX_LEVEL * (1000 - smoothstep) / 1000
                      : LED_PWM_MAX_LEVEL * smoothstep / 1000;
}
#endif

void activity_led_init() {
#ifdef JARVIS_D13_STATUS_LED
    gpio_set_function(PICO_DEFAULT_LED_PIN, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(PICO_DEFAULT_LED_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 120.0f);
    pwm_config_set_wrap(&config, LED_PWM_TOP);
    pwm_init(slice, &config, true);
    set_led_level(0);
#ifdef JARVIS_D13_LED_SELF_TEST
    activity_led_set_auto_attack_running(true);
#endif
#endif
}

void activity_led_on() {
#ifdef JARVIS_D13_STATUS_LED
    if (auto_attack_running) {
        return;
    }
#endif
    led_state = true;
#ifdef JARVIS_D13_STATUS_LED
    set_led_level(LED_PWM_MAX_LEVEL);
#else
    board_led_write(true);
#endif
    turn_led_off_after = time_us_64() + 50000;
}

void activity_led_off_maybe() {
#ifdef JARVIS_D13_STATUS_LED
    const uint64_t now = time_us_64();
    if (auto_attack_running) {
        if ((auto_attack_expires_at != 0) && (now >= auto_attack_expires_at)) {
            activity_led_clear_auto_attack_state();
            return;
        }
        if (now >= next_led_update_at) {
            set_led_level(breathing_level(now));
            next_led_update_at = now + LED_UPDATE_INTERVAL_US;
        }
        return;
    }
#else
    const uint64_t now = time_us_64();
#endif

    if (led_state && (now > turn_led_off_after)) {
        led_state = false;
#ifdef JARVIS_D13_STATUS_LED
        set_led_level(0);
#else
        board_led_write(false);
#endif
    }
}

void activity_led_set_auto_attack_running(bool running) {
#ifdef JARVIS_D13_STATUS_LED
    if (auto_attack_running == running) {
        if (!running) {
            led_state = false;
            turn_led_off_after = 0;
            set_led_level(0);
        }
        return;
    }

    auto_attack_running = running;
    led_state = false;
    turn_led_off_after = 0;
    if (running) {
        breathing_started_at = time_us_64();
        next_led_update_at = breathing_started_at + LED_UPDATE_INTERVAL_US;
        set_led_level(LED_PWM_MAX_LEVEL);
    } else {
        auto_attack_expires_at = 0;
        breathing_started_at = 0;
        next_led_update_at = 0;
        set_led_level(0);
    }
#else
    (void) running;
#endif
}

void activity_led_set_auto_attack_state(bool running, uint16_t ttl_ms) {
#ifdef JARVIS_D13_STATUS_LED
    if (!running) {
        activity_led_clear_auto_attack_state();
        return;
    }

    if (ttl_ms == 0) {
        ttl_ms = AUTO_ATTACK_DEFAULT_TTL_MS;
    } else if (ttl_ms < AUTO_ATTACK_MIN_TTL_MS) {
        ttl_ms = AUTO_ATTACK_MIN_TTL_MS;
    } else if (ttl_ms > AUTO_ATTACK_MAX_TTL_MS) {
        ttl_ms = AUTO_ATTACK_MAX_TTL_MS;
    }

    auto_attack_expires_at = time_us_64() + (uint64_t) ttl_ms * 1000;
    activity_led_set_auto_attack_running(true);
#else
    (void) running;
    (void) ttl_ms;
#endif
}

void activity_led_clear_auto_attack_state() {
#ifdef JARVIS_D13_STATUS_LED
    auto_attack_expires_at = 0;
#endif
    activity_led_set_auto_attack_running(false);
}
