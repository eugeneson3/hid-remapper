"""Compile and exercise the actual firmware parser/mapping engine on the host."""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="feather-reliability-") as folder:
    binary = pathlib.Path(folder) / "test"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-Wno-format", "-Wno-narrowing",
        "-DPERSISTED_CONFIG_SIZE=4096", "-I", str(root / "firmware/src"),
        str(root / "tests/hid_reliability_test.cc"),
        *[str(root / "firmware/src" / f) for f in ["remapper.cc", "globals.cc", "descriptor_parser.cc"]],
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    led_binary = pathlib.Path(folder) / "led-test"
    subprocess.run([
        "g++", "-std=c++17", "-DFEATHER_HOST_BOARD", "-I", str(root / "tests/stubs"),
        "-I", str(root / "firmware/src"), str(root / "tests/activity_led_test.cc"),
        str(root / "firmware/src/activity_led.cc"), "-o", str(led_binary),
    ], check=True)
    subprocess.run([str(led_binary)], check=True)
