#pragma once

#include <stdint.h>

// One endpoint has one in-flight transfer. A successful submission is not a
// delivery acknowledgement; the FIFO owner retains its head until complete().
class HidDelivery {
    enum class State { idle, pending, complete };
    State state = State::idle;

public:
    template <typename Submit>
    bool send(Submit submit) {
        if (state == State::complete) {
            state = State::idle;
            return true;
        }
        if (state == State::idle) {
            state = State::pending;
            if (!submit()) state = State::idle;
        }
        return false;
    }
    void complete() { if (state == State::pending) state = State::complete; }
    void failed() { if (state == State::pending) state = State::idle; }
    void reset() { state = State::idle; }
};
