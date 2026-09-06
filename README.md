# HID Remapper

_For user documentation please see the project's website at [remapper.org](https://www.remapper.org/)._

This is a configurable USB dongle that allows you to remap inputs from mice, keyboards and other devices. It works completely in hardware and requires no software running on the computer during normal use.

It can do things like reassign buttons, change keyboard layouts, map mouse buttons to keyboard inputs, map keystrokes to mouse inputs, change mouse sensitivity (permanently or when a button is held), rotate mouse axes by arbitrary (non-90 degree) angles, drag-lock for mouse buttons, scroll by moving the mouse, and much more.

It is configurable [through a web browser](https://www.remapper.org/config/) using WebHID (Chrome or Chrome-based browser required).

## Jarvis Feather candidate

This checkout contains an unpromoted Jarvis-specific Feather candidate derived directly from commit `cb0697468050e67e184e0df644d995f9aab2923e`. The Jarvis app's known-good `firmware_stable.uf2` is not replaced by this candidate.

For the `feather_host` `remapper` target, the candidate adds:

- read-only physical-key shortcut observation without modifying or consuming pass-through reports;
- existing `FF00:0021` monitor events `FFFC:0001` (Pause or Tab+Up), `0002` (Tab+Down), and reserved `0003/0004` (Tab+Left/Right);
- one event per rising edge with latch reset on key release, monitor disable, keyboard disconnect, or PC USB unmount;
- feature command 30 with packed payload `running:uint8` followed by `ttl_ms:uint16` little-endian;
- a GPIO13/D13 red LED smoothstep breathing status with one second maximum-to-minimum and one second minimum-to-maximum halves.

Outside auto-attack, D13 gives its existing 50 ms activity pulse only when a physical keyboard key changes from up to down. A key-up-only report does not pulse it; while auto-attack is ON, the breathing status always takes precedence.

Command 30 turns breathing on only for `running=1`. A zero TTL selects 5 seconds; nonzero TTLs are clamped to 1--30 seconds. Repeated ON reports refresh expiry without restarting the animation. OFF, TTL expiry, USB unmount, and reboot stop breathing immediately. Existing commands 22 and 26--28, configuration version 18, VID/PID `046D:C52B`, descriptors, and physical/injected-key merging remain unchanged.

Build the current candidate without the boot-time LED self-test:

```sh
cd firmware
PICO_BOARD=feather_host cmake -S . -B build-nightly-v2-20260830 \
  -DCMAKE_BUILD_TYPE=Release \
  -DJARVIS_D13_LED_SELF_TEST=OFF
cmake --build build-nightly-v2-20260830 --target remapper --parallel 4
```

### nightly_v2: HID delivery reliability

The 2026-08-30 v2 candidate keeps the current shortcut and D13 contracts while independently improving the stable-baseline delivery paths:

- a failed TinyUSB HID submission retains the queue head and its DOWN/UP ordering;
- remote wakeup requests do not count as report delivery; all queued reports and newly staged input can request wakeup, so a non-waking head or a full queue cannot hide a key-down;
- an accepted remote-wakeup request is latched once per suspend interval, and the report queue remains intact until actual submission;
- the output queue has 16 entries, and queue saturation never skips clearing a staging report;
- failed physical-HID receive registration is retried in main context on the next 1 ms tick, with per-interface state cleared on unmount;
- intentional discard after a descriptor change remains separate from transient submission failure.

`prev_reports` remains the last-enqueued baseline so retries do not repeatedly enqueue the same absolute state. Submission success is not a PC-level ACK, and a finite queue cannot preserve unlimited transitions during a prolonged USB outage. This v2 does not add keyboard-unplug held-key release, PIO recovery, VBUS cycling, or a watchdog. The companion Jarvis change serializes lease refresh with DOWN/UP/CLEAR to prevent a stale refresh DOWN after release.

Run the native mock-USB regression harness from the repository root:

```sh
python3 tests/test_hid_reliability.py
```

The artifact is `firmware/build-nightly-v2-20260830/remapper.uf2`, copied separately to Jarvis as `firmware/firmware_nightly_v2.uf2`: 366,592 bytes, SHA-256 `0D195E8BF4FCE259E1D363D9CCA28B1951AAF64490D7FA46CF7C3CF2FE470D51`. It was built with WSL Ubuntu 22.04, ARM GCC 10.3.1, Release, and LED self-test OFF. Its source is the `4adca8c` working tree plus the uncommitted reliability changes; the parent firmware tree of `4adca8c` is byte-identical to `cb069746...`. No previous hardening patch was copied.

The prior Jarvis nightly remains 364,544 bytes with SHA-256 `D582AB0699B8066AB98D8D892D4C320B7E0FCD958B53007B7A4CACFB12186E80`. Neither that file nor `firmware_stable.uf2` is overwritten. Hardware validation of physical pass-through, injected hold/release, merged keys, shortcuts, LED behavior, and 10,000 input transitions is still required before promotion.

Wireless receivers are supported and multiple devices can be connected at the same time using a USB hub (with different mappings for each device if desired).

In addition to the remapping functionality, it can do polling rate overclocking up to 1000 Hz.

A separate [serial](SERIAL.md) version of the remapper takes inputs from a serial (RS-232) mouse and translates them to USB.

There's also a [Bluetooth](BLUETOOTH.md) version that runs on nRF52840-based boards, which translates Bluetooth inputs to USB.

![HID Remapper](images/remapper1.jpg)

## How to make the device

There are three main ways of making the HID Remapper. You can either buy [this board](https://www.adafruit.com/product/5723) from Adafruit, make it yourself using a Raspberry Pi Pico (or two), or you can use the provided files to manufacture a custom board at JLCPCB or a similar service. The functionality is the same in all cases.

If you get the Feather RP2040 USB Host board from Adafruit, the device is ready to use, you just need to flash it with the right firmware ([remapper\_feather.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_feather.uf2)). Hold the "Boot" button on the board, then press the "Reset" button. A USB drive should show up on your computer. Copy the UF2 file to that drive. That's it.

See [here](HARDWARE.md) for details on how to make the Pico variants of the device and [here](custom-boards/) for details on the custom board option.

## How to use the configuration tool

A live version of the web configuration tool can be found at [remapper.org/config](https://www.remapper.org/config/).

For details on how to use it, please see the [HID Remapper Manual](https://www.remapper.org/manual/).

If you can't use the browser-based configuration tool, there's also a [command-line tool](config-tool) that takes JSON in the same format as the web tool on standard input. I only tested it on Linux, but in theory it should also run on Windows and Mac.

## How to update the firmware

The procedure to update the firmware is similar on all variants. When you go to the configuration website and try to connect to your device when it doesn't have the latest firmware, you will get a message and a link to a version of the configuration interface that is compatible with your current (old) firmware. Click that link, connect to your HID Remapper by clicking "Open device" as usual, then go to the "Actions" tab and click "Flash firmware". This will put your device in firmware flashing mode. A drive should appear on your computer. For all the RP2040-based variants, the drive will be named "RPI-RP2". For the Bluetooth variants, it will be called something else, depending on what board you're using. Download the correct firmware file for your variant (see table below) and copy it to that drive. On custom boards v1, v2, v5, v6 and v7 (dual RP2040 boards), after flashing the firmware you have to disconnect and reconnect your HID Remapper. That's it, you can go back to the regular version of the configuration interface and carry on.

_(Please note that previously a manual "Flash B side" step was required on custom boards v1, v2, v5, v6 and v7. That is no longer necessary.)_

If you're using the dual Pico variant then you need to flash the A side using the [remapper\_dual\_a.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_dual_a.uf2) file as described above and then flash the B side manually. Disconnect your HID Remapper from your computer, disconnect the OTG adapter from the B-side Pico, hold the BOOTSEL button on the B-side Pico and then, while holding the button, connect the B-side Pico to your computer. A drive named "RPI-RP2" should appear. Copy the [remapper\_dual\_b.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_dual_b.uf2) file to that drive. Disconnect the B-side Pico from your computer, reconnect the OTG adapter and reconnect your HID Remapper to your computer.

When updating firmware, the current configuration on your HID Remapper is preserved. For extra peace of mind you can export your configuration to a JSON file before performing the update. That way if you need to revert to the old version of the firmware for any reason, you'll be able to import the configuration from the JSON file (configuration is lost when going from a newer firmware to an older firmware).

variant | firmware file(s) | notes
------- | ---------------- | -----------------------
single Pico | [remapper.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper.uf2) |
dual Pico | [remapper\_dual\_a.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_dual_a.uf2)<br>[remapper\_dual\_b.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_dual_b.uf2) | each Pico needs to be flashed separately
Feather RP2040 with USB Host | [remapper\_feather.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_feather.uf2) |
custom board v1 | [remapper\_board.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board.uf2) | disconnect and reconnect after flashing
custom board v2 | [remapper\_board.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board.uf2) | disconnect and reconnect after flashing
custom board v3 | [remapper\_feather.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_feather.uf2) |
custom board v4 | [remapper\_feather.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_feather.uf2) |
custom board v5 | [remapper\_board.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board.uf2) | disconnect and reconnect after flashing
custom board v6 | [remapper\_board.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board.uf2) | disconnect and reconnect after flashing
custom board v7 | [remapper\_board\_v7.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board_v7.uf2) | disconnect and reconnect after flashing
custom board v8 | [remapper\_board\_v8.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_board_v8.uf2) |
Feather nRF52840 Express | [remapper_adafruit_feather_nrf52840.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_adafruit_feather_nrf52840.uf2) |
Xiao nRF52840 | [remapper_seeed_xiao_nrf52840.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_seeed_xiao_nrf52840.uf2) |
serial | [remapper_serial.uf2](https://github.com/jfedor2/hid-remapper/releases/latest/download/remapper_serial.uf2) |

For boards not listed above, use the same file name you used when flashing it for the first time.

## How to compile the firmware

The easiest way to compile the firmware is to let GitHub do it for you. This repository has GitHub Actions that build the firmware, so you can just fork, make your changes, wait for the job to complete, and look for the binaries in the artifacts produced.

To compile the RP2040 firmware on your machine, use the following steps (details may vary depending on your Linux distribution):

```
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib srecord
git clone https://github.com/jfedor2/hid-remapper.git
cd hid-remapper
git submodule update --init
cd firmware
mkdir build
cd build
cmake ..
# or, to build for the custom boards:
# PICO_BOARD=remapper cmake ..
make
```

To compile the nRF52 firmware, you can either follow [Nordic's setup instructions](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation.html) and then `west build -b seeed_xiao_nrf52840` to compile the firmware, or you can use Docker with a command like this (start from the top level of the repository or adjust the path accordingly):

```
docker run --rm -v $(pwd):/workdir/project -w /workdir/project/firmware-bluetooth nordicplayground/nrfconnect-sdk:v2.2-branch west build -b seeed_xiao_nrf52840
```

## License

The software in this repository is licensed under the [MIT License](LICENSE), unless stated otherwise.

The hardware designs in this repository are licensed under the Creative Commons Attribution 4.0 International license ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)), unless stated otherwise.
