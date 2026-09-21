"""Cloud HIL tests for threshold-triggered immediate uploads (FW-1179).

FW-1178 put the EXACT Threshold object (48944) on the device; this file covers
what the device *does* with it: a reading beyond an enabled slot must wake the
radio and push the current sample ahead of schedule, carrying the Alert flag of
the slot that breached and the EXACT Info Priority flag
(docs/specs/lwm2m-immediate-report-thresholds.md, sections 5 and 6).

The readings are driven, not the thresholds (FW-1234). ``sensor_sim set``
substitutes a value into the sample after the physical getters and before the
sensor event is submitted, so the two slots are configured once at fixed values
and every case moves the *reading* across them, the way a probe would. That
reaches three things a threshold moved around a live probe cannot: Drops Below,
the re-arm an invalid reading causes, and a run with no probe attached at all.
Every port is driven, unsimulated ports included, so a warm probe left on the
bench cannot cross a slot behind the test's back.

Covered here:

  * a reading below an Exceeds slot does not cross it,
  * a reading driven above it triggers on the next sample, dispatches an upload,
    and the Send reaches Coiote with ``/48944/0/5`` and ``/48933/0/7`` true while
    the device's own read-back shows the alert cleared and the slot counted,
  * a second crossing inside the hold-off is flagged only (``upload 0``) and is
    still counted,
  * a crossing past the hold-off transmits again,
  * a Drops Below slot on humidity triggers when the reading falls through it,
  * an unplugged probe (``sensor_sim set in1 nc``) drops the port's state, so the
    next reading beyond the threshold crosses again without ever having returned
    below it,
  * a reading that stays beyond the slot does not fire again (edge-triggered),
  * rewriting the Threshold Value re-arms the slot without the reading moving,
  * a crossing found on an unscheduled RTC log wake defers its dispatch by the
    per-device transmit delay,
  * no later report carries the Priority flag again.

**The order of the tests in this file is load-bearing.** The first test arms
both slots and leaves them enabled for the rest; the hold-off tests chain their
own crossings; the re-arm case starts from the latched state the edge-triggered
case leaves behind; and the last test compares against the Priority flag the
Exceeds case recorded. Running a test on its own is fine only for the first one.

The log-wake case is the one that costs real time: it waits out the hold-off,
then up to two shortened log intervals for the device's own RTC wake.

The hold-off is shortened to 60 s from Coiote for the whole file (FW-1225,
``/48931/0/16``) and restored to 900 s at the end, so the rate-limit cases cost
a minute rather than a quarter of an hour.

The device is never rebooted: FW-1221 makes a cold boot fail an assert in the
app module, and the trigger state this file exercises is RAM-only anyway, so a
reboot would erase exactly what is under test.

Timing. The DUT is a queue-mode (PSM/eDRX) cellular device, so every configure
and read task is delivered on a registration update; each is nudged along with
``app_module trigger_tx`` and can still take minutes. The immediate upload adds
an LTE bring-up, and the log-wake case waits for an RTC alarm. Budget ~25 min
for the file.

Requirements:
  * the board flashed (HIL debug build, no rtt.conf) and **LTE/cloud connected**
  * no probe needed: every reading is simulated
  * ``--port`` (the sample requests and the log assertions go over the console)
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the tests skip)
  * the 48944 DDF uploaded to the Coiote tenant data model

Build:
  west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
    -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Run:
  pytest tests/hil/test_threshold_trigger_coiote.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_9AB22C8BA162847D-if00 \
    --coiote-device urn:dev:mac:9AB22C8BA162847D \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import re
import time

import pytest

# The rail settle time and the shell writer every console-driven HIL test uses.
from acquisition import RAIL_SETTLE_S, shell
from coiote_client import CoioteError
# The FW-1178 file owns the 48944 data-model keys and the queue-mode task
# plumbing (nudge, drive, cached read-back gated on the task's start time).
# Importing them keeps both files addressing the object the same way, so a
# tenant dialect change is a one-line fix there rather than two.
from test_threshold_coiote import (ALERT_TYPE_DROPS_BELOW, ALERT_TYPE_EXCEEDS,
                                   R_ALERT, R_ALERT_TYPE, R_ENABLED,
                                   R_THRESHOLD_VALUE, R_TRIGGER_COUNT,
                                   R_VALUE_TYPE, VALUE_TYPE_HUMIDITY,
                                   VALUE_TYPE_TEMPERATURE, _as_bool, _as_int,
                                   _disable_slots,
                                   _drive_task, _key, _read_keys,
                                   _require_online, _write_ops,
                                   _write_threshold_value)
# Shell-side helpers: the `magnet status` parser and the log-safe expect used by
# every console-driven HIL test.
from test_upload_reason import (_drain, _expect_re, _get_setting, _grp,
                                _set_and_restore, _status, _wait_dispatches)

# Slot 0 watches temperature from above, slot 1 humidity from below. Slots 2 and
# 3 are left alone so this file can share a device with the FW-1178 tests.
SLOT_EXCEEDS = 0
SLOT_DROPS_BELOW = 1
TOUCHED_SLOTS = (SLOT_EXCEEDS, SLOT_DROPS_BELOW)

# The two thresholds. Both are written once; only the re-arm case rewrites the
# temperature one, to a value still between the two driven readings so the
# rewrite re-arms the slot without moving any reading to its other side.
TEMP_THRESHOLD_C = 25.0
TEMP_REARM_THRESHOLD_C = 24.0
HUMID_THRESHOLD_PCT = 40.0

# Driven readings, in the milli-units `sensor_sim set` takes. The temperatures
# straddle TEMP_THRESHOLD_C and the humidities HUMID_THRESHOLD_PCT, both with
# five units of margin so a rounding difference cannot decide a case.
TEMP_BELOW_MC = 20000
TEMP_ABOVE_MC = 30000
HUMID_ABOVE_MPCT = 50000
HUMID_BELOW_MPCT = 30000

# The temperature input the cases drive; the others are held unplugged.
DRIVEN_INPUT = "in1"
DRIVEN_INPUT_BIT = 1 << 0  # enum sensor_input: SENSOR_INPUT_IN1

# The hold-off this file writes over the device's 900 s default
# (CONFIG_ETC_APP_THRESHOLD_MIN_REPORT_INTERVAL_S), and the value it restores.
# FW-1225 added it as /48931/0/16 of EXACT Configuration. It is addressed by
# data-model key like every other resource these tests write: the tenant rejects
# raw LwM2M paths outright ("Failed to parse path"), DDF or no DDF.
THRESHOLD_REPORT_INTERVAL_KEY = "EXACT Configuration.0.THRESHOLD_REPORT_INTERVAL"
HOLD_OFF_S = 60
DEFAULT_HOLD_OFF_S = 900
# Margin on top of the hold-off before a crossing that must be granted an
# upload, so clock skew between the device's uptime and the host cannot leave it
# inside the window.
HOLD_OFF_GUARD_S = 30

SHELL_ECHO_TIMEOUT_S = 10
# A sample completes in ~3 s; these only bound a regression.
CROSSING_TIMEOUT_S = 60
# A crossing shows up within ~3 s of the sample; this window is the proof of
# absence for the cases where "nothing crossed" is the claim.
NO_CROSSING_WINDOW_S = 30
# The same proof where it only re-establishes a baseline a passing case has
# already covered, or has to fit inside the 60 s hold-off.
SHORT_QUIET_S = 8
# Unscheduled dispatch = the transmit delay (0..29.5 s) plus margin.
DISPATCH_MARGIN_S = 45
# LTE bring-up, Send, and Coiote ingesting it.
UPLOAD_TIMEOUT_S = 420
# Both flags ride the same Send, so once one is in the cached model the other is
# too; this only absorbs the portal applying them a moment apart.
SAME_SEND_TIMEOUT_S = 60

# The shortest log interval the settings layer accepts
# (ETC_SETTING_LOG_INTERVAL_SECS_MIN in etc_settings.h).
LOG_INTERVAL_MIN_S = 60
# Two intervals plus margin: the shortened interval only takes effect at the
# next alarm the device programs, so the first wake can still be a long one.
LOG_WAKE_TIMEOUT_S = 2 * LOG_INTERVAL_MIN_S + 60
# The deferred line follows the crossing within the same sample.
DEFERRED_TIMEOUT_S = 15

# One line per sample from the data module's threshold evaluation: the bitmask
# of slots that crossed, and whether an immediate upload was requested (0 when
# the hold-off suppressed it).
CROSSED_RE = re.compile(r"Threshold crossed: slots 0x([0-9a-fA-F]{2}) upload ([01])")
# The app module's delayed-dispatch line, emitted only when a threshold request
# is the sole reason a LOG-job sample has to transmit.
DEFERRED_RE = re.compile(r"Threshold upload deferred by (\d+) ms")
# FW-1234: the sensor module logs the simulated inputs once per sample. Its
# presence is what tells the run the injected readings actually reached the
# sample rather than being swallowed by a stale build.
SIM_MASK_RE = re.compile(r"Sensor sim active mask 0x([0-9a-fA-F]{3})")

# Read-only status resource of a slot, spec section 3 (RID 6). The FW-1178 file
# has no constant for it; it never reads one.
R_LAST_TRIGGERED = "Last Triggered"

# EXACT Info Priority (48933/0/7): "process this report now". A threshold report
# carries it beside the slot's Alert, so Portal handles it like a magnet-swipe
# reading instead of holding it for the periodic batch (spec section 6).
PRIORITY_KEY = "EXACT Info.0.Priority"

# Monotonic timestamp of the most recent crossing that was granted an upload —
# the point the device restarts the hold-off from. None until the first one.
_last_upload_s = None
# True once the Exceeds test has seen Priority on the triggered report, so the
# last test knows the file ran in order.
_priority_seen = False


# --------------------------------------------------------------------------- #
# Session state: arm once, clean up however the run ends
# --------------------------------------------------------------------------- #
_session = {"finalized": False, "cleaned": False}


def _queue_disable(coiote, device):
    """Queue Enabled=false for both slots without waiting for delivery.

    The safety net for a run that stops before the last test (``-x``, a fixture
    error, Ctrl-C): the ``dut`` fixture closes the console at the end of every
    test, so there is nothing left to nudge with, but a queued task still lands
    at the device's next check-in.
    """
    if _session["cleaned"]:
        return
    try:
        coiote.configure_task(
            device,
            [{"write": {"key": _key(slot, R_ENABLED), "value": "false"}}
             for slot in TOUCHED_SLOTS]
            + [{"write": {"key": THRESHOLD_REPORT_INTERVAL_KEY,
                          "value": str(DEFAULT_HOLD_OFF_S)}}],
            name="hil-threshold-trigger-cleanup")
        print(f"  [cleanup] queued disable of slots {list(TOUCHED_SLOTS)} and the "
              f"{DEFAULT_HOLD_OFF_S}s hold-off")
    except Exception as exc:  # cleanup must never fail the run
        print(f"  [cleanup] could not queue the disable: {exc}")


def _is_last_in_file(request):
    """True when this test is the last one collected from this file."""
    path = request.node.nodeid.split("::")[0]
    mine = [item for item in request.session.items
            if item.nodeid.split("::")[0] == path]
    return bool(mine) and mine[-1].nodeid == request.node.nodeid


@pytest.fixture(autouse=True)
def threshold_slots_disabled(coiote, dut, request):
    """Leave the slots disabled and nothing simulated however the run ends.

    Between tests the quiet baseline stays driven: the slots are still enabled,
    so a device-initiated log or TX wake in that gap would otherwise be
    evaluated against whatever is physically plugged into the bench.

    Autouse, so a run without Coiote credentials skips the whole file rather
    than half-configuring the device.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    if not _session["finalized"]:
        _session["finalized"] = True
        request.session.addfinalizer(lambda: _queue_disable(coiote, device))

    yield

    # The slots stay enabled between tests, so the device's own log and TX wakes
    # keep evaluating samples in the gap. Leave the quiet baseline driven rather
    # than releasing the ports back to whatever is plugged into the bench, and
    # only stop simulating once the slots are about to be disabled.
    if _is_last_in_file(request):
        _sim_clear(dut)
    else:
        _sim_baseline(dut)

    # The console is still open here, so this pass can nudge the writes along
    # and confirm they landed; the session finalizer above only covers an early
    # exit.
    if _is_last_in_file(request):
        try:
            _write_hold_off(coiote, dut, device, DEFAULT_HOLD_OFF_S, op_timeout)
        except Exception as exc:  # never mask a failure under test
            print(f"  [cleanup] WARNING: the hold-off is still {HOLD_OFF_S}s, "
                  f"restore it by hand: {exc}")
        _disable_slots(coiote, dut, device, TOUCHED_SLOTS, op_timeout)
        _session["cleaned"] = True


