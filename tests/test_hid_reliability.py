#!/usr/bin/env python3
"""Native acceptance-level regression tests for the nightly_v2 HID changes.

Run with ``python3 tests/test_hid_reliability.py`` (including under WSL).
Only Python's standard library and a native C++17 g++ compiler are required.

The runner extracts the current production functions and enqueue loop by
balanced braces, compiles those exact bodies with mock TinyUSB/state fixtures,
and runs fault-injection cases. It does not copy the algorithms into Python.
Success means TinyUSB *submission acceptance* and software-state invariants
passed; it is not a USB completion ACK, full firmware simulation, PIO/board
test, or a guarantee that a physical PC received every key transition.
Generated C++ and the executable live only in a temporary directory.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _mask_cpp(text: str) -> str:
    """Blank comments/literals, preserving offsets and newlines for braces."""
    chars = list(text)
    pos = 0

    def blank(start: int, end: int) -> None:
        for index in range(start, end):
            if chars[index] != "\n":
                chars[index] = " "

    while pos < len(text):
        if text.startswith("//", pos):
            end = text.find("\n", pos + 2)
            end = len(text) if end < 0 else end
        elif text.startswith("/*", pos):
            end = text.find("*/", pos + 2)
            if end < 0:
                raise ValueError("Unterminated C++ block comment")
            end += 2
        elif text.startswith('R"', pos):
            opening = text.find("(", pos + 2)
            if opening < 0 or opening - pos > 18:
                raise ValueError("Unsupported or unterminated C++ raw string")
            closing = ")" + text[pos + 2 : opening] + '"'
            end = text.find(closing, opening + 1)
            if end < 0:
                raise ValueError("Unterminated C++ raw string")
            end += len(closing)
        elif text[pos] in "\"'":
            quote = text[pos]
            end = pos + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                elif text[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            else:
                raise ValueError("Unterminated C++ string or character literal")
        else:
            pos += 1
            continue
        blank(pos, min(end, len(text)))
        pos = end
    return "".join(chars)


class CppSource:
    def __init__(self, relative_path: str) -> None:
        self.path = ROOT / relative_path
        self.text = self.path.read_text(encoding="utf-8")
        self.masked = _mask_cpp(self.text)

    def span(
        self, marker: str, within: tuple[int, int] | None = None
    ) -> tuple[int, int]:
        """Find one definition/block, skipping prototypes with the same marker."""
        lower, upper = within or (0, len(self.text))
        found = []
        search_from = lower
        while True:
            start = self.masked.find(marker, search_from, upper)
            if start < 0:
                break
            search_from = start + len(marker)
            parens = 0
            opening = None
            for index in range(start, upper):
                char = self.masked[index]
                if char == "(":
                    parens += 1
                elif char == ")":
                    parens -= 1
                elif char == ";" and parens == 0:
                    break  # A forward declaration, not a function definition.
                elif char == "{" and parens == 0:
                    opening = index
                    break
            if opening is None:
                continue
            depth = 1
            for index in range(opening + 1, upper):
                depth += (self.masked[index] == "{") - (
                    self.masked[index] == "}"
                )
                if depth == 0:
                    found.append((start, index + 1))
                    break
            else:
                raise ValueError(f"Unbalanced braces: {self.path}: {marker}")
        if len(found) != 1:
            raise ValueError(
                f"Expected one block for {marker!r} in {self.path}, got {len(found)}"
            )
        return found[0]

    def fragment(
        self, marker: str, within: tuple[int, int] | None = None
    ) -> str:
        start, end = self.span(marker, within)
        return self._render(start, end)

    def declaration(self, marker: str) -> str:
        positions = list(re.finditer(re.escape(marker), self.masked))
        if len(positions) != 1:
            raise ValueError(f"Expected one declaration for {marker!r} in {self.path}")
        start = positions[0].start()
        end = self.masked.find(";", start)
        if end < 0 or "{" in self.masked[start:end]:
            raise ValueError(f"Invalid declaration for {marker!r} in {self.path}")
        return self._render(start, end + 1)

    def _render(self, start: int, end: int) -> str:
        line = self.text.count("\n", 0, start) + 1
        path = self.path.as_posix().replace('"', '\\"')
        return f'#line {line} "{path}"\n{self.text[start:end]}\n'

    def define(self, name: str) -> str:
        match = re.search(
            rf"^#define[ \t]+{re.escape(name)}[ \t]+([0-9]+)[ \t]*$",
            self.masked,
            re.MULTILINE,
        )
        if match is None:
            raise ValueError(f"Missing numeric #define {name} in {self.path}")
        return f"#define {name} {match.group(1)}\n"


HARNESS_PREFIX = r"""
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string(__func__) + ":" + \
        std::to_string(__LINE__) + ": " #condition); \
} } while (false)

