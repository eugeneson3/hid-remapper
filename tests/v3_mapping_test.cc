#include <cassert>
#include <cstdio>
#include <cstring>
#include <queue>
#include <random>
#include <set>
#include <vector>
#include "globals.h"
#include "descriptor_parser.h"
#include "remapper.h"

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
const our_descriptor_def_t our_descriptors[NOUR_DESCRIPTORS] = {{
    .idx=0, .descriptor=nkro, .descriptor_length=sizeof(nkro),
    .handle_received_report=do_handle_received_report
}};
static uint64_t now = 1;
uint64_t get_time() { return now; }
void my_mutex_enter(MutexId) {}
void my_mutex_exit(MutexId) {}
void set_gpio_inout_masks(uint32_t, uint32_t) {}
void queue_out_report(uint16_t, uint8_t, const uint8_t*, uint8_t) {}
void apply_quirks(uint16_t, uint16_t, std::unordered_map<uint8_t, std::unordered_map<uint32_t, usage_def_t>>&, const uint8_t*, int, uint8_t) {}
extern uint64_t frame_counter;

static bool output_boot = false;
static std::vector<std::set<unsigned>> received;
static unsigned shortcut_count = 0;
static bool accept_monitor(uint8_t itf, const uint8_t* report, uint8_t) {
    assert(itf == 1);
    for (auto item : ((const monitor_report_t*)report)->items) {
        if (item.usage == 0xFFFC0001) ++shortcut_count;
    }
    return true;
}
static bool accept(uint8_t itf, const uint8_t* report, uint8_t len) {
    assert(itf == 0);
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
    received.push_back(keys);
    return true;
}
static bool reject(uint8_t, const uint8_t*, uint8_t) { return false; }
static void drain() { for (unsigned i=0; i<1024 && send_report(accept); ++i) {} }
static void attach(uint8_t address, bool use_nkro=false, uint8_t instance=0, uint8_t port=1) {
    uint16_t itf=(address<<8)|instance;
    parse_descriptor(0x1234,0x5678, use_nkro ? nkro : boot_kb_report_descriptor,
                     use_nkro ? sizeof(nkro) : sizeof(boot_kb_report_descriptor), itf, instance);
    device_connected_callback(itf,0x1234,0x5678,port);
}
static void keys(uint8_t address, std::initializer_list<unsigned> held, uint8_t modifiers=0, uint8_t instance=0) {
    uint8_t report[8]={modifiers,0};
    unsigned i=2;
    for (unsigned key : held) report[i++]=key;
    handle_received_report(report,8,(address<<8)|instance);
}
static void fresh(bool boot=false) {
    for (unsigned a=1; a<16; ++a) device_disconnected_callback(a);
    inject_clear_keys(); drain();
    output_boot=boot; boot_protocol_keyboard=boot;
    parse_our_descriptor(); set_mapping_from_config();
    received.clear();
}
int main() {
    our_descriptor=&our_descriptors[0]; unmapped_passthrough_layer_mask=1;
    fresh(); attach(1);
    auto frame=frame_counter;
    for (unsigned key : {4,5,6}) { keys(1,{key}); keys(1,{}); }
    for (unsigned i=0;i<100;++i) assert(!send_report(reject));
    drain();
    assert((received==std::vector<std::set<unsigned>>{{4},{},{5},{},{6},{}}));
    assert(frame_counter==frame);
    process_mapping(true); assert(frame_counter==frame+1);
    std::puts("PASS abc DOWN/UP in one tick, retry order, unchanged frame clock");

    fresh(); attach(1); attach(2);
    keys(1,{4},2); keys(2,{4,5},2);
    inject_key_down(0x70006,500); process_input_transition(); drain();
    assert((received.back()==std::set<unsigned>{4,5,6,0xE1}));
    device_disconnected_callback(1); drain();
    assert((received.back()==std::set<unsigned>{4,5,6,0xE1}));
    keys(1,{7}); drain(); // late callback from a disconnected device
    assert((received.back()==std::set<unsigned>{4,5,6,0xE1}));
    device_disconnected_callback(2); drain(); assert((received.back()==std::set<unsigned>{6}));
    inject_key_up(0x70006); process_mapping(false); drain(); assert(received.back().empty());
    for (unsigned i=0;i<100;++i) {
        attach(1); keys(1,{6},2); drain();
        device_disconnected_callback(1); drain(); assert(received.back().empty());
    }
    std::puts("PASS disconnect releases, shared key/modifier owners, injection, 100 reconnects");

    fresh(); attach(1,true); uint8_t nkro_report[16]={2,2,1};
    handle_received_report(nkro_report,16,0x100); drain();
    assert((received.back()==std::set<unsigned>{4,0xE1}));
    device_disconnected_callback(1); drain(); assert(received.back().empty());
    std::puts("PASS NKRO report ID and held modifier disconnect");

    fresh(); attach(1); keys(1,{4}); drain();
    keys(1,{1,1,1,1,1,1}); drain(); assert((received.back()==std::set<unsigned>{4}));
    keys(1,{}); drain(); assert(received.back().empty());
    std::puts("PASS rollover preserves held keys until valid release");

    fresh();
    inject_key_down(0x70004,500); process_input_transition();
    inject_key_up(0x70004); process_input_transition();
    inject_key_down(0x70005,500); process_input_transition();
    inject_clear_keys(); process_input_transition(); drain();
    assert((received==std::vector<std::set<unsigned>>{{4},{},{5},{}}));
    std::puts("PASS injection DOWN/UP/CLEAR in one tick without keyboard");

    fresh(); attach(1); attach(1,false,1); keys(1,{4},2); keys(1,{5},1,1); drain();
    assert((received.back()==std::set<unsigned>{4,5,0xE0,0xE1}));
    device_disconnected_callback(1); device_disconnected_callback(1); drain();
    assert(received.back().empty());
    std::puts("PASS composite keyboard releases all interfaces on repeated unmount");

    fresh();
    config_mappings = {{0x70005,0x70004,1000,1,0,1}, {0x70006,0x700E1,1000,1,0,1}};
    set_mapping_from_config(); attach(1); attach(2);
    keys(1,{4},2); keys(2,{4},2); drain();
    assert(received.back().count(5) && received.back().count(6));
    device_disconnected_callback(1); drain();
    assert(received.back().count(5) && received.back().count(6));
    keys(2,{}); drain(); assert(received.back().empty());
    config_mappings.clear(); set_mapping_from_config();
    std::puts("PASS port-specific array/modifier ownership with shared hub port");

    fresh(); attach(1); set_monitor_enabled(true); keys(1,{});
    for (auto held : {std::initializer_list<unsigned>{0x48}, {0x2B,0x52}}) {
        keys(1,held); keys(1,held); keys(1,{}); drain();
        while (send_monitor_report(accept_monitor)) {}
    }
    assert(shortcut_count==2); set_monitor_enabled(false);
    std::puts("PASS Pause/Tab+Up one event per press with physical passthrough");

    fresh(); attach(1);
    for (unsigned i=0;i<300;++i) { keys(1,{4+i%26}); keys(1,{}); }
    drain(); process_mapping(true); drain(); assert(received.back().empty());
    keys(1,{5}); keys(1,{}); drain(); assert(received.back().empty());
    std::puts("PASS v2 queue saturation converges to final release on tick");

    fresh(true); attach(1); keys(1,{4});
    inject_key_down(0x70004,500); process_mapping(false); drain();
    keys(1,{}); drain(); assert((received.back()==std::set<unsigned>{4}));
    now+=500001; process_mapping(true); drain(); assert(received.back().empty());
    std::puts("PASS boot protocol same-key physical/injection merge and TTL");

    fresh(); attach(1); std::mt19937 random(20260906);
    for (unsigned i=0;i<10000;++i) {
        unsigned key=4+random()%26; keys(1,{key}); keys(1,{});
        for (unsigned j=0;j<random()%8;++j) assert(!send_report(reject));
        drain(); assert((received[received.size()-2]==std::set<unsigned>{key}));
        assert(received.back().empty());
    }
    assert(received.size()==20000);
    std::puts("PASS 10000 taps / 20000 ordered states with submission failures");
}
