import argparse
import binascii
import re
import struct
import sys
import time

import hid


USB_VID = 0xCAFE
USB_PID = 0xBAF2
CONFIG_VERSION = 18
REPORT_ID_CONFIG = 100
REPORT_ID_MONITOR = 101
CONFIG_SIZE = 32
GPIO_USAGE_PAGE = 0xFFF40000

COMMAND_SET_MONITOR_ENABLED = 22
COMMAND_INJECT_KEY_DOWN = 26
COMMAND_INJECT_KEY_UP = 27
COMMAND_INJECT_CLEAR_KEYS = 28
CONFIG_USAGE = 0x20
MONITOR_USAGE = 0x21

KEY_NAME_TO_HID = {
    "a": 0x04,
    "b": 0x05,
    "c": 0x06,
    "d": 0x07,
    "e": 0x08,
    "f": 0x09,
    "g": 0x0A,
    "h": 0x0B,
    "i": 0x0C,
    "j": 0x0D,
    "k": 0x0E,
    "l": 0x0F,
    "m": 0x10,
    "n": 0x11,
    "o": 0x12,
    "p": 0x13,
    "q": 0x14,
    "r": 0x15,
    "s": 0x16,
    "t": 0x17,
    "u": 0x18,
    "v": 0x19,
    "w": 0x1A,
    "x": 0x1B,
    "y": 0x1C,
    "z": 0x1D,
    "1": 0x1E,
    "2": 0x1F,
    "3": 0x20,
    "4": 0x21,
    "5": 0x22,
    "6": 0x23,
    "7": 0x24,
    "8": 0x25,
    "9": 0x26,
    "0": 0x27,
    "enter": 0x28,
    "esc": 0x29,
    "escape": 0x29,
    "backspace": 0x2A,
    "tab": 0x2B,
    "space": 0x2C,
    "minus": 0x2D,
    "equal": 0x2E,
    "leftbracket": 0x2F,
    "rightbracket": 0x30,
    "backslash": 0x31,
    "semicolon": 0x33,
    "quote": 0x34,
    "grave": 0x35,
    "comma": 0x36,
    "dot": 0x37,
    "period": 0x37,
    "slash": 0x38,
    "capslock": 0x39,
    "printscreen": 0x46,
    "scrolllock": 0x47,
    "pause": 0x48,
    "insert": 0x49,
    "home": 0x4A,
    "pageup": 0x4B,
    "delete": 0x4C,
    "end": 0x4D,
    "pagedown": 0x4E,
    "right": 0x4F,
    "left": 0x50,
    "down": 0x51,
    "up": 0x52,
    "numlock": 0x53,
    "leftctrl": 0xE0,
    "leftshift": 0xE1,
    "leftalt": 0xE2,
    "leftgui": 0xE3,
    "win": 0xE3,
    "rightctrl": 0xE4,
    "rightshift": 0xE5,
    "rightalt": 0xE6,
    "rightgui": 0xE7,
}

for n in range(1, 13):
    KEY_NAME_TO_HID[f"f{n}"] = 0x3A + n - 1

for n in range(13, 25):
    KEY_NAME_TO_HID[f"f{n}"] = 0x68 + n - 13


def keyboard_usage(key: str) -> int:
    key = key.strip().lower().replace("_", "").replace("-", "")

    if key.startswith("0x"):
        value = int(key, 16)
    elif re.fullmatch(r"\d+", key):
        value = int(key)
    else:
        if key not in KEY_NAME_TO_HID:
            raise ValueError(f"unknown key: {key}")
        value = KEY_NAME_TO_HID[key]

    if value <= 0xFFFF:
        return 0x00070000 | value
    return value


def build_command_report(command: int, data: bytes = b"") -> bytes:
    if len(data) > 26:
        raise ValueError("command data is too long")

    payload = bytearray(CONFIG_SIZE)
    payload[0] = CONFIG_VERSION
    payload[1] = command & 0xFF
    payload[2 : 2 + len(data)] = data
    crc = binascii.crc32(payload[: CONFIG_SIZE - 4]) & 0xFFFFFFFF
    payload[CONFIG_SIZE - 4 : CONFIG_SIZE] = struct.pack("<I", crc)
    return bytes([REPORT_ID_CONFIG]) + bytes(payload)


