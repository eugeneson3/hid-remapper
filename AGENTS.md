# Feather nightly maintenance

- This candidate is derived directly from `cb0697468050e67e184e0df644d995f9aab2923e`.
- Board `feather_host`, target `remapper`, VID/PID `046D:C52B`, config version 18.
- Preserve physical passthrough, physical/injected ownership, commands 22/26/27/28/30 and FFFC shortcuts.
- Keep this file and README.md synchronized with firmware behavior and build procedures.
- Run `python3 tools/patch-tinyusb.py`, then `python3 tests/test_hid_reliability.py` before ARM compilation.
- The output FIFO retains its head until TinyUSB completes the transfer; submission or remote wakeup alone is not delivery. A failed completion is retryable.
- Main-context 1ms receive retries must remain bounded. No SOF callbacks before TinyUSB host initialization.
- On USB-A detach, remove that device's ownership before releasing its interface indices. Preserve other keyboards and injected keys; ignore late callbacks.
- A full queue records the latest state for each report, always clears staging, and converges after draining. Finite buffers cannot preserve unlimited input during USB suspension.
- D13 PWM activity is triggered only by physical keyboard DOWN; auto mode owns breathing and expires on OFF/TTL/USB unmount. Do not control the CHG LED.
- Do not overwrite the Jarvis stable or nightly_v2 image. Hardware validation and promotion require explicit evidence; host mocks do not prove PC/application delivery.
- Both RP2040 workflows must apply the TinyUSB guard and run the host tests. The Bluetooth build defines JARVIS_BLUETOOTH and keeps its existing LED/tick scheduling; do not link RP2040 activity or tick helpers into Zephyr.