# --------------------------------------------------------------------------- #
# Console helpers
# --------------------------------------------------------------------------- #

def _shell_echo(dut, cmd):
    """Send a shell command and wait for its own echo, so later reads line up."""
    shell(dut, cmd)
    _expect_re(dut, re.escape(cmd), timeout=SHELL_ECHO_TIMEOUT_S,
               what=f"`{cmd}` echo")


def _sim_set(dut, name, value):
    """Drive one input. ``value`` is milli-units, or ``'nc'`` for unplugged."""
    _shell_echo(dut, f"sensor_sim set {name} {value}")


def _sim_clear(dut):
    """Stop simulating every input."""
    _shell_echo(dut, "sensor_sim clear")


def _sim_baseline(dut):
    """Drive every input to a reading on the quiet side of both thresholds.

    The seven ports the cases do not drive are held unplugged rather than left
    to their probes, so only the driven port can ever cross the temperature
    slot.
    """
    _sim_set(dut, DRIVEN_INPUT, TEMP_BELOW_MC)
    for port in range(2, 9):
        _sim_set(dut, f"in{port}", "nc")
    _sim_set(dut, "humid", HUMID_ABOVE_MPCT)


def _request_sample(dut):
    """Ask for one acquisition, anchored on this request's own echo.

    Deliberately does not drain first: a caller waiting for a crossing may have
    a log line of interest already buffered.
    """
    time.sleep(RAIL_SETTLE_S)
    _shell_echo(dut, "magnet sample")