struct descriptor_t {
    void (*clear_report)(uint8_t*, uint8_t, uint16_t) = nullptr;
    void (*sanitize_report)(uint8_t, uint8_t*, uint16_t) = nullptr;
    bool (*should_cause_wakeup)(uint8_t, const uint8_t*, uint16_t) = nullptr;
};
descriptor_t our_descriptors[2];
descriptor_t* our_descriptor = &our_descriptors[0];
uint8_t our_descriptor_number = 0;
bool suspended = false;
uint8_t report_storage[MAX_INPUT_REPORT_ID + 1][MAX_REPORT_SIZE];
uint8_t previous_storage[MAX_INPUT_REPORT_ID + 1][MAX_REPORT_SIZE];
uint8_t relative_masks[MAX_INPUT_REPORT_ID + 1][MAX_REPORT_SIZE];
uint8_t absolute_masks[MAX_INPUT_REPORT_ID + 1][MAX_REPORT_SIZE];
uint8_t* reports[MAX_INPUT_REPORT_ID + 1];
uint8_t* prev_reports[MAX_INPUT_REPORT_ID + 1];
uint8_t* report_masks_relative[MAX_INPUT_REPORT_ID + 1];
uint8_t* report_masks_absolute[MAX_INPUT_REPORT_ID + 1];
uint16_t report_sizes[MAX_INPUT_REPORT_ID + 1];
std::vector<uint8_t> report_ids;
uint8_t outgoing_reports[OR_BUFSIZE][MAX_REPORT_SIZE + 1];
uint8_t or_head = 0, or_tail = 0, or_items = 0;
uint32_t reports_sent = 0, output_queue_overflows = 0;
using send_report_t = bool (*)(uint8_t, const uint8_t*, uint8_t);

struct usage_def_t {
    bool is_relative;
    uint16_t bitpos;
    uint8_t size;
    int32_t logical_minimum;
};
std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>> our_usages;

struct send_attempt_t {
    uint8_t interface;
    uint8_t report_id;
    std::vector<uint8_t> payload;
    bool accepted;
};
std::vector<send_attempt_t> send_attempts;
std::deque<bool> send_results;
std::deque<bool> wake_results;
bool bus_suspended = false, wake_eligible = false;
unsigned wake_requests = 0, wake_successes = 0, wake_predicate_checks = 0;

bool tud_suspended() { return bus_suspended; }
bool tud_remote_wakeup() {
    ++wake_requests;
    bool accepted = true;
    if (!wake_results.empty()) {
        accepted = wake_results.front();
        wake_results.pop_front();
    }
    if (accepted) ++wake_successes;
    return accepted;
}
bool should_wake(uint8_t report_id, const uint8_t* report, uint16_t len) {
    ++wake_predicate_checks;
    return wake_eligible && report_id == 2 && len > 0 && report[0] != 0;
}
bool tud_hid_n_report(uint8_t interface, uint8_t report_id,
                      const void* report, uint16_t len) {
    bool accepted = true;
    if (!send_results.empty()) {
        accepted = send_results.front();
        send_results.pop_front();
    }
    const auto* bytes = static_cast<const uint8_t*>(report);
    send_attempts.push_back({interface, report_id, {bytes, bytes + len}, accepted});
    return accepted;
}

struct host_slot_t {
    uint8_t dev_addr = 0;
    bool mounted = false;
    bool ready = false;
    std::deque<bool> results;
};
host_slot_t host_slots[CFG_TUH_HID];
std::vector<std::pair<uint8_t, uint8_t>> receive_attempts;
std::vector<uint8_t> disconnects;
std::vector<uint16_t> handled_interfaces, connected_interfaces;
unsigned invalid_receive_attempts = 0, host_task_calls = 0;
bool pending_tick = false;
std::function<void()> host_task_hook;

