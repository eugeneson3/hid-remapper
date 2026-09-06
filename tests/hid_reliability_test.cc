#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
#include <set>
#include <vector>
#include "globals.h"
#include "descriptor_parser.h"
#include "remapper.h"
#include "hid_delivery.h"
#include "hid_receive_retry.h"

// Real descriptor parser + mapping engine; only USB and board functions mocked.
const uint8_t boot_kb_report_descriptor[] = {
    0x05,1,0x09,6,0xA1,1,0x05,7,0x19,0xE0,0x29,0xE7,
    0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x75,8,0x95,1,0x81,3,0x19,0,0x29,0x73,
    0x15,0,0x25,0x73,0x75,8,0x95,6,0x81,0,0xC0
};
const uint32_t boot_kb_report_descriptor_length = sizeof(boot_kb_report_descriptor);
const uint8_t nkro[] = {
    0x05,1,0x09,6,0xA1,1,0x85,2,0x05,7,
    0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x19,4,0x29,0x73,0x95,0x70,0x81,2,0xC0
};
static bool wakes(uint8_t, const uint8_t* data, uint16_t len) {
    for (unsigned i=0; i<len; ++i) if (data[i]) return true;
    return false;
}
const our_descriptor_def_t our_descriptors[NOUR_DESCRIPTORS] = {{
    .idx=0, .descriptor=nkro, .descriptor_length=sizeof(nkro),
    .handle_received_report=do_handle_received_report, .should_cause_wakeup=wakes
}};
static uint64_t now = 1;
static unsigned activity = 0;
uint64_t get_time() { return now; }
void my_mutex_enter(MutexId) {}
void my_mutex_exit(MutexId) {}
void set_gpio_inout_masks(uint32_t, uint32_t) {}
void queue_out_report(uint16_t, uint8_t, const uint8_t*, uint8_t) {}
void activity_led_on() { ++activity; }
void apply_quirks(uint16_t, uint16_t, std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>>&, const uint8_t*, int, uint8_t) {}

static std::vector<std::set<unsigned>> received;
static std::vector<monitor_report_item_t> events;
static bool output_boot = false;
static std::set<unsigned> decode(const uint8_t* report, uint8_t len) {
    std::set<unsigned> keys;
    if (output_boot) {
        assert(report[0]==0 && len==9);
        for (unsigned i=0; i<8; ++i) if (report[1] & (1u<<i)) keys.insert(0xE0+i);
        for (unsigned i=3; i<len; ++i) if (report[i]) keys.insert(report[i]);
    } else {
        assert(report[0]==2 && len==16);
        for (unsigned i=0; i<8; ++i) if (report[1] & (1u<<i)) keys.insert(0xE0+i);
        for (unsigned i=0; i<112; ++i) if (report[2+i/8] & (1u<<(i%8))) keys.insert(4+i);
    }
    return keys;
}
static bool accept(uint8_t itf, const uint8_t* report, uint8_t len) {
    if (!itf) received.push_back(decode(report,len));
    else {
        auto m = (const monitor_report_t*)report;
        for (auto item : m->items) if (item.usage) events.push_back(item);
    }
    return true;
}
static bool reject(uint8_t, const uint8_t*, uint8_t) { return false; }
static HidDelivery usb_tx;
static std::vector<uint8_t> submitted;
static bool async_send(uint8_t, const uint8_t* data, uint8_t length) {
    return usb_tx.send([&] { submitted.assign(data, data+length); return true; });
}
static void drain() {
    for (unsigned i=0; i<1024 && send_report(accept); ++i) {}
    while(send_monitor_report(accept)) {}
}
static void attach(uint8_t address, bool use_nkro=false, uint8_t instance=0, uint8_t port=1) {
    uint16_t itf = (address<<8)|instance;
    parse_descriptor(0x1234,0x5678, use_nkro ? nkro : boot_kb_report_descriptor,
                     use_nkro ? sizeof(nkro) : sizeof(boot_kb_report_descriptor),itf,instance);
    device_connected_callback(itf,0x1234,0x5678,port);
}
static void key_report(uint8_t address, std::initializer_list<unsigned> keys, uint8_t modifiers=0, uint8_t instance=0) {
    uint8_t report[8]={modifiers,0};
    unsigned i=2;
    for (unsigned key : keys) report[i++]=key;
    handle_received_report(report,8,(address<<8)|instance);
}
static void fresh(bool boot=false) {
    for (unsigned a=1; a<16; ++a) device_disconnected_callback(a);
    inject_clear_keys();
    output_boot=boot;
    boot_protocol_keyboard=boot;
    parse_our_descriptor();
    set_mapping_from_config();
    reset_output_reports();
    received.clear(); events.clear(); activity=0;
    set_monitor_enabled(true);
}

