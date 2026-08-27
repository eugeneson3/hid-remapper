#include "jarvis_diagnostics.h"

#include <string.h>

#include "quantum.h"
#include "raw_hid.h"
#include "tusb.h"

#define JARVIS_DIAG_VERSION 1
#define JARVIS_DIAG_PACKET_SIZE 32
#define JARVIS_DIAG_INTERFACE_COUNT 8
#define JARVIS_DIAG_DESCRIPTOR_SIZE 1024
#define JARVIS_DIAG_EVENT_COUNT 32
#define JARVIS_DIAG_RAW_REPORT_SIZE 64
#define JARVIS_DIAG_PARSED_REPORT_SIZE 32

#define JARVIS_DIAG_STATUS_OK 0
#define JARVIS_DIAG_STATUS_BAD_ARGUMENT 1
#define JARVIS_DIAG_STATUS_NOT_AVAILABLE 2
#define JARVIS_DIAG_STATUS_OVERWRITTEN 3
#define JARVIS_DIAG_STATUS_UNKNOWN_COMMAND 0x7F

#define JARVIS_DIAG_CMD_STATUS 0xD0
#define JARVIS_DIAG_CMD_INTERFACE 0xD1
#define JARVIS_DIAG_CMD_DESCRIPTOR 0xD2
#define JARVIS_DIAG_CMD_RAW_EVENT 0xD3
#define JARVIS_DIAG_CMD_PARSED_EVENT 0xD4

typedef struct {
    volatile uint32_t generation;
    volatile uint8_t mounted;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t protocol;
    uint16_t vid;
    uint16_t pid;
    uint16_t descriptor_length;
    uint16_t descriptor_stored_length;
    uint8_t descriptor[JARVIS_DIAG_DESCRIPTOR_SIZE];
} jarvis_diag_interface_t;

typedef struct {
    volatile uint32_t published_sequence;
    uint16_t original_length;
    uint8_t stored_length;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t data[JARVIS_DIAG_RAW_REPORT_SIZE];
} jarvis_diag_raw_event_t;

typedef struct {
    volatile uint32_t published_sequence;
    uint8_t stored_length;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t data[JARVIS_DIAG_PARSED_REPORT_SIZE];
} jarvis_diag_parsed_event_t;

static jarvis_diag_interface_t interfaces[JARVIS_DIAG_INTERFACE_COUNT];
static jarvis_diag_raw_event_t raw_events[JARVIS_DIAG_EVENT_COUNT];
static jarvis_diag_parsed_event_t parsed_events[JARVIS_DIAG_EVENT_COUNT];
static volatile uint32_t interface_generation;
static volatile uint32_t raw_latest_sequence;
static volatile uint32_t parsed_latest_sequence;

