#include "jarvis_status.h"

#include <algorithm>

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/pio_instructions.h>
#include <pico/stdlib.h>

#include "remapper.h"

namespace {

constexpr uint64_t LED_FRAME_US = 10000;
constexpr uint64_t BREATH_HALF_CYCLE_US = 1000000;
constexpr uint16_t DEFAULT_STATUS_TTL_MS = 1500;

bool auto_hunting_enabled = false;
uint64_t auto_hunting_expires_at = 0;
uint64_t auto_hunting_started_at = 0;
uint64_t next_led_frame_at = 0;
bool led_is_off = true;

bool pause_latched = false;
bool arrow_latched[4] = { false, false, false, false };

#if defined(FEATHER_HOST_BOARD) && defined(PICO_DEFAULT_WS2812_PIN)
PIO led_pio = pio0;
int led_sm = -1;

constexpr uint WS2812_T1 = 2;
constexpr uint WS2812_T2 = 5;
constexpr uint WS2812_T3 = 3;
const uint16_t ws2812_instructions[] = {
    (uint16_t) (pio_encode_out(pio_x, 1) | pio_encode_sideset(1, 0) | pio_encode_delay(WS2812_T3 - 1)),
    (uint16_t) (pio_encode_jmp_not_x(3) | pio_encode_sideset(1, 1) | pio_encode_delay(WS2812_T1 - 1)),
    (uint16_t) (pio_encode_jmp(0) | pio_encode_sideset(1, 1) | pio_encode_delay(WS2812_T2 - 1)),
    (uint16_t) (pio_encode_nop() | pio_encode_sideset(1, 0) | pio_encode_delay(WS2812_T2 - 1)),
};
const pio_program_t ws2812_program = {
    .instructions = ws2812_instructions,
    .length = 4,
    .origin = -1,
    .pio_version = 0,
};

void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset, offset + 3);
    sm_config_set_sideset(&config, 1, false, false);
    sm_config_set_sideset_pins(&config, pin);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    float div = clock_get_hz(clk_sys) / (freq * (WS2812_T1 + WS2812_T2 + WS2812_T3));
    sm_config_set_clkdiv(&config, div);

    pio_sm_init(pio, sm, offset, &config);
    pio_sm_set_enabled(pio, sm, true);
}

uint8_t smooth_gamma_level(uint32_t x) {
    // Smoothstep produces ease-in/ease-out in perceptual space. Squaring the
    // result approximates gamma 2.0 so the visible fade is not top-heavy.
    uint64_t smooth = (uint64_t) x * x * (3000 - 2 * x) / 1000000;
    return (uint8_t) ((smooth * smooth * 255 + 500000) / 1000000);
}

void write_green(uint8_t level) {
    if (led_sm < 0) {
        return;
    }
    uint32_t grb = (uint32_t) level << 16;
    pio_sm_put_blocking(led_pio, led_sm, grb << 8u);
    led_is_off = level == 0;
}
#endif

void emit_shortcut(JarvisShortcut shortcut) {
    monitor_usage((uint32_t) shortcut, 1, 0);
}

}  // namespace

void jarvis_status_init() {
#if defined(FEATHER_HOST_BOARD) && defined(PICO_DEFAULT_WS2812_PIN)
    // USB Host already owns PIO0/SM0.  If no remaining PIO resource is
    // available, keep the remapper running and disable only the status LED.
    if (!pio_can_add_program(led_pio, &ws2812_program)) {
        return;
    }
    led_sm = pio_claim_unused_sm(led_pio, false);
    if (led_sm < 0) {
        return;
    }
    uint offset = pio_add_program(led_pio, &ws2812_program);
#ifdef PICO_DEFAULT_WS2812_POWER_PIN
    gpio_init(PICO_DEFAULT_WS2812_POWER_PIN);
    gpio_set_dir(PICO_DEFAULT_WS2812_POWER_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_WS2812_POWER_PIN, 1);
#endif
    ws2812_program_init(led_pio, led_sm, offset, PICO_DEFAULT_WS2812_PIN, 800000);
    write_green(0);
#endif
}

void jarvis_status_task(uint64_t now_us) {
#if defined(FEATHER_HOST_BOARD) && defined(PICO_DEFAULT_WS2812_PIN)
    if (auto_hunting_enabled && now_us >= auto_hunting_expires_at) {
        auto_hunting_enabled = false;
    }
    if (!auto_hunting_enabled) {
        if (!led_is_off) {
            write_green(0);
        }
        return;
    }
    if (now_us < next_led_frame_at) {
        return;
    }
    next_led_frame_at = now_us + LED_FRAME_US;

    uint64_t elapsed = now_us - auto_hunting_started_at;
    uint64_t phase = elapsed % (BREATH_HALF_CYCLE_US * 2);
    uint32_t perceptual;
    if (phase < BREATH_HALF_CYCLE_US) {
        perceptual = 1000 - (uint32_t) (phase * 1000 / BREATH_HALF_CYCLE_US);
    } else {
        perceptual = (uint32_t) ((phase - BREATH_HALF_CYCLE_US) * 1000 / BREATH_HALF_CYCLE_US);
    }
    write_green(smooth_gamma_level(perceptual));
#else
    (void) now_us;
#endif
}

void jarvis_set_auto_hunting(bool enabled, uint16_t ttl_ms) {
    uint64_t now_us = time_us_64();
    if (enabled && !auto_hunting_enabled) {
        auto_hunting_started_at = now_us;
        next_led_frame_at = 0;
    }
    auto_hunting_enabled = enabled;
    uint16_t effective_ttl = ttl_ms == 0 ? DEFAULT_STATUS_TTL_MS : std::min<uint16_t>(ttl_ms, 5000);
    auto_hunting_expires_at = now_us + (uint64_t) effective_ttl * 1000;
    if (!enabled) {
#if defined(FEATHER_HOST_BOARD) && defined(PICO_DEFAULT_WS2812_PIN)
        write_green(0);
#endif
    }
}

void jarvis_reset_shortcuts() {
    pause_latched = false;
    for (bool& latched : arrow_latched) {
        latched = false;
    }
}

void jarvis_update_shortcuts(bool tab, bool pause, bool up, bool down, bool left, bool right) {
    if (pause && !pause_latched) {
        emit_shortcut(JarvisShortcut::AUTO_HUNTING);
    }
    pause_latched = pause;

    bool arrows[4] = { up, down, left, right };
    JarvisShortcut events[4] = {
        JarvisShortcut::AUTO_HUNTING,
        JarvisShortcut::RUNE_CAPTURE,
        JarvisShortcut::RESERVED_LEFT,
        JarvisShortcut::RESERVED_RIGHT,
    };
    for (int i = 0; i < 4; i++) {
        bool chord_down = tab && arrows[i];
        if (chord_down && !arrow_latched[i]) {
            emit_shortcut(events[i]);
        }
        arrow_latched[i] = chord_down;
    }
}
