"""Operator-attended HIL test for splitter detection on real hardware.

Covers FW-1071: a port with no splitter must not publish a B branch. The bug was
that the B branch was classified once and never cleared, so a port kept
reporting inputs 5..8 after its splitter was removed - and because the branch
switch failed silently, the value it reported was the A branch probe measured a
second time.

Firmware maps splitter A branches to inputs 1..4 and B branches to inputs 5..8,
so input 7 is what the portal shows as port 3.B.

Run with `-s` so the operator prompts are visible and stdin works:

    pytest tests/hil/test_splitter_detection.py -v -s

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

**The order of the tests in this file is load-bearing.** pytest collects in
definition order, and each test leaves the hardware in the state the next one
starts from. The regression in particular depends on the device NOT being
rebooted between the splitter being present and being removed: a reboot clears
the stale state and hides the bug, which is why the field only saw it after a
probe swap.

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
# Raw calibrated count per sampled channel, 0-based: ADC[2] is input 3, ADC[6]
# is input 7 (port 3.B). Absent inputs emit no line at all.
ADC_RE = re.compile(r"ADC\[(\d)\] (-?\d+)")

TYPE_ABSENT = "0"

# The rail enforces a 2 s minimum off-time between acquisitions, so a sample
# requested too soon after the previous one is skipped rather than queued.
RAIL_SETTLE_S = 2.5
DETECT_TIMEOUT_S = 25


def _shell(dut, cmd):
    dut.write(f"{cmd}\r\n".encode())


def _collect_acquisition(dut, timeout):
    """Read serial output until one acquisition has fully reported.

    Returns (detect_match, {channel: raw_adc}). The detection line is emitted
    before the analog pass, so keep reading past it until the log goes quiet to
    catch the per-channel ADC lines that follow."""
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
    if detect is not None:
        for chan, raw in ADC_RE.findall(buf[detect.end():]):
            adc[int(chan)] = int(raw)
    return detect, adc


def _acquire(dut):
    """Trigger one sensor acquisition and return (splitters, types, adc).

    `splitters` and `types` are the raw digit strings from the detection line;
    `types` is indexed 0..7 for inputs 1..8."""
    for attempt in range(3):
        time.sleep(RAIL_SETTLE_S)
        _shell(dut, "magnet sample")
        detect, adc = _collect_acquisition(dut, DETECT_TIMEOUT_S)
        if detect is not None:
            return detect.group(1), detect.group(2) + detect.group(3), adc
    pytest.fail("no detection line after 3 acquisition requests; is the firmware "
                "built with overlay-hil.conf and the shell log backend enabled?")


def _assert_absent(types, adc, input_no):
    """Input `input_no` (1-based) reports nothing, and was not even sampled."""
    idx = input_no - 1
    assert types[idx] == TYPE_ABSENT, (
        f"input {input_no} classified as type {types[idx]}, expected absent")
    assert idx not in adc, (
        f"input {input_no} was sampled (ADC[{idx}] = {adc[idx]}) despite having "
        f"no probe behind it")


def _assert_present(types, adc, input_no):
    idx = input_no - 1
    assert types[idx] != TYPE_ABSENT, f"input {input_no} absent, expected a reading"
    assert idx in adc, f"input {input_no} classified but never sampled"


def test_probes_without_splitters_have_no_b_branch(dut, operator):
    """The FW-1071 configuration: splitters on ports 1 and 2, plain probes wired
    directly to ports 3 and 4. Ports 3 and 4 have no B branch to report."""
    operator.prompt("""
        Wire the device as follows, then power-cycle it:
          - port 1: splitter, both branches populated
          - port 2: splitter, both branches populated
          - port 3: plain probe, no splitter
          - port 4: plain probe, no splitter
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters == "1100", (
        f"splitters detected on {splitters}, expected 1100 (ports 1 and 2 only)")
    for input_no in (1, 2, 3, 4, 5, 6):
        _assert_present(types, adc, input_no)
    _assert_absent(types, adc, 7)
    _assert_absent(types, adc, 8)


def test_splitter_on_port_three_reports_a_distinct_b_branch(dut, operator):
    """Port 3 gains a splitter. Its B branch must appear, and must be its own
    measurement rather than a repeat of the A branch."""
    operator.prompt("""
        Move a splitter onto port 3, both branches populated, and put probes at
        clearly different temperatures on its two branches (e.g. hold one in
        your hand). Leave ports 1, 2 and 4 as they are.

        Do NOT power-cycle or reset the device.
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters[2] == "1", f"no splitter detected on port 3 (mask {splitters})"
    _assert_present(types, adc, 3)
    _assert_present(types, adc, 7)
    assert adc[2] != adc[6], (
        f"port 3.A and 3.B both read {adc[2]}; the branch switch is not taking "
        f"effect and the same probe is being measured twice")


def test_removing_a_splitter_clears_its_b_branch(dut, operator):
    """The regression. Before the fix, port 3 kept reporting a B branch after
    its splitter was removed, carrying a duplicate of the port 3 probe."""
    operator.prompt("""
        Remove the splitter from port 3 and wire a plain probe there instead.
        Leave ports 1, 2 and 4 as they are.

        Do NOT power-cycle or reset the device - a reboot clears the stale state
        this test is looking for.
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters[2] == "0", (
        f"port 3 still reports a splitter (mask {splitters}) after it was removed")
    _assert_present(types, adc, 3)
    _assert_absent(types, adc, 7)