bool tuh_hid_mounted(uint8_t dev_addr, uint8_t instance) {
    return instance < CFG_TUH_HID && host_slots[instance].mounted &&
        host_slots[instance].dev_addr == dev_addr;
}
bool tuh_hid_receive_ready(uint8_t dev_addr, uint8_t instance) {
    return tuh_hid_mounted(dev_addr, instance) && host_slots[instance].ready;
}
bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t instance) {
    receive_attempts.emplace_back(dev_addr, instance);
    if (!tuh_hid_receive_ready(dev_addr, instance)) {
        ++invalid_receive_attempts;
        return false;
    }
    auto& slot = host_slots[instance];
    bool accepted = true;
    if (!slot.results.empty()) {
        accepted = slot.results.front();
        slot.results.pop_front();
    }
    if (accepted) slot.ready = false;  // One outstanding transfer per endpoint.
    return accepted;
}
bool get_and_clear_tick_pending() {
    bool tick = pending_tick;
    pending_tick = false;
    return tick;
}
void tuh_task() {
    ++host_task_calls;
    auto hook = std::move(host_task_hook);
    host_task_hook = {};
    if (hook) hook();
}
struct tuh_itf_info_t { struct { uint8_t bInterfaceNumber; } desc; };
bool tuh_get_hub_addr_port(uint8_t, uint8_t* hub, uint8_t* port) {
    *hub = 0; *port = 0; return true;
}
bool tuh_vid_pid_get(uint8_t, uint16_t* vid, uint16_t* pid) {
    *vid = 0x1234; *pid = 0x5678; return true;
}
bool tuh_hid_itf_get_info(uint8_t, uint8_t instance, tuh_itf_info_t* info) {
    info->desc.bInterfaceNumber = instance; return true;
}
void parse_descriptor(uint16_t, uint16_t, const uint8_t*, int, uint16_t, uint8_t) {}
void device_connected_callback(uint16_t interface, uint16_t, uint16_t, uint8_t) {
    connected_interfaces.push_back(interface);
}
void device_disconnected_callback(uint8_t dev_addr) { disconnects.push_back(dev_addr); }
void handle_received_report(const uint8_t*, int, uint16_t interface) {
    handled_interfaces.push_back(interface);
}
"""


HARNESS_TESTS = r"""
void reset_output() {
    std::memset(report_storage, 0, sizeof(report_storage));
    std::memset(previous_storage, 0, sizeof(previous_storage));
    std::memset(relative_masks, 0, sizeof(relative_masks));
    std::memset(absolute_masks, 0, sizeof(absolute_masks));
    std::memset(outgoing_reports, 0, sizeof(outgoing_reports));
    for (unsigned id = 0; id <= MAX_INPUT_REPORT_ID; ++id) {
        reports[id] = report_storage[id];
        prev_reports[id] = previous_storage[id];
        report_masks_relative[id] = relative_masks[id];
        report_masks_absolute[id] = absolute_masks[id];
        report_sizes[id] = 2;
        absolute_masks[id][0] = absolute_masks[id][1] = 0xff;
    }
    for (auto& descriptor : our_descriptors) descriptor = {};
    our_descriptor = &our_descriptors[0];
    our_descriptor_number = 0;
    suspended = bus_suspended = wake_eligible = false;
    or_head = or_tail = or_items = 0;
    reports_sent = output_queue_overflows = wake_requests = 0;
    wake_successes = wake_predicate_checks = 0;
    report_ids = {2};
    our_usages.clear();
    send_attempts.clear();
    send_results.clear();
    wake_results.clear();
    report_wakeup_pending = false;
    maybe_wake_host();  // Observe awake state to reset the function-local latch.
}
void stage(uint8_t id, uint8_t absolute, uint8_t relative = 0) {
    reports[id][0] = absolute;
    reports[id][1] = relative;
}
void enqueue(uint8_t value) { stage(2, value); enqueue_staged_reports(); }
std::vector<uint8_t> ring_snapshot() {
    auto* first = reinterpret_cast<const uint8_t*>(outgoing_reports);
    return {first, first + sizeof(outgoing_reports)};
}
void test_submit_retry() {
    reset_output();
    enqueue(1);
    const auto before = ring_snapshot();
    const auto head = or_head, tail = or_tail;
    send_results = {false, false, false, true};
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        CHECK(!send_report(do_send_report));
        CHECK(or_head == head && or_tail == tail && or_items == 1);
        CHECK(ring_snapshot() == before && reports_sent == 0);
        CHECK(prev_reports[2][0] == 1);
    }
    CHECK(send_report(do_send_report));
    CHECK(or_items == 0 && or_head == (head + 1) % OR_BUFSIZE);
    CHECK(reports_sent == 1 && send_attempts.size() == 4);
    for (const auto& attempt : send_attempts) {
        CHECK(attempt.interface == 0 && attempt.report_id == 2);
        CHECK(attempt.payload == std::vector<uint8_t>({1, 0}));
    }
    const auto empty_head = or_head;
    CHECK(!send_report(do_send_report));
    CHECK(or_head == empty_head && send_attempts.size() == 4);
}
void test_down_up_order() {
    reset_output();
    enqueue(1); enqueue(0);
    CHECK(or_items == 2);
    send_results = {false, false, true, false, true};
    for (unsigned i = 0; i < 5; ++i) send_report(do_send_report);
    CHECK(or_items == 0 && reports_sent == 2);
    std::vector<uint8_t> attempted, accepted;
    for (const auto& attempt : send_attempts) {
        attempted.push_back(attempt.payload[0]);
        if (attempt.accepted) accepted.push_back(attempt.payload[0]);
    }
    CHECK(attempted == std::vector<uint8_t>({1, 1, 1, 0, 0}));
    CHECK(accepted == std::vector<uint8_t>({1, 0}));
}
void test_suspend_wakeup() {
    reset_output();
    wake_eligible = true;
    our_descriptor->should_cause_wakeup = should_wake;
    enqueue(1);
    const auto before = ring_snapshot();
    bus_suspended = true;
    CHECK(!send_report(do_send_report));
    CHECK(wake_requests == 0 && send_attempts.empty());  // Submission is pure.
    CHECK(or_items == 1 && ring_snapshot() == before && reports_sent == 0);
    for (unsigned loop = 0; loop < 10; ++loop) maybe_wake_host();
    CHECK(wake_requests == 1 && wake_successes == 1);
    CHECK(!report_wakeup_pending && ring_snapshot() == before && or_items == 1);
    const auto checks = wake_predicate_checks;
    maybe_wake_host();
    CHECK(wake_predicate_checks == checks);  // Latched loops need no queue scan.
    bus_suspended = false;
    maybe_wake_host();
    send_results = {false, true};
    CHECK(!send_report(do_send_report));
    CHECK(or_items == 1);
    CHECK(send_report(do_send_report));
    CHECK(or_items == 0 && reports_sent == 1);
    enqueue(0);
    CHECK(send_report(do_send_report));
    enqueue(1);
    bus_suspended = true;
    for (unsigned loop = 0; loop < 10; ++loop) maybe_wake_host();
    CHECK(wake_requests == 2 && wake_successes == 2);  // New suspend re-arms.
    CHECK(or_items == 1 && reports_sent == 2);
    suspended = true;  // Config suspend also leaves the queued report untouched.
    CHECK(!send_report(do_send_report));
    CHECK(or_items == 1 && reports_sent == 2);
}
void test_descriptor_mismatch() {
    reset_output();
    enqueue(1); enqueue(0);
    our_descriptor_number = 1;
    CHECK(!send_report(do_send_report));
    CHECK(or_items == 1 && or_head == 1);
    CHECK(!send_report(do_send_report));
    CHECK(or_items == 0 && or_head == 2);
    CHECK(send_attempts.empty() && reports_sent == 0);
    our_descriptor_number = 0;
    enqueue(2);
    CHECK(send_report(do_send_report));
    CHECK(or_items == 0 && reports_sent == 1);
}
void fill_ring(uint8_t id) {
    report_ids = {id};
    for (unsigned i = 0; i < OR_BUFSIZE; ++i) {
        stage(id, (i % 2 == 0) ? 1 : 0);
        enqueue_staged_reports();
    }
    CHECK(or_items == OR_BUFSIZE && or_head == or_tail);
}
void test_wakeup_nonwake_head() {
    reset_output();
    wake_eligible = true;
    our_descriptor->should_cause_wakeup = should_wake;
    or_head = or_tail = OR_BUFSIZE - 1;  // Exercise scan across the ring wrap.
    report_ids = {1};
    stage(1, 1); enqueue_staged_reports();  // Non-wake mouse report at the head.
    report_ids = {2};
    enqueue(1);                            // Wake keyboard report at the tail.
    CHECK(or_items == 2 && report_wakeup_pending);
    report_wakeup_pending = false;         // Test the queue scan, not the hint.
    const auto before = ring_snapshot();
    const auto head = or_head, tail = or_tail;
    bus_suspended = true;
    maybe_wake_host();
    CHECK(wake_requests == 1 && wake_successes == 1);
    CHECK(or_head == head && or_tail == tail && or_items == 2);
    CHECK(ring_snapshot() == before && send_attempts.empty());
    CHECK(outgoing_reports[or_head][0] == 1);
}
void test_wakeup_full_queue_staging() {
    reset_output();
    wake_eligible = true;
    our_descriptor->should_cause_wakeup = should_wake;
    fill_ring(1);  // All 16 reports are non-wake candidates.
    CHECK(!report_wakeup_pending);
    const auto before = ring_snapshot();
    report_ids = {2};
    our_descriptor->sanitize_report = [](uint8_t id, uint8_t* report, uint16_t len) {
        if (id == 2 && len > 0) report[0] = 1;
    };
    stage(2, 0);  // Sanitization must happen before wake-hint evaluation.
    enqueue_staged_reports();
    CHECK(report_wakeup_pending && output_queue_overflows == 1);
    CHECK(or_items == OR_BUFSIZE && ring_snapshot() == before);
    CHECK(prev_reports[2][0] == 0 && reports[2][0] == 0);
    bus_suspended = true;
    maybe_wake_host();
    CHECK(wake_requests == 1 && wake_successes == 1 && !report_wakeup_pending);
    for (unsigned tick = 0; tick < 5; ++tick) {
        stage(2, 1); enqueue_staged_reports();
        CHECK(report_wakeup_pending);
        const auto checks = wake_predicate_checks;
        maybe_wake_host();
        CHECK(!report_wakeup_pending && wake_predicate_checks == checks);
    }
    CHECK(wake_requests == 1 && wake_successes == 1);
    CHECK(or_items == OR_BUFSIZE && ring_snapshot() == before);
    CHECK(send_attempts.empty() && output_queue_overflows == 6);
}
void test_wakeup_denied_retry() {
    reset_output();
    wake_eligible = true;
    our_descriptor->should_cause_wakeup = should_wake;
    enqueue(1);
    const auto before = ring_snapshot();
    bus_suspended = true;
    wake_results = {false, false, true};
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        maybe_wake_host();
        CHECK(wake_requests == attempt);
        CHECK(wake_successes == ((attempt == 3) ? 1u : 0u));
        CHECK(or_items == 1 && ring_snapshot() == before && !report_wakeup_pending);
    }
    for (unsigned loop = 0; loop < 10; ++loop) maybe_wake_host();
    CHECK(wake_requests == 3 && wake_successes == 1 && send_attempts.empty());
}
void test_wakeup_guards() {
    reset_output();
    enqueue(1);
    bus_suspended = true;
    report_wakeup_pending = true;  // An obsolete hint cannot bypass a null callback.
    maybe_wake_host();
    CHECK(wake_requests == 0 && !report_wakeup_pending);
    our_descriptor->should_cause_wakeup = should_wake;
    wake_eligible = false;
    maybe_wake_host();
    CHECK(wake_requests == 0);
    wake_eligible = true;
    suspended = true;
    report_wakeup_pending = true;
    maybe_wake_host();
    CHECK(wake_requests == 0 && !report_wakeup_pending);
    suspended = false;
    our_descriptor_number = 1;
    report_wakeup_pending = true;
    maybe_wake_host();
    CHECK(wake_requests == 0 && !report_wakeup_pending);
    our_descriptor_number = 0;
    maybe_wake_host();
    CHECK(wake_requests == 1 && wake_successes == 1 && or_items == 1);
}
void test_wakeup_awake_pending() {
    reset_output();
    wake_eligible = true;
    our_descriptor->should_cause_wakeup = should_wake;
    enqueue(1);
    CHECK(report_wakeup_pending);
    const auto checks = wake_predicate_checks;
    maybe_wake_host();  // Awake loops consume the hint without scanning the queue.
    CHECK(!report_wakeup_pending && wake_requests == 0);
    CHECK(wake_predicate_checks == checks);
    CHECK(send_report(do_send_report));
    enqueue(1);  // An unchanged held key is not a new report or wake hint.
    CHECK(or_items == 0 && !report_wakeup_pending);
    bus_suspended = true;
    maybe_wake_host();
    CHECK(wake_requests == 0 && wake_successes == 0);
    bus_suspended = false;
    maybe_wake_host();
    enqueue(0);
    maybe_wake_host();
    CHECK(send_report(do_send_report));
    bus_suspended = true;
    maybe_wake_host();
    CHECK(wake_requests == 0 && !report_wakeup_pending && or_items == 0);
}
void test_full_queue() {
    reset_output();
    CHECK(OR_BUFSIZE == 16);
    fill_ring(2);
    const auto before = ring_snapshot();
    const auto head = or_head, tail = or_tail;
    report_ids = {1, 2, 3};
    stage(1, 0x10); stage(2, 0x04); stage(3, 0x80);
    enqueue_staged_reports();
    CHECK(or_items == 16 && or_head == head && or_tail == tail);
    CHECK(ring_snapshot() == before && output_queue_overflows == 3);
    for (uint8_t id : report_ids) {
        CHECK(reports[id][0] == 0 && reports[id][1] == 0);
        CHECK(prev_reports[id][0] == 0 && prev_reports[id][1] == 0);
    }
    CHECK(send_report(do_send_report));
    report_ids = {2};
    stage(2, 0x04);  // A persistent state is recomputed next tick.
    enqueue_staged_reports();
    CHECK(or_items == 16 && prev_reports[2][0] == 0x04);
    CHECK(reports[2][0] == 0 && output_queue_overflows == 3);
    while (or_items) CHECK(send_report(do_send_report));
    CHECK(send_attempts.back().payload[0] == 0x04);
    enqueue(0);
    CHECK(send_report(do_send_report));
    CHECK(send_attempts.back().payload[0] == 0 && or_items == 0);
}
void test_full_queue_coalesce() {
    reset_output();
    absolute_masks[1][1] = 0;
    relative_masks[1][1] = 0xff;
    our_usages[1][0x00010030] = {true, 8, 8, 0};
    fill_ring(1);
    const auto tail = or_tail;
    stage(1, 0, 5);
    enqueue_staged_reports();
    CHECK(or_items == OR_BUFSIZE && or_tail == tail);
    CHECK(outgoing_reports[(tail + OR_BUFSIZE - 1) % OR_BUFSIZE][2] == 5);
    CHECK(output_queue_overflows == 0);
    CHECK(reports[1][0] == 0 && reports[1][1] == 0);
    CHECK(prev_reports[1][0] == 0 && prev_reports[1][1] == 0);
}
void test_queued_baseline() {
    reset_output();
    report_ids = {1, 2, 3};
    for (uint8_t id : report_ids) stage(id, id);
    enqueue_staged_reports();
    CHECK(or_items == 3);
    for (unsigned tick = 0; tick < 100; ++tick) {
        for (uint8_t id : report_ids) stage(id, id);
        enqueue_staged_reports();
    }
    CHECK(or_items == 3 && output_queue_overflows == 0);
    CHECK(reports_sent == 0);
}
void test_ring_wrap_stress() {
    reset_output();
    constexpr unsigned pairs = 10000;
    for (unsigned pair = 0; pair < pairs; ++pair) {
        enqueue(1); enqueue(0);
        for (unsigned edge = 0; edge < 2; ++edge) {
            const unsigned failures = 1 + (pair + edge) % 7;
            for (unsigned i = 0; i < failures; ++i) send_results.push_back(false);
            send_results.push_back(true);
            while (!send_report(do_send_report)) {}
        }
        CHECK(or_items == 0);
    }
    unsigned accepted = 0;
    for (const auto& attempt : send_attempts) {
        if (attempt.accepted) {
            CHECK(attempt.payload[0] == ((accepted % 2 == 0) ? 1 : 0));
            ++accepted;
        }
    }
    CHECK(accepted == pairs * 2 && reports_sent == pairs * 2);
    CHECK(output_queue_overflows == 0);
}

