"""Prevent the v3 recovery candidate from changing v2's USB transport."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
expected = json.loads((root / "tests/v2-transport.json").read_text())
for name, digest in expected.items():
    data = (root / name).read_bytes().replace(b"\r\n", b"\n")
    assert hashlib.sha256(data).hexdigest() == digest, name
print(f"PASS {len(expected)} v2 USB startup/receive/submit/LED/driver files unchanged")