static void memory_barrier(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint16_t read_u16(uint8_t const* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(uint8_t const* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static jarvis_diag_interface_t* find_interface(uint8_t dev_addr,
                                                uint8_t instance,
                                                bool create) {
    jarvis_diag_interface_t* available = NULL;

    for (uint8_t index = 0; index < JARVIS_DIAG_INTERFACE_COUNT; index++) {
        jarvis_diag_interface_t* candidate = &interfaces[index];
        if (candidate->dev_addr == dev_addr && candidate->instance == instance &&
            (candidate->mounted || candidate->descriptor_stored_length > 0)) {
            return candidate;
        }
        if (available == NULL && !candidate->mounted) {
            available = candidate;
        }
    }

    return create ? available : NULL;
}

void jarvis_diag_mount(uint8_t dev_addr, uint8_t instance, uint8_t protocol,
                       uint8_t const* descriptor, uint16_t descriptor_length) {
    jarvis_diag_interface_t* slot = find_interface(dev_addr, instance, true);
    if (slot == NULL) {
        return;
    }

    slot->mounted = 0;
    memory_barrier();

    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    uint16_t stored_length = descriptor_length;
    if (stored_length > JARVIS_DIAG_DESCRIPTOR_SIZE) {
        stored_length = JARVIS_DIAG_DESCRIPTOR_SIZE;
    }

    slot->dev_addr = dev_addr;
    slot->instance = instance;
    slot->protocol = protocol;
    slot->vid = vid;
    slot->pid = pid;
    slot->descriptor_length = descriptor_length;
    slot->descriptor_stored_length = stored_length;
    if (stored_length > 0 && descriptor != NULL) {
        memcpy(slot->descriptor, descriptor, stored_length);
    }
    slot->generation = ++interface_generation;

    memory_barrier();
    slot->mounted = 1;
}

void jarvis_diag_unmount(uint8_t dev_addr, uint8_t instance) {
    jarvis_diag_interface_t* slot = find_interface(dev_addr, instance, false);
    if (slot == NULL) {
        return;
    }

    slot->mounted = 0;
    memory_barrier();
    slot->generation = ++interface_generation;
}

void jarvis_diag_raw_report(uint8_t dev_addr, uint8_t instance,
                            uint8_t const* report, uint16_t report_length) {
    uint32_t sequence = raw_latest_sequence + 1;
    jarvis_diag_raw_event_t* event =
        &raw_events[(sequence - 1) % JARVIS_DIAG_EVENT_COUNT];

    event->published_sequence = 0;
    memory_barrier();

    uint8_t stored_length = report_length;
    if (report_length > JARVIS_DIAG_RAW_REPORT_SIZE) {
        stored_length = JARVIS_DIAG_RAW_REPORT_SIZE;
    }

    event->original_length = report_length;
    event->stored_length = stored_length;
    event->dev_addr = dev_addr;
    event->instance = instance;
    if (stored_length > 0 && report != NULL) {
        memcpy(event->data, report, stored_length);
    }

    memory_barrier();
    event->published_sequence = sequence;
    memory_barrier();
    raw_latest_sequence = sequence;
}

void jarvis_diag_parsed_report(uint8_t encoded_interface,
                               uint8_t const* keyboard_bits,
                               uint8_t keyboard_bits_length) {
    uint32_t sequence = parsed_latest_sequence + 1;
    jarvis_diag_parsed_event_t* event =
        &parsed_events[(sequence - 1) % JARVIS_DIAG_EVENT_COUNT];

    event->published_sequence = 0;
    memory_barrier();

    uint8_t stored_length = keyboard_bits_length;
    if (stored_length > JARVIS_DIAG_PARSED_REPORT_SIZE) {
        stored_length = JARVIS_DIAG_PARSED_REPORT_SIZE;
    }

    event->stored_length = stored_length;
    event->dev_addr = encoded_interface / 16;
    event->instance = encoded_interface % 16;
    if (stored_length > 0 && keyboard_bits != NULL) {
        memcpy(event->data, keyboard_bits, stored_length);
    }

    memory_barrier();
    event->published_sequence = sequence;
    memory_barrier();
    parsed_latest_sequence = sequence;
}

static void respond_status(uint8_t* response) {
    response[2] = JARVIS_DIAG_STATUS_OK;
    response[3] = JARVIS_DIAG_INTERFACE_COUNT;
    response[4] = JARVIS_DIAG_EVENT_COUNT;
    response[5] = JARVIS_DIAG_EVENT_COUNT;
    write_u32(&response[6], raw_latest_sequence);
    write_u32(&response[10], parsed_latest_sequence);
    write_u32(&response[14], interface_generation);
    write_u16(&response[18], JARVIS_DIAG_DESCRIPTOR_SIZE);
    response[20] = JARVIS_DIAG_RAW_REPORT_SIZE;
    response[21] = JARVIS_DIAG_PARSED_REPORT_SIZE;
}

static void respond_interface(uint8_t const* request, uint8_t* response) {
    uint8_t slot_index = request[1];
    if (slot_index >= JARVIS_DIAG_INTERFACE_COUNT) {
        response[2] = JARVIS_DIAG_STATUS_BAD_ARGUMENT;
        return;
    }

    jarvis_diag_interface_t const* slot = &interfaces[slot_index];
    response[2] = JARVIS_DIAG_STATUS_OK;
    response[3] = slot_index;
    response[4] = slot->mounted;
    response[5] = slot->dev_addr;
    response[6] = slot->instance;
    response[7] = slot->protocol;
    write_u16(&response[8], slot->vid);
    write_u16(&response[10], slot->pid);
    write_u16(&response[12], slot->descriptor_length);
    write_u16(&response[14], slot->descriptor_stored_length);
    write_u32(&response[16], slot->generation);
}

static void respond_descriptor(uint8_t const* request, uint8_t* response) {
    uint8_t slot_index = request[1];
    uint16_t offset = read_u16(&request[2]);
    if (slot_index >= JARVIS_DIAG_INTERFACE_COUNT) {
        response[2] = JARVIS_DIAG_STATUS_BAD_ARGUMENT;
        return;
    }

    jarvis_diag_interface_t const* slot = &interfaces[slot_index];
    if (slot->descriptor_stored_length == 0) {
        response[2] = JARVIS_DIAG_STATUS_NOT_AVAILABLE;
        return;
    }
    if (offset >= slot->descriptor_stored_length) {
        response[2] = JARVIS_DIAG_STATUS_BAD_ARGUMENT;
        return;
    }

    uint16_t remaining = slot->descriptor_stored_length - offset;
    uint8_t chunk_length = remaining > 21 ? 21 : (uint8_t)remaining;

    response[2] = JARVIS_DIAG_STATUS_OK;
    response[3] = slot_index;
    write_u16(&response[4], slot->descriptor_length);
    write_u16(&response[6], slot->descriptor_stored_length);
    write_u16(&response[8], offset);
    response[10] = chunk_length;
    memcpy(&response[11], &slot->descriptor[offset], chunk_length);
}

static uint8_t event_status(uint32_t requested_sequence,
                            uint32_t latest_sequence) {
    if (requested_sequence == 0 || requested_sequence > latest_sequence) {
        return JARVIS_DIAG_STATUS_NOT_AVAILABLE;
    }
    if (latest_sequence - requested_sequence >= JARVIS_DIAG_EVENT_COUNT) {
        return JARVIS_DIAG_STATUS_OVERWRITTEN;
    }
    return JARVIS_DIAG_STATUS_OK;
}

static void respond_raw_event(uint8_t const* request, uint8_t* response) {
    uint32_t sequence = read_u32(&request[1]);
    uint8_t offset = request[5];
    uint32_t latest_sequence = raw_latest_sequence;
    uint8_t status = event_status(sequence, latest_sequence);
    if (status != JARVIS_DIAG_STATUS_OK) {
        response[2] = status;
        write_u32(&response[4], latest_sequence);
        return;
    }

    jarvis_diag_raw_event_t const* event =
        &raw_events[(sequence - 1) % JARVIS_DIAG_EVENT_COUNT];
    if (event->published_sequence != sequence) {
        response[2] = JARVIS_DIAG_STATUS_OVERWRITTEN;
        return;
    }
    if (offset >= event->stored_length) {
        response[2] = JARVIS_DIAG_STATUS_BAD_ARGUMENT;
        return;
    }

    uint8_t chunk_length = event->stored_length - offset;
    if (chunk_length > 17) {
        chunk_length = 17;
    }

    response[2] = JARVIS_DIAG_STATUS_OK;
    response[3] = 1;
    write_u32(&response[4], sequence);
    response[8] = event->dev_addr;
    response[9] = event->instance;
    write_u16(&response[10], event->original_length);
    response[12] = event->stored_length;
    response[13] = offset;
    response[14] = chunk_length;
    memcpy(&response[15], &event->data[offset], chunk_length);
}

static void respond_parsed_event(uint8_t const* request, uint8_t* response) {
    uint32_t sequence = read_u32(&request[1]);
    uint8_t offset = request[5];
    uint32_t latest_sequence = parsed_latest_sequence;
    uint8_t status = event_status(sequence, latest_sequence);
    if (status != JARVIS_DIAG_STATUS_OK) {
        response[2] = status;
        write_u32(&response[4], latest_sequence);
        return;
    }

    jarvis_diag_parsed_event_t const* event =
        &parsed_events[(sequence - 1) % JARVIS_DIAG_EVENT_COUNT];
    if (event->published_sequence != sequence) {
        response[2] = JARVIS_DIAG_STATUS_OVERWRITTEN;
        return;
    }
    if (offset >= event->stored_length) {
        response[2] = JARVIS_DIAG_STATUS_BAD_ARGUMENT;
        return;
    }

    uint8_t chunk_length = event->stored_length - offset;
    if (chunk_length > 17) {
        chunk_length = 17;
    }

    response[2] = JARVIS_DIAG_STATUS_OK;
    response[3] = 2;
    write_u32(&response[4], sequence);
    response[8] = event->dev_addr;
    response[9] = event->instance;
    write_u16(&response[10], event->stored_length);
    response[12] = event->stored_length;
    response[13] = offset;
    response[14] = chunk_length;
    memcpy(&response[15], &event->data[offset], chunk_length);
}

void raw_hid_receive(uint8_t* data, uint8_t length) {
    if (length == 0 || data == NULL) {
        return;
    }

    uint8_t response[JARVIS_DIAG_PACKET_SIZE] = {0};
    response[0] = data[0];
    response[1] = JARVIS_DIAG_VERSION;

    switch (data[0]) {
        case JARVIS_DIAG_CMD_STATUS:
            respond_status(response);
            break;
        case JARVIS_DIAG_CMD_INTERFACE:
            respond_interface(data, response);
            break;
        case JARVIS_DIAG_CMD_DESCRIPTOR:
            respond_descriptor(data, response);
            break;
        case JARVIS_DIAG_CMD_RAW_EVENT:
            respond_raw_event(data, response);
            break;
        case JARVIS_DIAG_CMD_PARSED_EVENT:
            respond_parsed_event(data, response);
            break;
        default:
            response[2] = JARVIS_DIAG_STATUS_UNKNOWN_COMMAND;
            break;
    }

    raw_hid_send(response, sizeof(response));
}
