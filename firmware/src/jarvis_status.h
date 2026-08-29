#ifndef JARVIS_STATUS_H
#define JARVIS_STATUS_H

#include <cstdint>

enum class JarvisShortcut : uint32_t {
    AUTO_HUNTING = 0xFFF90001,
    RUNE_CAPTURE = 0xFFF90002,
    RESERVED_LEFT = 0xFFF90003,
    RESERVED_RIGHT = 0xFFF90004,
};

void jarvis_status_init();
void jarvis_status_task(uint64_t now_us);
void jarvis_set_auto_hunting(bool enabled, uint16_t ttl_ms);
void jarvis_reset_shortcuts();
void jarvis_update_shortcuts(bool tab, bool pause, bool up, bool down, bool left, bool right);

#endif
