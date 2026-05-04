import argparse
import binascii
import struct
import sys
import time

import hid


USB_VID = 0xCAFE
USB_PID = 0xBAF2
CONFIG_VERSION = 18
REPORT_ID_CONFIG = 100
CONFIG_SIZE = 32
COMMAND_TRIGGER_LEFT_GUI_PULSE = 26


def build_command_report(command: int) -> bytes:
    payload = bytearray(CONFIG_SIZE)
    payload[0] = CONFIG_VERSION
    payload[1] = command & 0xFF
    crc = binascii.crc32(payload[: CONFIG_SIZE - 4]) & 0xFFFFFFFF
    payload[CONFIG_SIZE - 4 : CONFIG_SIZE] = struct.pack("<I", crc)
    return bytes([REPORT_ID_CONFIG]) + bytes(payload)


def open_config_device(path: str | None = None) -> hid.device:
    if path:
        dev = hid.device()
        dev.open_path(path.encode())
        return dev

    candidates = hid.enumerate(USB_VID, USB_PID)
    if not candidates:
        raise RuntimeError("HID Remapper device not found.")

    # The config interface is vendor-defined. Prefer it over the keyboard interface.
    candidates.sort(
        key=lambda d: (
            d.get("usage_page") != 0xFF00,
            d.get("interface_number", 99),
        )
    )

    last_error: Exception | None = None
    for info in candidates:
        dev = hid.device()
        try:
            dev.open_path(info["path"])
            return dev
        except OSError as exc:
            last_error = exc

    raise RuntimeError(f"Could not open HID Remapper device: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Trigger Left Windows/GUI on a custom HID Remapper build.")
    parser.add_argument("--path", help="Optional hidapi device path from hid.enumerate().")
    args = parser.parse_args()

    try:
        dev = open_config_device(args.path)
        dev.send_feature_report(build_command_report(COMMAND_TRIGGER_LEFT_GUI_PULSE))
        time.sleep(0.02)
        dev.close()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