def _sample_and_expect_crossing(dut, slots_expected, what):
    """Take a sample, require exactly ``slots_expected`` to cross, return upload."""
    global _last_upload_s

    _drain(dut, 1)
    _request_sample(dut)
    match = _expect_re(dut, CROSSED_RE, timeout=CROSSING_TIMEOUT_S,
                       what=f"`Threshold crossed` line for {what}")
    slots, upload = int(_grp(match, 1), 16), int(_grp(match, 2))
    print(f"  [dut] {what}: slots 0x{slots:02x} upload {upload}")
    assert slots == slots_expected, (
        f"Expected mask 0x{slots_expected:02x} to cross on {what}, got 0x{slots:02x}")
    if upload == 1:
        _last_upload_s = time.monotonic()
    return upload


def _sample_and_expect_quiet(dut, what, window=NO_CROSSING_WINDOW_S):
    """Take a sample and fail if any slot crosses within ``window`` seconds."""
    _drain(dut, 1)
    _request_sample(dut)
    try:
        match = dut.expect(CROSSED_RE, timeout=window)
    except Exception:
        print(f"  [dut] no crossing on {what}, as expected")
        return
    pytest.fail(f"Unexpected threshold crossing on {what}: slots 0x{_grp(match, 1)} "
                f"upload {_grp(match, 2)}")


