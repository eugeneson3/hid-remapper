# Jarvis QMK passthrough nightly

This build uses the structure and generic full-size keymap from
`whyaaronbailey/adafruit_rp2040_usbh` rather than the retired Jarvis C0 target.

Pinned inputs:

- QMK: tag `0.24.0`, commit `4e369d405af6bba1adce6337b2e1b1ea1788566c`
- Converter: commit `8df86fe375d7055df618399f0d2f2f34dc2fc3a0`
- Converter Pico-PIO-USB submodule: `528616d809ad3a400dc0cf4dab0f790e62944244`
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
- clear every physical key and any pending USB-A report when the keyboard is
  disconnected;
- schedule an RP2040 watchdog reboot 250 ms after a downstream HID disconnect,
  because the pinned PIO USB host stack can clear the old key state yet fail to
  enumerate the same keyboard after it is reconnected;
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

The disconnect recovery change is intentionally a full watchdog reboot rather
than a partial TinyUSB or PIO reset. Hardware reproduction showed that the
disconnect callback released a held key, but reconnecting the keyboard did not
resume reports until the Feather RESET button was pressed. A 250 ms delayed
reboot preserves that already-working release behavior and automatically takes
the same clean initialization path as the manual RESET recovery.

Do not promote this nightly to stable until it passes keyboard-attached
power-on, ordinary and modifier input, Jarvis injection, physical/injected
same-key merging, disconnect release, reconnect, and long-duration key-stuck
tests on hardware.
