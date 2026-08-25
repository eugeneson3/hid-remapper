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
- use product name `Jarvis QMK Passthrough`;
- copy the upstream `default` keymap as `jarvis_baseline`;
- replace the unused console interface with a read-only Raw HID diagnostic
  interface, and disable virtual serial, mouse keys, tap dance, and combos;
- keep NKRO and Consumer/System Control support.

The diagnostic protocol uses Raw HID usage `FF60:0061`. It exposes attached HID
interface metadata, up to 1024 bytes of each report descriptor, a 32-entry ring
of raw USB-A reports, and a separate 32-entry ring of parsed keyboard bitsets.
Commands `D0` through `D4` are read-only and cannot inject keys or alter parser
state. The host tool in Jarvis is `tools/diagnose_qmk_passthrough.py`.

This baseline still has no Jarvis command-injection protocol. Do not promote it
to stable until it passes keyboard-attached power-on, ordinary and modifier
input, disconnect release, reconnect, and long-duration key-stuck tests on
hardware.