def _assert_sim_reached_the_sample(dut):
    """Fail early when the image on the board has no `sensor_sim` (FW-1234)."""
    _drain(dut, 1)
    _request_sample(dut)
    match = _expect_re(dut, SIM_MASK_RE, timeout=CROSSING_TIMEOUT_S,
                       what="`Sensor sim active mask` log line")
    mask = int(_grp(match, 1), 16)
    print(f"  [dut] sensor sim active mask 0x{mask:03x}")
    assert mask & DRIVEN_INPUT_BIT, (
        f"The driven port is not in the simulated mask 0x{mask:03x}")


def _wait_out_hold_off():
    """Sleep until a crossing is certain to be granted an upload."""
    if _last_upload_s is None:
        return
    wait_s = HOLD_OFF_S + HOLD_OFF_GUARD_S - (time.monotonic() - _last_upload_s)
    if wait_s > 0:
        print(f"  [test] waiting {wait_s:.0f}s for the {HOLD_OFF_S}s hold-off to expire")
        time.sleep(wait_s)


def _dispatch_timeout(dut):
    """Seconds to allow for the unscheduled dispatch: transmit delay + margin."""
    try:
        delay_ms = _get_setting(dut, "tx_delay")
    except AssertionError:
        delay_ms = 30000  # the setting's ceiling; only affects how long we wait
    return delay_ms / 1000.0 + DISPATCH_MARGIN_S


# --------------------------------------------------------------------------- #
# Coiote helpers
# --------------------------------------------------------------------------- #

def _arm_both_slots(coiote, dut, device, op_timeout):
    """Write the two fixed slots in a single task, Enabled last.

    Called once per run. Every later case moves the readings, never these.
    """
    temperature = {
        R_VALUE_TYPE: VALUE_TYPE_TEMPERATURE,
        R_ALERT_TYPE: ALERT_TYPE_EXCEEDS,
        R_THRESHOLD_VALUE: f"{TEMP_THRESHOLD_C:.1f}",
        R_ENABLED: "true",
    }
    humidity = {
        R_VALUE_TYPE: VALUE_TYPE_HUMIDITY,
        R_ALERT_TYPE: ALERT_TYPE_DROPS_BELOW,
        R_THRESHOLD_VALUE: f"{HUMID_THRESHOLD_PCT:.1f}",
        R_ENABLED: "true",
    }
    task_id = coiote.configure_task(
        device,
        _write_ops(SLOT_EXCEEDS, temperature) + _write_ops(SLOT_DROPS_BELOW, humidity),
        name="hil-threshold-trigger-arm")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] arm slot {SLOT_EXCEEDS} Exceeds {TEMP_THRESHOLD_C} C and slot "
          f"{SLOT_DROPS_BELOW} Drops Below {HUMID_THRESHOLD_PCT} %RH -> status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Arming the two threshold slots ended in {status!r}. Report: {report}")


