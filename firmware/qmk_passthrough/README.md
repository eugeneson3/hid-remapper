# Jarvis QMK passthrough nightly

This build uses the structure and generic full-size keymap from
`whyaaronbailey/adafruit_rp2040_usbh` rather than the retired Jarvis C0 target.

Pinned inputs:

- QMK: tag `0.24.0`, commit `4e369d405af6bba1adce6337b2e1b1ea1788566c`
- Converter: commit `8df86fe375d7055df618399f0d2f2f34dc2fc3a0`
- Converter Pico-PIO-USB submodule: `447ea437fde9ba050281f3827ea4b41921833a90`
- Converter TinyUSB submodule: `2179fb1bd93b71d755c4bf212aff7094f5e8337d`

The converter keeps USB host processing on RP2040 core 1, generates the 1 ms
PIO USB host frame there, parses the attached keyboard's HID report descriptor,
and publishes the parsed state through QMK's keyboard report path.

The Jarvis overlay is deliberately small:

- keep USB VID/PID `046D:C52B`;
- use manufacturer name `Logitech`;
- use product name `USB Receiver`, matching the product string normally used by
  Logitech `046D:C52B` receivers;
- copy the upstream `default` keymap as `jarvis_baseline`;
- replace the unused console interface with a read-only Raw HID diagnostic
  interface, and disable virtual serial, mouse keys, tap dance, and combos;
- accept the existing Jarvis key-down, key-up, and clear commands over the same
  Raw HID collection;
- keep physical and injected keyboard states separate and publish their union;
- publish USB-A reports with a connection generation using acquire/release
  atomics, discard reports from an older generation, and clear every physical
  key when mount or unmount changes that generation;
- after every mount, suppress physical keyboard input until the keyboard has
  produced an all-keys-released report, so a key held through reconnection is
  not replayed;
- cap copied USB-A reports at the real 64-byte buffer size;
- keep the pinned Pico-PIO-USB RX bounds fix and backport the independently
  tested 1200 us absolute RX deadline from upstream PR #210, preventing a noisy
  disconnect from keeping core 1 in the receive loop forever;
- keep NKRO and Consumer/System Control support.

The diagnostic protocol uses Raw HID usage `FF60:0061`. It exposes attached HID
interface metadata, up to 1024 bytes of each report descriptor, a 32-entry ring
of raw USB-A reports, and a separate 32-entry ring of parsed keyboard bitsets.
Commands `D0` through `D4` remain read-only and cannot alter parser state. The
host tool in Jarvis is `tools/diagnose_qmk_passthrough.py`.

Jarvis injection uses the same 32-byte command body as the stable firmware:

- byte 0: config version `18`;
- byte 1: command `26` (down), `27` (up), or `28` (clear injected keys);
- bytes 2-5: little-endian full HID usage (`0007:xxxx` for keyboard keys);
- bytes 6-7: little-endian key-down lease in milliseconds, default 500 and
  clamped to 1000;
- bytes 8-27: reserved zero bytes;
- bytes 28-31: little-endian CRC32 over bytes 0-27.

Injection commands intentionally have no Raw HID response. A key-down lease is
refreshed by Jarvis while the key remains logically held, so a stopped or
disconnected host process releases injected keys within one second. Physical
and injected states are OR-merged; releasing one source does not release a key
still held by the other source.

The first capture-backed parser fix adds `len > 0` to both keyboard report
loops. A descriptor may declare more array entries than TinyUSB delivered in
the current callback; the parser must stop at the received length instead of
reading bytes beyond the report buffer. Report layout, buffer sizes, key state,
and output behavior are otherwise unchanged.

Hardware reproduction showed three nondeterministic results when unplugging a
keyboard while a key was held: reconnect could remain dead, replay the held key,
or fail to release it. Two faults overlapped. The pinned Pico-PIO-USB revision
bounded the receive buffer but could still wait forever when no EOP arrived,
and the converter shared its disconnect flag and report mailbox between RP2040
cores without synchronization. The absolute RX deadline prevents the host-core
wait, while connection generations make a disconnect clear non-lossy and reject
old reports. The post-mount all-released gate prevents a held key from becoming
a new press during enumeration.

The earlier callback-level watchdog workaround is not present. It could not
recover a host core that froze before TinyUSB delivered the unmount callback.

Do not promote this nightly to stable until it passes keyboard-attached
power-on, ordinary and modifier input, Jarvis injection, physical/injected
same-key merging, disconnect release, reconnect, and long-duration key-stuck
tests on hardware.
