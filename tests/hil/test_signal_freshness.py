"""HIL tests for FW-1005 / FW-779: cellular signal strength reporting.

The device used to publish whatever ``AT+QCSQ`` last returned, with no validity
or age check. Two placeholder readings therefore reached the cloud as if they
were signal levels:

  * **-140 dBm** - the modem's RSRP floor, returned when it has no usable
    measurement. A usable RSRP is -130 dBm or higher. (FW-779)
  * **0** - the parser only read RSRP/RSRQ from a full five-argument reply but
    stored the locals unconditionally, so a ``+QCSQ: "NOSERVICE"`` reply
    overwrote a good measurement with zero. Since FW-903 the device can also
    register and send before QCSQ has ever run. (FW-1005)

The firmware now keeps the last complete, in-range measurement with the uptime
at which it was taken, reports it only when it is no older than six hours, and
omits the Connectivity Monitoring resources entirely when it has nothing valid
to say.

Every wait anchors on a log sentinel and returns the instant it appears; the
timeouts only cap how long we wait before declaring a regression. Application
logs are interleaved on the same console, so reads anchor on the sentinel's own
text rather than on the shell prompt.

Sentinel:

  ``Signal report rsrp=<n> rsrq=<n> age_s=<n> valid=<0|1>``
      emitted by data_module.c each time a send snapshots the measurement.

Build with the shell log backend on (no rtt.conf):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Run:
  pytest tests/hil/test_signal_freshness.py -v -s
"""

import re

# Reportable ranges, mirroring RSRP_*_RANGE_VALUE / RSRQ_*_RANGE_VALUE in
# applications/etc-app/src/cloud/cloud_codec/data_codec_signal.h.
RSRP_MIN = -130
RSRP_MAX = -40
RSRQ_MIN = -34
RSRQ_MAX = -1

# Readings the modem emits when it has nothing to report. Neither may ever be
# published as a signal level.
PLACEHOLDER_RSRP = (0, -140)

SIGNAL_RE = re.compile(
    r"Signal report rsrp=(-?\d+) rsrq=(-?\d+) age_s=(-?\d+) valid=(\d)"
)

# Bounded fallbacks; each expect returns as soon as its sentinel lands.
SEND_TIMEOUT_S = 60
REGISTRATION_TIMEOUT_S = 120
PSM_TIMEOUT_S = 300

# The old cadence was a flat 30 s poll, so a measurement older than that at the
# first send would mean the re-poll is not working.
FRESH_AT_FIRST_SEND_S = 30

# Sends to sample in the repeated-transmit test.
SEND_COUNT = 5


def _grp(match, index):
    value = match.group(index)
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def _expect_signal(dut, timeout=SEND_TIMEOUT_S):
    """Wait for the next signal-report sentinel and return it parsed."""
    match = dut.expect(SIGNAL_RE, timeout=timeout)
    return {
        "rsrp": int(_grp(match, 1)),
        "rsrq": int(_grp(match, 2)),
        "age_s": int(_grp(match, 3)),
        "valid": _grp(match, 4) == "1",
    }


def _assert_reportable(report):
    """A report marked valid must carry a measurement, never a placeholder."""
    if not report["valid"]:
        return
    assert report["rsrp"] not in PLACEHOLDER_RSRP, (
        f"published modem placeholder RSRP {report['rsrp']} as a signal level"
    )
    assert RSRP_MIN <= report["rsrp"] <= RSRP_MAX, (
        f"RSRP {report['rsrp']} outside the reportable range "
        f"[{RSRP_MIN}, {RSRP_MAX}]"
    )
    assert RSRQ_MIN <= report["rsrq"] <= RSRQ_MAX, (
        f"RSRQ {report['rsrq']} outside the reportable range "
        f"[{RSRQ_MIN}, {RSRQ_MAX}]"
    )


def test_no_invalid_signal_on_any_send(dut):
    """No send may ever report a placeholder reading as a valid measurement."""
    reports = []
    for _ in range(SEND_COUNT):
        dut.write(b'app_module trigger_tx\r\n')
        report = _expect_signal(dut)
        _assert_reportable(report)
        reports.append(report)

    assert any(r["valid"] for r in reports), (
        "no send reported a valid measurement; the device may not be "
        "registered, so this run proves nothing"
    )


def test_first_send_after_reboot_is_valid(dut):
    """The first send after boot must not publish an uninitialised reading.

    This is the FW-1005 race: since FW-903 the device can register and send
    before QCSQ has populated a measurement.
    """
    dut.cold_boot()
    report = _expect_signal(dut, timeout=REGISTRATION_TIMEOUT_S)
    _assert_reportable(report)
    assert report["valid"], (
        "first send after boot reported no usable measurement")


def test_measurement_is_fresh_by_the_first_send(dut):
    """The first send after boot must carry a recently taken measurement.

    The driver re-polls every 3 s until a usable measurement exists, so by the
    time the device first sends it should be reporting one taken seconds ago
    rather than one up to a full 30 s poll period old.

    Asserted on the reported age rather than by timing the QCSQ log line: the
    first measurements are taken during registration, before the boot sentinel
    this test waits on, so the *next* QCSQ line would only show the steady-state
    cadence.
    """
    dut.cold_boot()
    report = _expect_signal(dut, timeout=REGISTRATION_TIMEOUT_S)

    _assert_reportable(report)
    assert report["valid"], "first send after boot reported no usable measurement"
    assert report["age_s"] <= FRESH_AT_FIRST_SEND_S, (
        f"first send reported a measurement {report['age_s']}s old; the re-poll "
        f"should keep it under {FRESH_AT_FIRST_SEND_S}s"
    )


def test_signal_survives_psm(dut):
    """A measurement taken before PSM must still be reportable after wakeup.

    The driver used to invalidate its reading on PSM power-down, leaving the
    device with nothing to report until the next poll. It now keeps the
    measurement and lets the age decide.

    The age is deliberately not asserted: the wakeup re-poll often lands a
    fresh measurement first, so either a cached or a newly taken one is a pass.
    What must never happen is having none at all.
    """
    dut.expect("Entering PSM", timeout=PSM_TIMEOUT_S)
    dut.write(b'app_module trigger_tx\r\n')
    report = _expect_signal(dut)

    _assert_reportable(report)
    assert report["valid"], "no measurement was reportable after the PSM cycle"