def _write_hold_off(coiote, dut, device, seconds, op_timeout):
    """Write the threshold hold-off and assert the task landed."""
    task_id = coiote.configure_task(
        device,
        [{"write": {"key": THRESHOLD_REPORT_INTERVAL_KEY, "value": str(seconds)}}],
        name="hil-threshold-report-interval")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] {THRESHOLD_REPORT_INTERVAL_KEY} = {seconds} -> status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Writing a {seconds}s threshold report interval ended in {status!r}. "
        f"Report: {report}")


def _await_sent_flag(coiote, device, key, since, what, timeout=UPLOAD_TIMEOUT_S):
    """Wait for a cached flag to show the report it arrived with.

    read_cached reflects what the device last *sent*, so a true here is proof of
    a threshold-marked upload rather than of the device's live state — the
    device clears both flags after the ACK and only a Read shows that (spec
    sections 3 and 6). Returns the time the portal recorded for that value, or
    None when the tenant does not carry one.
    """
    deadline = time.time() + timeout
    value, updated = None, None
    while time.time() < deadline:
        try:
            value, updated = coiote.read_cached(device, key)
        except CoioteError:
            # Both flags are only ever sent when true, so either can be absent
            # from the cached model until the first threshold upload.
            time.sleep(10)
            continue
        if _as_bool(value) is True and (updated is None or updated >= since):
            print(f"  [coiote] {what}={value!r} sent at {updated}")
            return updated
        time.sleep(10)
    pytest.fail(f"{what} never reached Coiote as true within {timeout}s "
                f"(last {value!r} at {updated})")


def _assert_sent_after_contact(what, sent_at, before_contact):
    """Fail when a cached true predates the upload under test."""
    assert sent_at is None or before_contact is None or sent_at > before_contact, (
        f"{what} was last sent at {sent_at}, not after the pre-upload contact "
        f"at {before_contact}: the true is left over from an earlier report")


def _cached_priority(coiote, device):
    """Return (value, update time) of the Priority flag, or (None, None)."""
    try:
        return coiote.read_cached(device, PRIORITY_KEY)
    except CoioteError:
        return None, None


def _read_status_resources(coiote, dut, device, slot, op_timeout, name):
    """Device Read of a slot's Alert, Trigger Count and Last Triggered."""
    keys = [_key(slot, R_ALERT), _key(slot, R_TRIGGER_COUNT),
            _key(slot, R_LAST_TRIGGERED)]
    values = _read_keys(coiote, dut, device, keys, op_timeout, name=name)
    return (_as_bool(values[keys[0]]), _as_int(values[keys[1]]),
            values[keys[2]])


def _is_triggered_time(value):
    """True when a Last Triggered value is a real timestamp, not 'never' (0)."""
    if value is None:
        return False
    text = str(value).strip()
    if not text or text.startswith("1970-01-01T00:00:00"):
        return False
    try:
        return int(text) != 0
    except ValueError:
        return True  # an ISO-8601 rendering; anything but the epoch is a time


# --------------------------------------------------------------------------- #
# 1 - the fixed slots arm, and a reading on the quiet side does not cross
# --------------------------------------------------------------------------- #