def open_device(path: str | None = None, usage: int | None = None) -> hid.device:
    if path:
        dev = hid.device()
        dev.open_path(path.encode())
        return dev

    candidates = hid.enumerate(USB_VID, USB_PID)
    if not candidates:
        raise RuntimeError("HID Remapper device not found.")

    if usage is not None:
        candidates = [d for d in candidates if d.get("usage_page") == 0xFF00 and d.get("usage") == usage]
        if not candidates:
            raise RuntimeError(f"HID Remapper usage 0x{usage:02X} interface not found.")

    candidates.sort(key=lambda d: (d.get("usage_page") != 0xFF00, d.get("interface_number", 99), d.get("usage", 99)))

    last_error: Exception | None = None
    for info in candidates:
        dev = hid.device()
        try:
            dev.open_path(info["path"])
            return dev
        except OSError as exc:
            last_error = exc

    raise RuntimeError(f"Could not open HID Remapper device: {last_error}")


def open_config_device(path: str | None = None) -> hid.device:
    return open_device(path, CONFIG_USAGE)


def open_monitor_device(path: str | None = None) -> hid.device:
    return open_device(path, MONITOR_USAGE)


def send_command(dev: hid.device, command: int, data: bytes = b"") -> None:
    dev.send_feature_report(build_command_report(command, data))


def send_key(dev: hid.device, command: int, key: str) -> None:
    send_command(dev, command, struct.pack("<I", keyboard_usage(key)))


def iter_monitor_reports(dev: hid.device):
    while True:
        report = bytes(dev.read(64, 1000))
        if not report:
            continue
        if report[0] != REPORT_ID_MONITOR:
            continue
        for idx in range(7):
            offset = 1 + idx * 9
            usage, value, hub_port = struct.unpack_from("<IiB", report, offset)
            if usage != 0:
                yield usage, value, hub_port


def main() -> int:
    parser = argparse.ArgumentParser(description="Custom HID Remapper key injection and GPIO monitor helper.")
    parser.add_argument("--path", help="Optional hidapi device path from hid.enumerate().")

    subparsers = parser.add_subparsers(dest="command", required=True)

    down_parser = subparsers.add_parser("key-down")
    down_parser.add_argument("key")

    up_parser = subparsers.add_parser("key-up")
    up_parser.add_argument("key")

    press_parser = subparsers.add_parser("press")
    press_parser.add_argument("key")
    press_parser.add_argument("--duration", type=float, default=0.05)

    subparsers.add_parser("clear")
    subparsers.add_parser("listen-gpio5")

    args = parser.parse_args()

    try:
        if args.command == "key-down":
            dev = open_config_device(args.path)
            send_key(dev, COMMAND_INJECT_KEY_DOWN, args.key)
            dev.close()
        elif args.command == "key-up":
            dev = open_config_device(args.path)
            send_key(dev, COMMAND_INJECT_KEY_UP, args.key)
            dev.close()
        elif args.command == "press":
            dev = open_config_device(args.path)
            send_key(dev, COMMAND_INJECT_KEY_DOWN, args.key)
            time.sleep(max(0.0, args.duration))
            send_key(dev, COMMAND_INJECT_KEY_UP, args.key)
            dev.close()
        elif args.command == "clear":
            dev = open_config_device(args.path)
            send_command(dev, COMMAND_INJECT_CLEAR_KEYS)
            dev.close()
        elif args.command == "listen-gpio5":
            config_dev = open_config_device(args.path)
            send_command(config_dev, COMMAND_SET_MONITOR_ENABLED, b"\x01")
            config_dev.close()

            dev = open_monitor_device()
            print("listening for GPIO5 falling edge: usage=0xFFF40005 value=1", flush=True)
            for usage, value, hub_port in iter_monitor_reports(dev):
                if usage == (GPIO_USAGE_PAGE | 5) and value == 1:
                    print("GPIO5_FALLING", flush=True)
                else:
                    print(f"usage=0x{usage:08X} value={value} hub_port={hub_port}", flush=True)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
