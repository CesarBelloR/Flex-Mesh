"""Shared helpers for HIL tests that drive a sensor acquisition and read the
result out of the device log.

Firmware maps splitter A branches to inputs 1..4 and B branches to inputs 5..8,
so input 7 is what the portal shows as port 3.B.

Waits are driven by the device's log output, not fixed wall-clock windows, so a
passing run finishes as soon as the line lands; timeouts only bound a regression
so it fails fast.
"""

import re
import time

import pytest

# One line per acquisition, emitted at the end of etc_sensor_run_detection():
#   detect: splitter 1100 type 1100/1100
# Four splitter-present flags, then enum sensor_type for inputs 1..4 and 5..8
# (0 = UNDEF/absent, 1 = ANALOG, 2 = DIGITAL).
DETECT_RE = re.compile(r"detect: splitter (\d{4}) type (\d{4})/(\d{4})")
# Companion line carrying the 1-Wire family code of the splitter found on each
# port, 00 where none answered:
#   detect: parts 26/27/00/00
PARTS_RE = re.compile(r"detect: parts ([0-9a-f]{2})/([0-9a-f]{2})/([0-9a-f]{2})/([0-9a-f]{2})")
# Raw calibrated count per sampled channel, 0-based: ADC[2] is input 3, ADC[6]
# is input 7 (port 3.B). Absent inputs emit no line at all.
ADC_RE = re.compile(r"ADC\[(\d)\] (-?\d+)")
# One line per acquisition, emitted at the end of etc_sensor_run_digital_sample()
# after the analog pass:
#   digital: humid 45.3 port 0 ft 0
# %RH from the one supported RH probe (-1.0 when none was read), the 0-based
# input it sits on (-1 when none), and whether the functional-test jig was
# detected (every port grounded).
DIGITAL_RE = re.compile(r"digital: humid (-?\d+\.\d) port (-?\d+) ft (\d)")
HUMID_ABSENT = -1.0

TYPE_ABSENT = "0"

# 1-Wire ROM family codes of the supported splitter temperature sensors, and the
# value the firmware reports for a port where nothing answered.
FAMILY_TMP1826 = "26"
FAMILY_TMP1827 = "27"
FAMILY_NONE = "00"

# The rail enforces a 2 s minimum off-time between acquisitions, so a sample
# requested too soon after the previous one is skipped rather than queued.
RAIL_SETTLE_S = 2.5
DETECT_TIMEOUT_S = 25


def shell(dut, cmd):
    dut.write(f"{cmd}\r\n".encode())


def _drain(dut, quiet_s=0.3):
    """Discard everything already buffered from the device."""
    try:
        while True:
            dut.expect(r".+", timeout=quiet_s)
    except Exception:
        pass


def collect_acquisition(dut, timeout):
    """Read serial output until one acquisition has fully reported.

    Returns (detect_match, parts_match, {channel: raw_adc}). The detection line
    is emitted before the analog pass, so keep reading past it until the log
    goes quiet to catch the per-channel ADC lines that follow."""
    deadline = time.time() + timeout
    buf = ""
    detect = None
    quiet_until = None
    while time.time() < deadline:
        try:
            m = dut.expect(r".+", timeout=0.5)
            s = m.group(0)
            if isinstance(s, bytes):
                s = s.decode("utf-8", "replace")
            buf += s + "\n"
            if detect is None:
                detect = DETECT_RE.search(buf)
                if detect is not None:
                    # Give the analog pass room to finish reporting.
                    quiet_until = time.time() + 6
            elif time.time() > quiet_until:
                break
        except Exception:
            if detect is not None and time.time() > quiet_until:
                break
    adc = {}
    parts = None
    if detect is not None:
        parts = PARTS_RE.search(buf[detect.end():])
        for chan, raw in ADC_RE.findall(buf[detect.end():]):
            adc[int(chan)] = int(raw)
    return detect, parts, adc


def acquire_parts(dut):
    """Trigger one acquisition and return (splitters, types, adc, parts).

    `splitters` and `types` are the raw digit strings from the detection line;
    `types` is indexed 0..7 for inputs 1..8. `parts` is the per-port family code
    as a 4-tuple of two-character strings."""
    for attempt in range(3):
        time.sleep(RAIL_SETTLE_S)
        shell(dut, "magnet sample")
        detect, parts, adc = collect_acquisition(dut, DETECT_TIMEOUT_S)
        if detect is not None:
            families = parts.groups() if parts is not None else (FAMILY_NONE,) * 4
            return detect.group(1), detect.group(2) + detect.group(3), adc, families
    pytest.fail("no detection line after 3 acquisition requests; is the firmware "
                "built with overlay-hil.conf and the shell log backend enabled?")


def acquire(dut):
    """Trigger one acquisition and return (splitters, types, adc)."""
    splitters, types, adc, _ = acquire_parts(dut)
    return splitters, types, adc


def assert_absent(types, adc, input_no):
    """Input `input_no` (1-based) reports nothing, and was not even sampled."""
    idx = input_no - 1
    assert types[idx] == TYPE_ABSENT, (
        f"input {input_no} classified as type {types[idx]}, expected absent")
    assert idx not in adc, (
        f"input {input_no} was sampled (ADC[{idx}] = {adc[idx]}) despite having "
        f"no probe behind it")


def assert_present(types, adc, input_no):
    idx = input_no - 1
    assert types[idx] != TYPE_ABSENT, f"input {input_no} absent, expected a reading"
    assert idx in adc, f"input {input_no} classified but never sampled"


def acquire_digital(dut):
    """Trigger one acquisition and return (humidity, input_index, functional_test)
    from the digital pass. `input_index` is 0-based, -1 when no RH probe was
    read; `humidity` is HUMID_ABSENT in that case."""
    for attempt in range(3):
        time.sleep(RAIL_SETTLE_S)
        # Anchor on this request's own echo so a `digital:` line left in the
        # buffer by an earlier acquisition cannot satisfy the match.
        _drain(dut)
        shell(dut, "magnet sample")
        try:
            dut.expect(re.escape("magnet sample"), timeout=10)
            m = dut.expect(DIGITAL_RE, timeout=DETECT_TIMEOUT_S)
        except Exception:
            continue
        groups = [g.decode("utf-8", "replace") if isinstance(g, bytes) else g
                  for g in m.groups()]
        return float(groups[0]), int(groups[1]), groups[2] == "1"
    pytest.fail("no digital line after 3 acquisition requests; is the firmware "
                "built with overlay-hil.conf and the shell log backend enabled?")