def test_baseline_reading_does_not_cross(coiote, dut, request):
    """Arm both slots at their fixed values and check a 20 C / 50 %RH sample,
    on the quiet side of each, crosses neither."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _assert_sim_reached_the_sample(dut)

    _write_hold_off(coiote, dut, device, HOLD_OFF_S, op_timeout)
    _arm_both_slots(coiote, dut, device, op_timeout)

    _sample_and_expect_quiet(dut, "the baseline reading")


# --------------------------------------------------------------------------- #
# 2 - a reading driven above the slot triggers an immediate, marked upload
# --------------------------------------------------------------------------- #

def test_exceeds_triggers_immediate_upload(coiote, dut, request):
    """A reading driven from 20 C to 30 C transmits the sample ahead of
    schedule, and the Send carries that slot's Alert flag and Priority."""
    global _priority_seen

    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)
    _wait_out_hold_off()

    baseline_dispatches = _status(dut)["dispatches"]
    before_contact = coiote.device_last_contact(device)
    since = datetime.datetime.now(datetime.timezone.utc)

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    upload = _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS, "the 30 C reading")
    assert upload == 1, (
        "A crossing past the hold-off must request an immediate upload; upload 0 "
        f"means the device was still inside the {HOLD_OFF_S}s window")

    # pending=threshold is consumed by the dispatch and is usually gone before
    # the first `magnet status` lands, so only the dispatch itself is asserted.
    status = _wait_dispatches(dut, baseline_dispatches + 1,
                              timeout=_dispatch_timeout(dut))
    print(f"  [dut] status after the threshold dispatch: {status}")

    what = f"Slot {SLOT_EXCEEDS} Alert"
    sent_at = _await_sent_flag(coiote, device, _key(SLOT_EXCEEDS, R_ALERT), since, what)
    _assert_sent_after_contact(what, sent_at, before_contact)

    # The same Send must carry Priority, so Portal acts on the report at once.
    priority_sent_at = _await_sent_flag(coiote, device, PRIORITY_KEY, since,
                                        "EXACT Info Priority",
                                        timeout=SAME_SEND_TIMEOUT_S)
    _priority_seen = True
    _assert_sent_after_contact("EXACT Info Priority", priority_sent_at, before_contact)

    alert, count, last_triggered = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-status")
    assert alert is False, (
        f"Slot {SLOT_EXCEEDS} Alert reads {alert!r} on a device Read; the device "
        f"must clear it once the report carrying it was acknowledged")
    assert count >= 1, (
        f"Slot {SLOT_EXCEEDS} Trigger Count is {count!r}, expected at least 1")
    assert _is_triggered_time(last_triggered), (
        f"Slot {SLOT_EXCEEDS} Last Triggered is {last_triggered!r}, expected a "
        f"timestamp (0 means never)")


# --------------------------------------------------------------------------- #
# 3 - a second crossing inside the hold-off is flagged, not transmitted
# --------------------------------------------------------------------------- #

def test_second_crossing_inside_hold_off_is_flagged_only(coiote, dut, request):
    """Two crossings a few seconds apart: the first transmits, the second is
    only flagged, and the crossing is still counted.

    Self-contained on purpose — it drives both of its own crossings rather than
    measuring against whatever the test above left behind, because the Coiote
    round trips there take far longer than the hold-off.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)
    _wait_out_hold_off()

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    upload = _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS, "the anchor crossing")
    assert upload == 1, (
        f"The anchor crossing must transmit; the {HOLD_OFF_S}s hold-off was waited out")
    anchor_s = time.monotonic()

    # Back below and above again: the return re-arms the port, the second rise
    # crosses, and both samples fit inside the hold-off the anchor started.
    _sim_set(dut, DRIVEN_INPUT, TEMP_BELOW_MC)
    _sample_and_expect_quiet(dut, "the return below the threshold", window=SHORT_QUIET_S)

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    upload = _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS,
                                         "the crossing inside the hold-off")
    elapsed = time.monotonic() - anchor_s
    assert elapsed < HOLD_OFF_S, (
        f"The second crossing came {elapsed:.0f}s after the first, outside the "
        f"{HOLD_OFF_S}s hold-off, so it proves nothing; the bench is too slow")
    assert upload == 0, (
        f"The crossing came {elapsed:.0f}s after the transmitted one, inside the "
        f"{HOLD_OFF_S}s hold-off, so it must be flagged only (upload 0)")

    # The crossing is recorded even though nothing was transmitted for it. The
    # Alert it left pending is not asserted here: the read task's own nudge
    # sends the report carrying it, and the Read that follows answers with the
    # flag already cleared, so the cached model can show either. The Exceeds
    # test covers the delivery of an Alert.
    _, count, _ = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-flagged")
    assert count >= 2, (
        f"Slot {SLOT_EXCEEDS} Trigger Count is {count!r}; a flagged-only crossing "
        f"still counts")


# --------------------------------------------------------------------------- #
# 4 - a crossing past the hold-off transmits again
# --------------------------------------------------------------------------- #

def test_crossing_past_the_hold_off_uploads_again(coiote, dut, request):
    """The same pair of readings, spaced beyond the 60 s hold-off, transmits."""
    device = request.config.getoption("--coiote-device")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)
    _wait_out_hold_off()

    baseline_dispatches = _status(dut)["dispatches"]
    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    upload = _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS,
                                         "the crossing past the hold-off")
    assert upload == 1, (
        f"The crossing came more than {HOLD_OFF_S}s after the last transmitted "
        f"one, so it must request an immediate upload (upload 1)")

    status = _wait_dispatches(dut, baseline_dispatches + 1,
                              timeout=_dispatch_timeout(dut))
    assert status["subjob"] == "normal", (
        f"A threshold upload transmits as a plain report: {status}")


# --------------------------------------------------------------------------- #
# 5 - edge-triggered: a reading that stays beyond does not fire again
# --------------------------------------------------------------------------- #

def test_reading_that_stays_beyond_does_not_retrigger(coiote, dut, request):
    """The slot re-arms only when the reading leaves the triggering side, so a
    second sample at the same 30 C reading must be silent.

    This is what stops a probe parked above the threshold from requesting an
    upload on every sample, which on a battery device is an LTE session per
    log interval.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS, "the 30 C reading")

    # Same reading, next sample: nothing moved, so nothing may cross.
    _sample_and_expect_quiet(dut, "the second sample at 30 C")

    _, count, _ = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-no-retrigger")
    print(f"  [coiote] Trigger Count after the repeated reading: {count}")