void reset_host() {
    for (auto& slot : host_slots) slot = {};
    std::memset(hid_receive_states, 0, sizeof(hid_receive_states));
    receive_attempts.clear(); disconnects.clear(); handled_interfaces.clear();
    connected_interfaces.clear();
    invalid_receive_attempts = host_task_calls = 0;
    pending_tick = reports_received = false;
    host_task_hook = {};
}
void mount(uint8_t dev, uint8_t instance, std::deque<bool> results = {}) {
    auto& slot = host_slots[instance];
    slot.dev_addr = dev; slot.mounted = slot.ready = true;
    slot.results = std::move(results);
    const uint8_t descriptor[] = {0x05, 0x01};
    tuh_hid_mount_cb(dev, instance, descriptor, sizeof(descriptor));
}
bool read_tick(bool tick) {
    pending_tick = tick;
    bool new_report = false, actual_tick = false;
    read_report(&new_report, &actual_tick);
    CHECK(actual_tick == tick && !pending_tick);
    return new_report;
}
void test_receive_tick_retry() {
    reset_host();
    mount(4, 2, {false, true});
    CHECK(receive_attempts.size() == 1 && hid_receive_states[2].rearm_pending);
    CHECK(!read_tick(false));
    CHECK(receive_attempts.size() == 1);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 2 && !hid_receive_states[2].rearm_pending);
    CHECK(!host_slots[2].ready);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 2 && invalid_receive_attempts == 0);
}
void test_receive_callback_retry() {
    reset_host();
    mount(4, 2);
    const uint8_t report[] = {1, 0};
    host_slots[2].ready = true;  // TinyUSB clears busy before its callback.
    host_slots[2].results = {false, true};
    host_task_hook = [&] { tuh_hid_report_received_cb(4, 2, report, sizeof(report)); };
    CHECK(read_tick(false));
    CHECK(handled_interfaces == std::vector<uint16_t>({0x0402}));
    CHECK(receive_attempts.size() == 2 && hid_receive_states[2].rearm_pending);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 3 && !hid_receive_states[2].rearm_pending);
    host_slots[2].ready = true;
    host_slots[2].results = {false, true};
    host_task_hook = [&] { tuh_hid_report_received_cb(4, 2, report, 0); };
    CHECK(!read_tick(false));  // A zero-length completion still needs rearming.
    CHECK(hid_receive_states[2].rearm_pending);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 5 && !hid_receive_states[2].rearm_pending);
    CHECK(invalid_receive_attempts == 0);
}
void test_receive_not_ready() {
    reset_host();
    mount(4, 2, {false, true});
    host_slots[2].ready = false;
    for (unsigned tick = 0; tick < 5; ++tick) CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 1 && hid_receive_states[2].rearm_pending);
    host_slots[2].ready = true;
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 2 && !hid_receive_states[2].rearm_pending);
    CHECK(invalid_receive_attempts == 0);
}
void test_receive_unmount_reuse() {
    reset_host();
    mount(4, 2, {false});
    tuh_hid_umount_cb(4, 2);
    CHECK(!hid_receive_states[2].mounted && !hid_receive_states[2].rearm_pending);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 1 && disconnects == std::vector<uint8_t>({4}));
    mount(7, 2, {false, true});
    CHECK(receive_attempts.size() == 2 && hid_receive_states[2].dev_addr == 7);
    request_hid_receive(4, 2);  // An old callback must not target the new device.
    umount_callback(4, 2);     // Nor may an old unmount clear the reused slot.
    CHECK(hid_receive_states[2].mounted && hid_receive_states[2].rearm_pending);
    CHECK(hid_receive_states[2].dev_addr == 7 && receive_attempts.size() == 2);
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 3);
    CHECK(receive_attempts.back() == std::make_pair(uint8_t(7), uint8_t(2)));
    CHECK(!hid_receive_states[2].rearm_pending && invalid_receive_attempts == 0);
}
void test_receive_stale_slot() {
    reset_host();
    mount(4, 2, {false});
    host_slots[2].mounted = false;
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 1);
    CHECK(hid_receive_states[2].dev_addr == 0 && !hid_receive_states[2].mounted);
    CHECK(!hid_receive_states[2].rearm_pending);
    request_hid_receive(4, CFG_TUH_HID);
    try_rearm_hid_receive(CFG_TUH_HID);
    CHECK(receive_attempts.size() == 1 && invalid_receive_attempts == 0);
}
void test_receive_multiple_slots() {
    reset_host();
    mount(4, 2, {false, true});
    mount(7, 5, {false, true});
    host_slots[2].ready = false;
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 3);
    CHECK(hid_receive_states[2].rearm_pending && !hid_receive_states[5].rearm_pending);
    CHECK(receive_attempts.back() == std::make_pair(uint8_t(7), uint8_t(5)));
    host_slots[2].ready = true;
    CHECK(!read_tick(true));
    CHECK(receive_attempts.size() == 4 && !hid_receive_states[2].rearm_pending);
    CHECK(receive_attempts.back() == std::make_pair(uint8_t(4), uint8_t(2)));
    CHECK(invalid_receive_attempts == 0);
}