int main() {
    our_descriptor=&our_descriptors[0];
    unmapped_passthrough_layer_mask=1;
    fresh(); attach(1);
    // Three taps before a 1ms tick, with repeated submission failures.
    for (unsigned key : {4,5,6}) { key_report(1,{key}); key_report(1,{}); }
    for (unsigned i=0;i<100;++i) assert(!send_report(reject));
    drain();
    assert((received==std::vector<std::set<unsigned>>{{4},{},{5},{},{6},{}}));
    assert(activity==3);
    assert(events.size()==6); // boot array KEYUP monitoring
    std::puts("PASS physical sub-tick taps, retry order, array KEYUP, activity");

    fresh(); attach(1); key_report(1,{4}); key_report(1,{});
    assert(!send_report(async_send)); assert((decode(submitted.data(),submitted.size())==std::set<unsigned>{4}));
    auto first=submitted; usb_tx.failed(); assert(!send_report(async_send)); assert(submitted==first);
    usb_tx.complete(); assert(send_report(async_send));
    assert(!send_report(async_send)); assert(decode(submitted.data(),submitted.size()).empty());
    usb_tx.complete(); assert(send_report(async_send)); assert(!send_report(async_send));
    // Monitor frame must also remain immutable while other input arrives.
    assert(!send_monitor_report(async_send)); first=submitted;
    key_report(1,{5}); usb_tx.failed(); assert(!send_monitor_report(async_send)); assert(submitted==first);
    usb_tx.complete(); assert(send_monitor_report(async_send));
    assert(!send_monitor_report(async_send)); assert(submitted!=first);
    usb_tx.complete(); assert(send_monitor_report(async_send));
    std::puts("PASS real FIFO retains DOWN until USB completion, then delivers UP");

    fresh(); attach(1); attach(2); key_report(1,{4},2); key_report(2,{4,5});
    inject_key_down(0x70006,500); process_mapping(false); drain();
    assert((received.back()==std::set<unsigned>{4,5,6,0xE1}));
    device_disconnected_callback(1); drain();
    assert((received.back()==std::set<unsigned>{4,5,6}));
    key_report(1,{7}); drain(); // stale completion must be ignored
    assert((received.back()==std::set<unsigned>{4,5,6}));
    device_disconnected_callback(2); drain(); assert((received.back()==std::set<unsigned>{6}));
    inject_key_up(0x70006); process_mapping(false); drain(); assert(received.back().empty());
    for (unsigned i=0;i<100;++i) { attach(1); key_report(1,{6},2); drain(); device_disconnected_callback(1); drain(); assert(received.back().empty()); }
    std::puts("PASS disconnect, modifiers, independent owners, stale receive, 100 reconnects");

    fresh(); attach(1); key_report(1,{4}); drain();
    uint8_t truncated[2]={}; handle_received_report(truncated,2,0x100); drain();
    assert((received.back()==std::set<unsigned>{4}));
    key_report(1,{1,1,1,1,1,1}); drain(); assert((received.back()==std::set<unsigned>{4}));
    key_report(1,{}); drain(); assert(received.back().empty());
    std::puts("PASS truncated and rollover reports do not create false releases");

    fresh(); attach(1,true); uint8_t report[16]={2,2,1};
    handle_received_report(report,16,0x100); drain(); assert((received.back()==std::set<unsigned>{4,0xE1}));
    device_disconnected_callback(1); drain(); assert(received.back().empty());
    std::puts("PASS NKRO report-id keyboard disconnect");

    fresh(); attach(1); key_report(1,{0x2B,0x52}); key_report(1,{0x2B,0x52});
    key_report(1,{}); key_report(1,{0x48}); key_report(1,{0x48}); drain();
    unsigned shortcuts=0; for (auto e:events) if(e.usage==0xFFFC0001) ++shortcuts;
    assert(shortcuts==2);
    std::puts("PASS Pause and Tab+Up edge-only shortcuts, physical passthrough");

    fresh(); attach(1);
    // Force pressure far beyond available RAM; the final UP must survive.
    for (unsigned i=0;i<300;++i) { key_report(1,{4+(i%26)}); key_report(1,{}); }
    assert(!input_queue_has_room()); assert(pending_report_wakes_host());
    drain(); assert(received.back().empty()); assert(input_queue_has_room());
    key_report(1,{5}); key_report(1,{}); drain(); assert(received.back().empty());
    std::puts("PASS saturated queue converges to release and recovers");

    fresh(true); attach(1); key_report(1,{4}); inject_key_down(0x70004,500); process_mapping(false); drain();
    assert((received.back()==std::set<unsigned>{4}));
    key_report(1,{}); drain(); assert((received.back()==std::set<unsigned>{4}));
    now+=500001; process_mapping(true); drain(); assert(received.back().empty());
    std::puts("PASS boot protocol physical+injected same-key merge and lease expiry");

    fresh(); attach(1); std::mt19937 random(20260906);
    for (unsigned i=0;i<10000;++i) {
        unsigned key=4+random()%26; key_report(1,{key}); key_report(1,{});
        for (unsigned j=0;j<random()%8;++j) assert(!send_report(reject));
        drain(); assert((received[received.size()-2]==std::set<unsigned>{key})); assert(received.back().empty());
    }
    assert(received.size()==20000);
    std::puts("PASS 10000 taps / 20000 ordered states with random submission failures");

    HidDelivery tx; unsigned submits=0;
    assert(!tx.send([&]{++submits;return false;}));
    assert(!tx.send([&]{++submits;return true;}));
    assert(!tx.send([&]{++submits;return true;})); assert(submits==2);
    tx.failed(); assert(!tx.send([&]{++submits;return true;}));
    tx.complete(); assert(tx.send([&]{++submits;return true;})); assert(submits==3);
    tx.reset(); assert(!tx.send([]{return true;}));
    std::puts("PASS transfer submission vs completion and failed completion retry");

    HidReceiveRetry<16> rx; unsigned attempts=0;
    rx.mount(1,0); rx.retry([&](uint8_t,uint8_t){++attempts;return false;});
    rx.retry([&](uint8_t,uint8_t){++attempts;return true;});
    rx.retry([&](uint8_t,uint8_t){++attempts;return true;}); assert(attempts==2);
    rx.received(1,0); rx.unmount(1); rx.received(1,0);
    rx.retry([&](uint8_t,uint8_t){++attempts;return true;}); assert(attempts==2);
    rx.mount(1,0); rx.retry([&](uint8_t,uint8_t){++attempts;return true;}); assert(attempts==3);
    std::puts("PASS receive re-arm retry, unmount cancellation, interface reuse");
}