# --------------------------------------------------------------------------- #
# 6 - rewriting the Threshold Value re-arms the slot
# --------------------------------------------------------------------------- #

def test_config_change_rearms_the_slot(coiote, dut, request):
    """A slot whose configuration changes is re-armed, so the next sample
    crosses again even though the reading never moved.

    Picks up where the previous case left off: the reading is still at 30 C and
    the slot is latched beyond, so only the rewrite can produce a crossing. The
    value is rewritten to the same threshold in a different decimal form, which
    the device still takes as a change to that slot.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    _sample_and_expect_quiet(dut, "the still-latched 30 C reading",
                             window=SHORT_QUIET_S)

    _write_threshold_value(coiote, dut, device, SLOT_EXCEEDS,
                           f"{TEMP_REARM_THRESHOLD_C:.1f}", op_timeout)
    _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS, "the re-armed slot")

    # Put the slot back where the rest of the file expects it.
    _write_threshold_value(coiote, dut, device, SLOT_EXCEEDS,
                           f"{TEMP_THRESHOLD_C:.1f}", op_timeout)


# --------------------------------------------------------------------------- #
# 5 - Drops Below on the humidity input
# --------------------------------------------------------------------------- #

def test_drops_below_triggers_on_humidity(coiote, dut, request):
    """Humidity driven from 50 %RH to 30 %RH crosses the Drops Below slot.

    The case a threshold moved around a live probe cannot reach: the RH input
    reads well above any value a Drops Below slot could be armed at without
    firing the moment it arms.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline humidity", window=SHORT_QUIET_S)

    _sim_set(dut, "humid", HUMID_BELOW_MPCT)
    _sample_and_expect_crossing(dut, 1 << SLOT_DROPS_BELOW, "the 30 %RH reading")

    _, count, last_triggered = _read_status_resources(
        coiote, dut, device, SLOT_DROPS_BELOW, op_timeout,
        name="hil-threshold-trigger-humidity")
    assert count >= 1, (
        f"Slot {SLOT_DROPS_BELOW} Trigger Count is {count!r}, expected at least 1 "
        f"after the humidity crossing")
    assert _is_triggered_time(last_triggered), (
        f"Slot {SLOT_DROPS_BELOW} Last Triggered is {last_triggered!r}, expected "
        f"a timestamp")


# --------------------------------------------------------------------------- #
# 7 - an unplugged probe drops the port state, so the next reading re-arms
# --------------------------------------------------------------------------- #

def test_unplugged_probe_rearms_the_slot(coiote, dut, request):
    """A port that goes invalid re-arms without the reading ever returning
    below the threshold (spec section 5, "Re-arming").

    The reading goes 30 C -> unplugged -> 30 C. Only the invalid sample in the
    middle can re-arm the port, so the second crossing is proof the firmware
    drops a port's state when its probe disappears.
    """
    device = request.config.getoption("--coiote-device")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)

    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS, "the first 30 C reading")

    _sim_set(dut, DRIVEN_INPUT, "nc")
    _sample_and_expect_quiet(dut, "the unplugged probe")

    # Straight back to 30 C: the reading never returned below 25 C, so an
    # unarmed port could not cross here.
    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    _sample_and_expect_crossing(dut, 1 << SLOT_EXCEEDS,
                                "the 30 C reading after the unplug")


# --------------------------------------------------------------------------- #
# 8 - a crossing on an RTC log wake defers its dispatch by the transmit delay
# --------------------------------------------------------------------------- #

