# Feather nightly v3

- The user explicitly requested v3 based on nightly_v2 after the 2026-09-06
  stable-derived nightly had no physical keyboard passthrough on their board.
- Base: 4adca8c plus the original local v2 changes, snapshotted as c3d469b.
  Rebuilt v2 differs from the distributed v2 only in five build-date bytes.
- Keep v2 USB startup, PIO SOF timer placement, immediate receive re-arm,
  1ms receive retry, 16-report FIFO and successful-submission dequeue semantics.
  tests/v2-transport.json pins the unchanged transport/LED/driver files.
- Do not import the failed nightly delivery wrapper, deferred SOF startup,
  backpressure receiver, TinyUSB patch or LED rewrite.
- VID/PID 046D:C52B, config v18, FF00:0020/0021, commands 22/26/27/28/30
  and v2 shortcuts/status LED behavior remain compatible.
- Map each physical report and injection command before the next transition.
  Only the 1ms tick advances frame and macro duration.
- On USB-A removal, release the departing device's interface ownership before
  deleting descriptors. Preserve other keyboards and injected key leases.
- Tests: python3 tests/test_v2_transport.py; python3 tests/test_hid_reliability.py;
  python3 tests/test_v3_mapping.py (real parser/engine, ASan/UBSan).
- Build firmware for feather_host, target remapper, Release. Firmware is a
  separate firmware_nightly_v3.uf2 candidate. Never overwrite stable or v2.
- Software tests do not validate physical USB enumeration or PC delivery.
  Record the actual board result separately and update this file with changes.
