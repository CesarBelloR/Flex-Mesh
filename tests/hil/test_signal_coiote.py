"""End-to-end HIL tests for signal strength reaching Coiote (FW-1005 / FW-779).

Confirms on the cloud side what test_signal_freshness.py confirms on the
console: the value that lands in Connectivity Monitoring is a real measurement,
never one of the modem's placeholder readings.

  * **-140 dBm** is the modem's RSRP floor, not a weak signal. A usable RSRP is
    -130 dBm or higher. The driver used to store and publish it verbatim.
    (FW-779)
  * **0** is what the driver wrote whenever ``+QCSQ`` came back without a full
    measurement, and what the LwM2M engine serialises from its own
    zero-initialised statics when the whole /4 object is sent before a
    measurement exists. (FW-1005, introduced by 880607a)

read_cached reflects the value the device last *sent*; no Read is issued, so a
passing test proves the device pushed a sane value rather than that the server
could pull one.

Requirements:
  * the board flashed (debug build, no rtt.conf) and **LTE/cloud connected**
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)

Run:
  pytest tests/hil/test_signal_coiote.py -v -s \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import re
import time

import pytest

from coiote_client import CoioteError

# Data-model key for /4/0/2 by DDF resource name. If the tenant addresses the
# object by raw LwM2M path instead, switch this to "/4/0/2".
RSS_KEY = "Connectivity Monitoring.0.Radio Signal Strength"

# Mirrors RSRP_*_RANGE_VALUE in
# applications/etc-app/src/cloud/cloud_codec/data_codec_signal.h.
RSRP_MIN = -130
RSRP_MAX = -40

# Readings that must never appear as a published signal level.
PLACEHOLDER_RSRP = (0, -140)

SIGNAL_RE = re.compile(
    r"Signal report rsrp=(-?\d+) rsrq=(-?\d+) age_s=(-?\d+) valid=(\d)"
)

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)

SEND_TIMEOUT_S = 60
REGISTRATION_TIMEOUT_S = 120


def _grp(match, index):
    value = match.group(index)
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def _shell(dut, cmd):
    dut.write(f"{cmd}\r\n".encode())


def _nudge(dut):
    """Best-effort check-in prompt for a queue-mode device.

    Only provokes a registration update, so a dead console must not fail the
    test — the device checks in on its own schedule regardless. The USB console
    can drop while the device sleeps, so reopen it and carry on.
    """
    try:
        _shell(dut, "app_module trigger_tx")
        return
    except Exception:
        pass

    try:
        dut.reopen_console()
        _shell(dut, "app_module trigger_tx")
    except Exception:
        pass


def _trigger_and_read_device_signal(dut, attempts=3):
    """Provoke a send and return the measurement the device reported.

    The device may be in PSM when the test starts, which suspends the USB
    console, so a first attempt can find a silent port. Reopen and retry rather
    than reading that as the device having nothing to report.
    """
    for attempt in range(attempts):
        try:
            _shell(dut, "app_module trigger_tx")
            match = dut.expect(SIGNAL_RE, timeout=SEND_TIMEOUT_S)
            return {
                "rsrp": int(_grp(match, 1)),
                "age_s": int(_grp(match, 3)),
                "valid": _grp(match, 4) == "1",
            }
        except Exception:
            if attempt == attempts - 1:
                raise
            try:
                dut.reopen_console()
            except Exception:
                pass


def _require_online(coiote, device):
    last_contact = coiote.device_last_contact(device)
    if last_contact is None:
        pytest.skip(f"Device {device} not found in Coiote")
    age = datetime.datetime.now(datetime.timezone.utc) - last_contact
    if age > MAX_CONTACT_AGE:
        pytest.skip(f"Device {device} last contacted Coiote {age} ago (offline)")
    print(f"\nDevice {device} last contact {age} ago")


def _await_pushed_rss(coiote, dut, device, op_timeout, expected=None):
    """Poll the cached RSS, nudging the queue-mode device on each pass.

    Returns the last value seen, or the expected one as soon as it appears.
    """
    deadline = time.monotonic() + op_timeout
    value = None
    while time.monotonic() < deadline:
        _nudge(dut)
        time.sleep(20)
        try:
            value = coiote.read_cached(device, RSS_KEY)[0]
        except CoioteError:
            continue
        print(f"  [coiote] pushed RSS={value!r}")
        if expected is not None and str(value) == str(expected):
            return value
    return value


def test_connmon_rss_matches_device(coiote, dut, request):
    """The RSS Coiote holds must be the measurement the device reported."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    _require_online(coiote, device)

    report = _trigger_and_read_device_signal(dut)
    if not report["valid"]:
        pytest.skip("Device has no reportable measurement; nothing to compare")

    try:
        value = _await_pushed_rss(coiote, dut, device, op_timeout,
                                  expected=report["rsrp"])
    except CoioteError as exc:
        pytest.fail(f"Coiote read failed (key/dialect mismatch?): {exc}")

    assert value is not None, f"Coiote never reported {RSS_KEY}"
    rss = int(value)
    assert rss == report["rsrp"], (
        f"Coiote holds RSS {rss} but the device reported {report['rsrp']}")
    assert RSRP_MIN <= rss <= RSRP_MAX, (
        f"Published RSS {rss} outside the reportable range "
        f"[{RSRP_MIN}, {RSRP_MAX}]")


def test_registration_send_does_not_publish_invalid_rss(coiote, dut, request):
    """A reboot must not push a placeholder RSS on the registration send.

    The registration-change report adds the whole /4 object, so before the fix
    the LwM2M engine serialised its own zeroed RSS no matter what the
    application had set. The device now names the populated resources instead
    when it has no measurement to report.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    _require_online(coiote, device)

    dut.cold_boot()
    dut.expect(SIGNAL_RE, timeout=REGISTRATION_TIMEOUT_S)

    try:
        value = _await_pushed_rss(coiote, dut, device, op_timeout)
    except CoioteError as exc:
        pytest.fail(f"Coiote read failed (key/dialect mismatch?): {exc}")

    assert value is not None, f"Coiote never reported {RSS_KEY}"
    rss = int(value)
    assert rss not in PLACEHOLDER_RSRP, (
        f"Registration send published modem placeholder RSS {rss}; "
        f"0 is the FW-1005 regression and -140 is FW-779")
    assert RSRP_MIN <= rss <= RSRP_MAX, (
        f"Published RSS {rss} outside the reportable range "
        f"[{RSRP_MIN}, {RSRP_MAX}]")
