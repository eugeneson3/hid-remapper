#pragma once

#include <stdint.h>

template <unsigned Count>
class HidReceiveRetry {
    struct Slot { uint8_t address = 0; uint8_t instance = 0; bool pending = false; };
    Slot slots[Count];

public:
    void mount(uint8_t address, uint8_t instance) {
        for (auto& slot : slots) {
            if (slot.address == address && slot.instance == instance) {
                slot.pending = true;
                return;
            }
        }
        for (auto& slot : slots) {
            if (!slot.address) { slot = {address, instance, true}; return; }
        }
    }
    void received(uint8_t address, uint8_t instance) {
        for (auto& slot : slots)
            if (slot.address == address && slot.instance == instance) slot.pending = true;
    }
    void unmount(uint8_t address) {
        for (auto& slot : slots) if (slot.address == address) slot = {};
    }
    template <typename Receive>
    void retry(Receive receive) {
        for (auto& slot : slots)
            if (slot.address && slot.pending && receive(slot.address, slot.instance)) slot.pending = false;
    }
};