int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> cases[] = {
        {"submit_retry", test_submit_retry},
        {"down_up_order", test_down_up_order},
        {"suspend_wakeup", test_suspend_wakeup},
        {"wakeup_nonwake_head", test_wakeup_nonwake_head},
        {"wakeup_full_queue_staging", test_wakeup_full_queue_staging},
        {"wakeup_denied_retry", test_wakeup_denied_retry},
        {"wakeup_guards", test_wakeup_guards},
        {"wakeup_awake_pending", test_wakeup_awake_pending},
        {"descriptor_mismatch", test_descriptor_mismatch},
        {"full_queue", test_full_queue},
        {"full_queue_coalesce", test_full_queue_coalesce},
        {"queued_baseline", test_queued_baseline},
        {"ring_wrap_stress", test_ring_wrap_stress},
        {"receive_tick_retry", test_receive_tick_retry},
        {"receive_callback_retry", test_receive_callback_retry},
        {"receive_not_ready", test_receive_not_ready},
        {"receive_unmount_reuse", test_receive_unmount_reuse},
        {"receive_stale_slot", test_receive_stale_slot},
        {"receive_multiple_slots", test_receive_multiple_slots},
    };
    if (argc != 2) { std::cerr << "Expected one case name\n"; return 2; }
    try {
        for (const auto& item : cases) {
            if (std::string(argv[1]) == item.first) {
                item.second();
                std::cout << "PASS " << item.first << '\n';
                return 0;
            }
        }
        std::cerr << "Unknown case: " << argv[1] << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
}
"""


def build_harness() -> str:
    main = CppSource("firmware/src/main.cc")
    remapper = CppSource("firmware/src/remapper.cc")
    single = CppSource("firmware/src/remapper_single.cc")
    descriptor = CppSource("firmware/src/our_descriptor.h")
    config = CppSource("firmware/src/tusb_config_both/tusb_config.h")
    parts = [
        remapper.define("MAX_REPORT_SIZE"),
        remapper.define("OR_BUFSIZE"),
        descriptor.define("MAX_INPUT_REPORT_ID"),
        config.define("CFG_TUH_HID"),
        HARNESS_PREFIX,
        remapper.declaration("static bool report_wakeup_pending"),
    ]
    for marker in (
        "inline int8_t get_bit",
        "inline uint32_t get_bits",
        "inline void put_bit",
        "inline void put_bits",
        "bool needs_to_be_sent",
        "bool differ_on_absolute",
        "void aggregate_relative",
    ):
        parts.append(remapper.fragment(marker + "("))
    parts.append(main.fragment("bool do_send_report("))
    parts.append(remapper.fragment("bool take_report_wakeup_request("))
    parts.append(main.fragment("void maybe_wake_host("))
    parts.append(remapper.fragment("bool send_report("))
    mapping_span = remapper.span("void process_mapping(")
    parts.append("void enqueue_staged_reports() {\n")
    parts.append(
        remapper.fragment(
            "for (unsigned int i = 0; i < report_ids.size(); i++)", mapping_span
        )
    )
    parts.append("}\nstatic bool reports_received;\n")
    parts.append(single.fragment("struct hid_receive_state_t"))
    parts.append(";\nstatic hid_receive_state_t hid_receive_states[CFG_TUH_HID];\n")
    for marker in (
        "static void try_rearm_hid_receive(",
        "static void request_hid_receive(",
        "static void retry_pending_hid_receives(",
        "void read_report(",
        "void descriptor_received_callback(",
        "void tuh_hid_mount_cb(",
        "void umount_callback(",
        "void tuh_hid_umount_cb(",
        "void report_received_callback(",
        "void tuh_hid_report_received_cb(",
    ):
        parts.append(single.fragment(marker))
    parts.append('#line 1 "native_hid_reliability_tests.cpp"\n')
    parts.append(HARNESS_TESTS)
    return "\n".join(parts)


class HidReliabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        compiler = shlex.split(os.environ.get("CXX", "g++"))
        if not compiler or shutil.which(compiler[0]) is None:
            raise RuntimeError("Native g++ is required; install it or set CXX")
        cls.temp = tempfile.TemporaryDirectory(prefix="hid-reliability-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        cpp = directory / "hid_reliability.cpp"
        cls.binary = directory / "hid_reliability"
        cpp.write_text(build_harness(), encoding="utf-8")
        command = compiler + [
            "-std=c++17",
            "-O0",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(cpp),
            "-o",
            str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(
                "Native harness compilation failed:\n" + result.stdout + result.stderr
            )

    def run_case(self, name: str) -> None:
        result = subprocess.run(
            [str(self.binary), name], capture_output=True, text=True, timeout=15
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_submit_retry(self) -> None:
        self.run_case("submit_retry")

    def test_down_up_order(self) -> None:
        self.run_case("down_up_order")

    def test_suspend_wakeup(self) -> None:
        self.run_case("suspend_wakeup")

    def test_wakeup_scans_past_nonwake_head(self) -> None:
        self.run_case("wakeup_nonwake_head")

    def test_wakeup_full_queue_uses_sanitized_staging_hint(self) -> None:
        self.run_case("wakeup_full_queue_staging")

    def test_wakeup_denied_request_is_retried_until_accepted(self) -> None:
        self.run_case("wakeup_denied_retry")

    def test_wakeup_null_false_config_and_descriptor_guards(self) -> None:
        self.run_case("wakeup_guards")

    def test_wakeup_awake_hint_consumption_and_unchanged_hold(self) -> None:
        self.run_case("wakeup_awake_pending")

    def test_descriptor_mismatch(self) -> None:
        self.run_case("descriptor_mismatch")

    def test_full_queue_staging_and_baseline(self) -> None:
        self.run_case("full_queue")

    def test_full_queue_relative_coalescing(self) -> None:
        self.run_case("full_queue_coalesce")

    def test_queued_baseline_prevents_duplicates(self) -> None:
        self.run_case("queued_baseline")

    def test_10000_down_up_pairs_with_rejections(self) -> None:
        self.run_case("ring_wrap_stress")

    def test_receive_failure_retries_only_on_tick(self) -> None:
        self.run_case("receive_tick_retry")

    def test_receive_callback_and_zero_length_rearm(self) -> None:
        self.run_case("receive_callback_retry")

    def test_receive_not_ready_does_not_submit(self) -> None:
        self.run_case("receive_not_ready")

    def test_receive_unmount_and_instance_reuse(self) -> None:
        self.run_case("receive_unmount_reuse")

    def test_receive_stale_and_invalid_slots(self) -> None:
        self.run_case("receive_stale_slot")

    def test_receive_multiple_interfaces_are_independent(self) -> None:
        self.run_case("receive_multiple_slots")


if __name__ == "__main__":
    unittest.main(verbosity=2)
