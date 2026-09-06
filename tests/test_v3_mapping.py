"""Exercise the real v3 parser and mapping engine; mock only board/USB I/O."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="feather-v3-") as folder:
    binary = Path(folder) / "mapping-test"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-Wno-format", "-Wno-narrowing", "-DPERSISTED_CONFIG_SIZE=4096",
        "-I", str(root / "firmware/src"), str(root / "tests/v3_mapping_test.cc"),
        *[str(root / "firmware/src" / f) for f in ["remapper.cc", "globals.cc", "descriptor_parser.cc"]],
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
