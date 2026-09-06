"""Reject failed HID IN transfers without replaying the stale receive buffer."""
import hashlib
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "firmware/tinyusb/src/class/hid/hid_host.c"
text = source.read_text()
old = "    tuh_hid_report_received_cb(daddr, idx, p_hid->epin_buf, (uint16_t) xferred_bytes);"
new = """    // Failed transfers carry no valid keyboard state. Still notify the
    // application with length zero so its main-context receive retry runs.
    tuh_hid_report_received_cb(daddr, idx, p_hid->epin_buf,
                              result == XFER_RESULT_SUCCESS ? (uint16_t) xferred_bytes : 0);"""
if new not in text:
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest != "da271b9e44efc9b551e9d05f9bbfe0385ecde258e4579aa298a5c7dca3438550" or text.count(old) != 1:
        raise SystemExit("Unexpected TinyUSB source; expected pinned f7779351 hid_host.c")
    source.write_text(text.replace(old, new))
print("TinyUSB HID error guard verified")