def test_log_wake_crossing_defers_dispatch(coiote, dut, request):
    """A crossing found on an unscheduled log wake waits out the transmit delay.

    The only case in the file that reaches that path. Every other case drives
    its Coiote tasks with ``app_module trigger_tx``, which sets job BOTH and
    arms a NORMAL upload before the sample, so the crossing is dispatched on the
    spot by DATA_EVT_DATA_READY. Only a LOG-job wake with nothing else pending
    takes the app module's delayed branch, which spreads the fleet over the
    per-device transmit delay before dispatching.

    So this case drives the reading beyond the threshold, shortens the log
    interval, and then stops touching the device: the crossing must come from
    the device's own RTC wake, not from ``magnet sample``. The hold-off is
    waited out first, because a flagged-only crossing defers nothing.
    """
    global _last_upload_s

    device = request.config.getoption("--coiote-device")
    _require_online(coiote, device)

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)
    _wait_out_hold_off()

    # The last command that touches the device: from here the next sample has to
    # be the device's own log wake, so no nudge and no `magnet sample`.
    _sim_set(dut, DRIVEN_INPUT, TEMP_ABOVE_MC)
    _set_and_restore(dut, request, "log_interval", LOG_INTERVAL_MIN_S)
    _drain(dut, 1)
    baseline_dispatches = _status(dut)["dispatches"]

    match = _expect_re(dut, CROSSED_RE, timeout=LOG_WAKE_TIMEOUT_S,
                       what="`Threshold crossed` line from an RTC log wake")
    slots, upload = int(_grp(match, 1), 16), int(_grp(match, 2))
    print(f"  [dut] log-wake crossing: slots 0x{slots:02x} upload {upload}")
    assert slots == 1 << SLOT_EXCEEDS, (
        f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")
    assert upload == 1, (
        f"The crossing was only flagged even though the {HOLD_OFF_S}s hold-off "
        f"was waited out first; a flagged-only crossing defers nothing")
    # This sample restarted the device's hold-off, like any granted upload.
    _last_upload_s = time.monotonic()

    match = _expect_re(dut, DEFERRED_RE, timeout=DEFERRED_TIMEOUT_S,
                       what="`Threshold upload deferred` log line")
    deferred_ms = int(_grp(match, 1))
    print(f"  [dut] dispatch deferred by {deferred_ms} ms")
    tx_delay_ms = _get_setting(dut, "tx_delay")
    assert deferred_ms == tx_delay_ms, (
        f"The dispatch was deferred by {deferred_ms} ms, not by the device's "
        f"own transmit delay of {tx_delay_ms} ms")

    status = _wait_dispatches(dut, baseline_dispatches + 1,
                              timeout=_dispatch_timeout(dut))
    print(f"  [dut] status after the deferred dispatch: {status}")
    assert status["subjob"] == "normal", (
        f"A threshold upload transmits as a plain report: {status}")


# --------------------------------------------------------------------------- #
# 9 - no ordinary report carries the Priority flag
# --------------------------------------------------------------------------- #

def test_no_later_report_carries_priority(coiote, dut, request):
    """Reports that no threshold triggered must not be marked Priority.

    The flag belongs to the report it arrived with, so a later report either
    carries Priority false (the whole EXACT Info object rides a non-PSM attach)
    or omits the resource, leaving the earlier true in the cached model. Only a
    *newer* true is a regression: an ordinary report went out flagged.

    Two read tasks, because each one nudges a report out of the device. The
    first drains whatever the preceding cases left pending: several of them end
    on a crossing whose Alert and Priority legitimately ride a later report.
    Only the second window, which starts with nothing pending, is under test.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    if not _priority_seen:
        pytest.skip("The Exceeds test did not record a Priority flag; run the file in order")

    _sim_baseline(dut)
    _sample_and_expect_quiet(dut, "the baseline reading", window=SHORT_QUIET_S)

    _read_status_resources(coiote, dut, device, SLOT_EXCEEDS, op_timeout,
                           name="hil-threshold-trigger-priority-drain")
    _, before = _cached_priority(coiote, device)
    print(f"  [coiote] EXACT Info Priority last sent at {before} before the "
          f"unflagged report")

    _read_status_resources(coiote, dut, device, SLOT_EXCEEDS, op_timeout,
                           name="hil-threshold-trigger-priority")
    value, after = _cached_priority(coiote, device)
    print(f"  [coiote] EXACT Info Priority={value!r} sent at {after}")

    if _as_bool(value) is not True:
        return
    if before is None or after is None:
        print("  [coiote] no update times to compare; the true may be the one an "
              "earlier threshold report sent")
        return
    assert after <= before, (
        f"EXACT Info Priority is true again at {after}, after the last threshold "
        f"report at {before}: a report that no threshold triggered carried the "
        f"Priority flag")
