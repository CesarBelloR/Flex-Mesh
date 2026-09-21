"""Cloud HIL tests for threshold-triggered immediate uploads (FW-1179).

FW-1178 put the EXACT Threshold object (48944) on the device; this file covers
what the device *does* with it: a reading beyond an enabled slot must wake the
radio and push the current sample ahead of schedule, carrying the Alert flag of
the slot that breached and the EXACT Info Priority flag
(docs/specs/lwm2m-immediate-report-thresholds.md, sections 5 and 6).

No physical stimulus is needed. The threshold is placed relative to the live
port readings — five degrees below the coldest port for the positive cases, ten
above the warmest for the negative one — so every configured slot is on the
intended side of every port from the moment it arms.

Covered here:

  * an Exceeds slot armed below the current reading triggers on the next sample,
    dispatches an upload, and the Send reaches Coiote with ``/48944/0/5`` and
    ``/48933/0/7`` true while the device's own read-back shows the alert cleared
    and the slot counted,
  * no later report carries the Priority flag again,
  * a reading that stays beyond does not trigger again (edge-triggered, section
    5 "Re-arming"),
  * rewriting the Threshold Value re-arms the slot, and the rate limit
    (``/48931/0/16``, 900 s by default) decides whether that second crossing
    transmits or is only flagged,
  * a slot configured above every reading stays silent,
  * a crossing found on an unscheduled RTC log wake defers its dispatch by the
    per-device transmit delay (test 5, the only one that reaches that path),
  * a hold-off shortened from Coiote (FW-1225) lets two crossings that the 900 s
    default would have collapsed into one both request an upload (test 6, which
    arms its own slot and does not depend on the tests above).

**The order of the tests in this file is load-bearing.** Test 1 arms slot 0 and
is the only one that lets it trigger; test 2 asserts the slot stays quiet while
the reading stays beyond it; test 3 re-arms that same slot by rewriting its
value and compares its upload flag against the time test 1 triggered; test 4
adds a second slot on top of the still-enabled first one; test 5 re-arms slot 0
once more and waits out the rate limit first. Running a test on its own is fine
only for tests 1 and 6.

The device is never rebooted: FW-1221 makes a cold boot fail an assert in the
app module, and the trigger state this file exercises is RAM-only anyway, so a
reboot would erase exactly what is under test.

Timing. The DUT is a queue-mode (PSM/eDRX) cellular device, so every configure
and read task is delivered on a registration update; each is nudged along with
``app_module trigger_tx`` and can still take minutes. The immediate upload
itself adds an LTE bring-up, and test 5 may sit out most of the 900 s rate-limit
window before it arms. Test 6 adds two more configure tasks and a 90 s wait.
Budget ~85 min for the file.

Requirements:
  * the board flashed (HIL debug build, no rtt.conf) and **LTE/cloud connected**
  * a temperature probe on port 1 (any further ports may be populated too)
  * ``--port`` (the sample requests and the log assertions go over the console)
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the tests skip)
  * the 48944 DDF uploaded to the Coiote tenant data model

Build:
  west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
    -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Run:
  pytest tests/hil/test_threshold_trigger_coiote.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
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
from test_threshold_coiote import (ALERT_TYPE_EXCEEDS, R_ALERT, R_ALERT_TYPE,
                                   R_ENABLED, R_THRESHOLD_VALUE,
                                   R_TRIGGER_COUNT, R_VALUE_TYPE,
                                   VALUE_TYPE_TEMPERATURE, _as_bool, _as_int,
                                   _disable_slots, _drive_task, _key,
                                   _read_keys, _require_online, _write_ops,
                                   _write_threshold_value)
# Shell-side helpers: the `magnet status` parser and the log-safe expect used by
# every console-driven HIL test.
from test_upload_reason import (_drain, _expect_re, _get_setting, _grp,
                                _set_and_restore, _status, _wait_dispatches)

# Slot 0 is the one that triggers; slot 1 is the silent control. Slots 2 and 3
# are left alone so this file can share a device with the FW-1178 tests.
SLOT_EXCEEDS = 0
SLOT_ABOVE = 1
TOUCHED_SLOTS = (SLOT_EXCEEDS, SLOT_ABOVE)

# Threshold offsets from the live readings. The first two put the threshold
# below every port so every port is beyond it the moment the slot arms; the
# third puts it above every port so nothing can cross.
BELOW_MARGIN_C = 5.0
BELOW_MARGIN_REARM_C = 6.0
BELOW_MARGIN_LOG_WAKE_C = 7.0
ABOVE_MARGIN_C = 10.0

# The device's default hold-off (CONFIG_ETC_APP_THRESHOLD_MIN_REPORT_INTERVAL_S,
# 900 s), which tests 1-5 assume is in force. A crossing inside this window of
# the previous threshold upload is flagged but does not transmit ahead of
# schedule. Test 6 writes a shorter one and restores this value.
RATE_LIMIT_S = 900
# Elapsed times this close to the rate limit are ambiguous (the device measures
# it from its own uptime at the sample, the test from the host clock at the log
# line), so either upload flag is accepted there.
RATE_LIMIT_GUARD_S = 60

# The range the firmware itself accepts as a temperature reading
# (SENSOR_TEMP_C_MIN/MAX in applications/etc-app/src/events/sensor_event.h);
# outside it the port is not a temperature probe.
TEMP_MIN_C = -30.0
TEMP_MAX_C = 120.0

SHELL_ECHO_TIMEOUT_S = 10
# A sample completes in ~3 s; these only bound a regression.
SAMPLE_TIMEOUT_S = 40
CHANNEL_QUIET_S = 4
CROSSING_TIMEOUT_S = 60
# How long to look in the already-buffered output before requesting a sample.
BUFFERED_LOOK_S = 2
# A crossing shows up within ~3 s of the sample; this window is the proof of
# absence for the two negative cases.
NO_CROSSING_WINDOW_S = 30
# Unscheduled dispatch = the transmit delay (0..29.5 s) plus margin.
DISPATCH_MARGIN_S = 45
# LTE bring-up, Send, and Coiote ingesting it.
UPLOAD_TIMEOUT_S = 420
# Both flags ride the same Send, so once one is in the cached model the other is
# too; this only absorbs the portal applying them a moment apart.
SAME_SEND_TIMEOUT_S = 60

# Per-port temperature of one acquisition, 0-based channel (channel 0 = port 1):
#   Channel 0 temp 21.500000
CHANNEL_TEMP_RE = re.compile(r"Channel (\d) temp (-?\d+\.\d+)")
# One line per sample from the data module's threshold evaluation: the bitmask
# of slots that crossed, and whether an immediate upload was requested (0 when
# the rate limit suppressed it).
CROSSED_RE = re.compile(r"Threshold crossed: slots 0x([0-9a-fA-F]{2}) upload ([01])")
# The app module's delayed-dispatch line, emitted only when a threshold request
# is the sole reason a LOG-job sample has to transmit.
DEFERRED_RE = re.compile(r"Threshold upload deferred by (\d+) ms")

# The shortest log interval the settings layer accepts
# (ETC_SETTING_LOG_INTERVAL_SECS_MIN in etc_settings.h).
LOG_INTERVAL_MIN_S = 60
# Two intervals plus margin: the shortened interval only takes effect at the
# next alarm the device programs, so the first wake can still be a long one.
LOG_WAKE_TIMEOUT_S = 2 * LOG_INTERVAL_MIN_S + 60
# The deferred line follows the crossing within the same sample.
DEFERRED_TIMEOUT_S = 15
# Ceiling on the wait for the rate-limit window to expire before test 5 arms.
REARM_WAIT_BUDGET_S = 20 * 60

# Read-only status resource of a slot, spec section 3 (RID 6). The FW-1178 file
# has no constant for it; it never reads one.
R_LAST_TRIGGERED = "Last Triggered"

# EXACT Info Priority (48933/0/7): "process this report now". A threshold report
# carries it beside the slot's Alert, so Portal handles it like a magnet-swipe
# reading instead of holding it for the periodic batch (spec section 6).
PRIORITY_KEY = "EXACT Info.0.Priority"

# Monotonic timestamp of the first trigger, so test 3 can tell whether its own
# crossing falls inside the rate-limit window. None until test 1 has triggered.
_first_trigger_s = None
# True once test 1 has seen Priority on the triggered report, and the portal
# timestamp of that flag, so test 2 can tell a stale true apart from one a later
# report sent. The timestamp stays None on a tenant that carries no update times.
_priority_seen = False
_priority_sent_at = None
# Monotonic timestamp of the most recent crossing that was granted an upload —
# the point the device restarts the rate-limit window from. Test 5 waits this
# out so its own crossing is never the flagged-only kind.
_last_upload_s = None


# --------------------------------------------------------------------------- #
# Cleanup: no slot this file touched may outlive the run enabled
# --------------------------------------------------------------------------- #
_cleanup_state = {"done": False, "armed": False}


def _queue_disable(coiote, device):
    """Queue Enabled=false for both slots without waiting for delivery.

    The safety net for a run that stops before the last test (``-x``, a fixture
    error, Ctrl-C): the ``dut`` fixture closes the console at the end of every
    test, so there is nothing left to nudge with, but a queued task still lands
    at the device's next check-in.
    """
    if _cleanup_state["done"]:
        return
    try:
        coiote.configure_task(
            device,
            [{"write": {"key": _key(slot, R_ENABLED), "value": "false"}}
             for slot in TOUCHED_SLOTS],
            name="hil-threshold-trigger-cleanup")
        print(f"  [cleanup] queued disable of slots {list(TOUCHED_SLOTS)}")
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
    """Leave slots 0 and 1 disabled however the run ends.

    Autouse, so a run without Coiote credentials skips the whole file rather
    than half-configuring the device.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    if not _cleanup_state["armed"]:
        _cleanup_state["armed"] = True
        request.session.addfinalizer(lambda: _queue_disable(coiote, device))

    yield

    # The console is still open here, so this pass can nudge the write along and
    # confirm it landed; the session finalizer above only covers an early exit.
    if _is_last_in_file(request):
        _disable_slots(coiote, dut, device, TOUCHED_SLOTS, op_timeout)
        _cleanup_state["done"] = True


# --------------------------------------------------------------------------- #
# Console helpers
# --------------------------------------------------------------------------- #

def _request_sample(dut):
    """Ask for one acquisition, anchored on this request's own echo.

    Deliberately does not drain first: a caller waiting for a crossing may have
    a log line of interest already buffered.
    """
    time.sleep(RAIL_SETTLE_S)
    shell(dut, "magnet sample")
    _expect_re(dut, re.escape("magnet sample"), timeout=SHELL_ECHO_TIMEOUT_S,
               what="`magnet sample` echo")


def _sample_port_temps(dut):
    """Trigger one acquisition and return {port (1-based): degrees Celsius}.

    The per-channel DBG line is the same reading the threshold evaluation sees,
    which the Coiote data model is not: the cached EXACT Temperature value is
    whatever the device last *sent*, so it can be a whole transmit interval old
    and would put the threshold next to a stale reading.
    """
    _drain(dut, 1)
    _request_sample(dut)

    temps = {}
    deadline = time.time() + SAMPLE_TIMEOUT_S
    while time.time() < deadline:
        try:
            match = dut.expect(CHANNEL_TEMP_RE, timeout=CHANNEL_QUIET_S)
        except Exception:
            break  # the acquisition has reported every port it has
        temps[int(_grp(match, 1)) + 1] = float(_grp(match, 2))

    plausible = {port: value for port, value in temps.items()
                 if TEMP_MIN_C <= value <= TEMP_MAX_C}
    if not plausible:
        pytest.skip(f"No usable temperature reading on any port (saw {temps}); "
                    f"attach a probe to port 1")
    print(f"  [dut] port temperatures {plausible}")
    return plausible


def _await_crossing(dut, timeout=CROSSING_TIMEOUT_S):
    """Return (slot mask, upload flag) from the next `Threshold crossed` line.

    The buffered output is inspected before a sample is requested: a slot arms
    the moment its Enabled write lands, so a scheduled log-interval sample can
    have crossed it already while the configure task was still being polled. A
    crossing missed that way could not be reproduced — the port stays beyond and
    an edge-triggered slot does not fire twice.
    """
    try:
        match = dut.expect(CROSSED_RE, timeout=BUFFERED_LOOK_S)
    except Exception:
        _request_sample(dut)
        match = _expect_re(dut, CROSSED_RE, timeout=timeout,
                           what="`Threshold crossed` log line")
    slots, upload = int(_grp(match, 1), 16), int(_grp(match, 2))
    print(f"  [dut] threshold crossed: slots 0x{slots:02x} upload {upload}")
    return slots, upload


def _assert_no_crossing(dut, window=NO_CROSSING_WINDOW_S):
    """Fail if a `Threshold crossed` line appears within ``window`` seconds."""
    try:
        match = dut.expect(CROSSED_RE, timeout=window)
    except Exception:
        return
    pytest.fail(f"Unexpected threshold crossing: slots 0x{_grp(match, 1)} "
                f"upload {_grp(match, 2)}")


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

def _write_slot(coiote, dut, device, slot, threshold_c, op_timeout):
    """Write one Exceeds temperature slot in a single task, Enabled last."""
    config = {
        R_VALUE_TYPE: VALUE_TYPE_TEMPERATURE,
        R_ALERT_TYPE: ALERT_TYPE_EXCEEDS,
        R_THRESHOLD_VALUE: f"{threshold_c:.1f}",
        R_ENABLED: "true",
    }
    task_id = coiote.configure_task(device, _write_ops(slot, config),
                                    name="hil-threshold-trigger-arm")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] arm slot {slot} at {config[R_THRESHOLD_VALUE]} C -> "
          f"status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Arming slot {slot} at {config[R_THRESHOLD_VALUE]} C ended in "
        f"{status!r}. Report: {report}")


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


def _assert_priority_not_resent(coiote, device):
    """No report after the triggered one may carry the Priority flag again.

    The flag belongs to the report it arrived with, so a later report either
    carries Priority false (the whole EXACT Info object rides a non-PSM attach)
    or omits the resource, leaving the earlier true in the cached model. Only a
    *newer* true is a regression: an ordinary report went out flagged.
    """
    if not _priority_seen:
        pytest.skip("Test 1 did not record a Priority flag; run the file in order")
    try:
        value, updated = coiote.read_cached(device, PRIORITY_KEY)
    except CoioteError:
        pytest.fail("EXACT Info Priority is absent from the cached data model; "
                    "the threshold report of test 1 should have carried it")
    print(f"  [coiote] EXACT Info Priority={value!r} sent at {updated}")
    if _as_bool(value) is not True:
        return
    if updated is None or _priority_sent_at is None:
        print("  [coiote] no update time to compare; the true may be the one "
              "the threshold report sent")
        return
    assert updated <= _priority_sent_at, (
        f"EXACT Info Priority is true again at {updated}, after the threshold "
        f"report at {_priority_sent_at}: a report that no threshold triggered "
        f"carried the Priority flag")


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
# 1 - an armed slot below the reading triggers an immediate, marked upload
# --------------------------------------------------------------------------- #

def test_threshold_exceeds_triggers_immediate_upload(coiote, dut, request):
    """A slot armed below every port transmits the current sample ahead of
    schedule, and the Send carries that slot's Alert flag and Priority."""
    global _first_trigger_s, _last_upload_s, _priority_seen, _priority_sent_at

    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    temps = _sample_port_temps(dut)
    # Below the coldest port, so every populated port is beyond the threshold
    # from the moment the slot arms and no port can cross later on its own.
    threshold_c = min(temps.values()) - BELOW_MARGIN_C

    _write_slot(coiote, dut, device, SLOT_EXCEEDS, threshold_c, op_timeout)

    # Taken after the last nudge: every `app_module trigger_tx` poll of the
    # configure task arms and dispatches an upload of its own, so a baseline
    # from before the write would not pin the threshold dispatch to anything.
    baseline_dispatches = _status(dut)["dispatches"]

    # From here on nothing drains the console: the crossing may already be in
    # the buffer from a scheduled sample taken while the task was in flight.
    before_contact = coiote.device_last_contact(device)
    since = datetime.datetime.now(datetime.timezone.utc)

    slots, upload = _await_crossing(dut)
    _first_trigger_s = _last_upload_s = time.monotonic()
    assert slots == 1 << SLOT_EXCEEDS, (
        f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")
    assert upload == 1, (
        "The first crossing since boot must request an immediate upload; "
        "upload 0 means the rate limit suppressed it, so an earlier threshold "
        "upload happened within the last "
        f"{RATE_LIMIT_S}s (reboot-free rate-limit state)")

    # pending=threshold is consumed by the dispatch and is usually gone before
    # the first `magnet status` lands, so only the dispatch itself is asserted.
    status = _wait_dispatches(dut, baseline_dispatches + 1,
                              timeout=_dispatch_timeout(dut))
    print(f"  [dut] status after the threshold dispatch: {status}")

    what = f"Slot {SLOT_EXCEEDS} Alert"
    sent_at = _await_sent_flag(coiote, device, _key(SLOT_EXCEEDS, R_ALERT),
                               since, what)
    _assert_sent_after_contact(what, sent_at, before_contact)

    # The same Send must carry Priority, so Portal acts on the report at once.
    _priority_sent_at = _await_sent_flag(coiote, device, PRIORITY_KEY, since,
                                         "EXACT Info Priority",
                                         timeout=SAME_SEND_TIMEOUT_S)
    _priority_seen = True
    _assert_sent_after_contact("EXACT Info Priority", _priority_sent_at,
                               before_contact)

    alert, count, last_triggered = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-status")
    assert alert is False, (
        f"Slot {SLOT_EXCEEDS} Alert reads {alert!r} on a device Read; the "
        f"device must clear it once the report carrying it was acknowledged")
    assert count == 1, (
        f"Slot {SLOT_EXCEEDS} Trigger Count is {count!r}, expected 1 after one "
        f"crossing")
    assert _is_triggered_time(last_triggered), (
        f"Slot {SLOT_EXCEEDS} Last Triggered is {last_triggered!r}, expected a "
        f"timestamp (0 means never)")


# --------------------------------------------------------------------------- #
# 2 - edge-triggered: a reading that stays beyond does not fire again
# --------------------------------------------------------------------------- #

def test_threshold_does_not_retrigger_while_beyond(coiote, dut, request):
    """The slot re-arms only when the reading returns to the other side, so the
    next sample with the same reading must be silent."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _drain(dut, 1)
    _request_sample(dut)
    _assert_no_crossing(dut)

    alert, count, _ = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-no-retrigger")
    assert count == 1, (
        f"Slot {SLOT_EXCEEDS} Trigger Count is {count!r} after a second sample "
        f"beyond the same threshold, expected it to stay 1")
    assert alert is False, (
        f"Slot {SLOT_EXCEEDS} Alert is {alert!r}; nothing triggered, so nothing "
        f"should be awaiting delivery")

    # The read task above was nudged along with `app_module trigger_tx`, so at
    # least one ordinary report has gone out since the threshold one.
    _assert_priority_not_resent(coiote, device)


# --------------------------------------------------------------------------- #
# 3 - a configuration change re-arms the slot; the rate limit gates the upload
# --------------------------------------------------------------------------- #

def test_threshold_rearms_after_config_change(coiote, dut, request):
    """Rewriting the Threshold Value resets the slot's evaluation state, so the
    next sample crosses again even though the reading never moved."""
    global _last_upload_s

    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    if _first_trigger_s is None:
        pytest.skip("Test 1 did not record a trigger; run the file in order")

    temps = _sample_port_temps(dut)
    # Still below every port, so the rewrite only re-arms: it does not change
    # which side of the threshold any reading is on.
    threshold_c = min(temps.values()) - BELOW_MARGIN_REARM_C
    _write_threshold_value(coiote, dut, device, SLOT_EXCEEDS,
                           f"{threshold_c:.1f}", op_timeout)

    slots, upload = _await_crossing(dut)
    elapsed = time.monotonic() - _first_trigger_s
    if upload == 1:
        _last_upload_s = time.monotonic()
    assert slots == 1 << SLOT_EXCEEDS, (
        f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")

    # Inside the hold-off window the crossing is flagged but must not transmit;
    # outside it, it must. Near the boundary the device's uptime and the host
    # clock disagree by more than the answer is worth, so both are accepted.
    if abs(elapsed - RATE_LIMIT_S) <= RATE_LIMIT_GUARD_S:
        print(f"  [dut] {elapsed:.0f}s since the first trigger is within "
              f"{RATE_LIMIT_GUARD_S}s of the {RATE_LIMIT_S}s rate limit; "
              f"accepting upload={upload}")
    elif elapsed < RATE_LIMIT_S:
        assert upload == 0, (
            f"The crossing came {elapsed:.0f}s after the first trigger, inside "
            f"the {RATE_LIMIT_S}s rate limit, so it must be flagged only "
            f"(upload 0), not transmitted")
    else:
        assert upload == 1, (
            f"The crossing came {elapsed:.0f}s after the first trigger, past "
            f"the {RATE_LIMIT_S}s rate limit, so it must request an immediate "
            f"upload (upload 1)")

    _, count, last_triggered = _read_status_resources(
        coiote, dut, device, SLOT_EXCEEDS, op_timeout,
        name="hil-threshold-trigger-rearm")
    assert count == 2, (
        f"Slot {SLOT_EXCEEDS} Trigger Count is {count!r} after the second "
        f"crossing, expected 2 (a rate-limited crossing still counts)")
    assert _is_triggered_time(last_triggered), (
        f"Slot {SLOT_EXCEEDS} Last Triggered is {last_triggered!r}, expected a "
        f"timestamp")


# --------------------------------------------------------------------------- #
# 4 - a slot above every reading never fires
# --------------------------------------------------------------------------- #

def test_threshold_above_reading_is_silent(coiote, dut, request):
    """An Exceeds slot ten degrees above the warmest port must stay untriggered,
    with its status resources at their boot values."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    temps = _sample_port_temps(dut)
    threshold_c = max(temps.values()) + ABOVE_MARGIN_C
    _write_slot(coiote, dut, device, SLOT_ABOVE, threshold_c, op_timeout)

    # Slot 0 is still enabled and still beyond its own threshold, so the only
    # line this window could produce would come from slot 1.
    _drain(dut, 1)
    _request_sample(dut)
    _assert_no_crossing(dut)

    alert, count, last_triggered = _read_status_resources(
        coiote, dut, device, SLOT_ABOVE, op_timeout,
        name="hil-threshold-trigger-silent")
    assert alert is False, (
        f"Slot {SLOT_ABOVE} Alert is {alert!r}; a threshold above every reading "
        f"must never flag a report")
    assert count == 0, (
        f"Slot {SLOT_ABOVE} Trigger Count is {count!r}, expected 0")
    assert not _is_triggered_time(last_triggered), (
        f"Slot {SLOT_ABOVE} Last Triggered is {last_triggered!r}, expected 0 "
        f"(never triggered)")


# --------------------------------------------------------------------------- #
# 5 - a crossing on an RTC log wake defers its dispatch by the transmit delay
# --------------------------------------------------------------------------- #

def _await_log_wake_crossing(dut, timeout=LOG_WAKE_TIMEOUT_S):
    """Wait for a `Threshold crossed` line without asking for a sample.

    Requesting one would defeat the point: `magnet sample` leaves the job as the
    last nudge set it (BOTH), and the deferred path needs a plain LOG job.
    """
    match = _expect_re(dut, CROSSED_RE, timeout=timeout,
                       what="`Threshold crossed` line from an RTC log wake")
    slots, upload = int(_grp(match, 1), 16), int(_grp(match, 2))
    print(f"  [dut] log-wake crossing: slots 0x{slots:02x} upload {upload}")
    return slots, upload


def test_threshold_log_wake_defers_dispatch(coiote, dut, request):
    """A crossing found on an unscheduled log wake waits out the transmit delay.

    This is the only case in the file that reaches that path. Tests 1-4 drive
    their Coiote tasks with ``app_module trigger_tx``, and that command sets job
    BOTH and arms a NORMAL upload before requesting its sample, so a crossing
    there is dispatched on the spot by DATA_EVT_DATA_READY. Only a LOG-job wake
    with nothing else pending takes the app module's delayed branch, which
    spreads the fleet over the per-device transmit delay before dispatching. So
    this case shortens the log interval, stops nudging, and lets the device's
    own RTC wake produce the crossing.

    The rate limit is waited out *before* arming rather than accepted as an
    upload-0 outcome: a flagged-only crossing never defers anything.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    if _last_upload_s is None:
        pytest.skip("No earlier threshold upload recorded; run the file in order")

    # Arm only once the device's hold-off has expired, so the crossing below is
    # guaranteed to request an upload.
    wait_s = RATE_LIMIT_S + RATE_LIMIT_GUARD_S - (time.monotonic() - _last_upload_s)
    if wait_s > REARM_WAIT_BUDGET_S:
        pytest.skip(f"Would have to wait {wait_s:.0f}s for the {RATE_LIMIT_S}s "
                    f"rate limit, over the {REARM_WAIT_BUDGET_S}s budget")
    if wait_s > 0:
        print(f"  [test] waiting {wait_s:.0f}s for the rate-limit window to expire")
        time.sleep(wait_s)

    temps = _sample_port_temps(dut)
    # Still below every port: the rewrite only re-arms the slot.
    threshold_c = min(temps.values()) - BELOW_MARGIN_LOG_WAKE_C
    _write_threshold_value(coiote, dut, device, SLOT_EXCEEDS,
                           f"{threshold_c:.1f}", op_timeout)

    # The re-arm takes effect for the next sample, which must be the device's
    # own log wake - so no nudge, no `magnet sample`, from here on.
    _set_and_restore(dut, request, "log_interval", LOG_INTERVAL_MIN_S)
    _drain(dut, 1)
    baseline_dispatches = _status(dut)["dispatches"]

    slots, upload = _await_log_wake_crossing(dut)
    assert slots == 1 << SLOT_EXCEEDS, (
        f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")
    assert upload == 1, (
        "The crossing was only flagged even though the rate-limit window was "
        "waited out before arming; a flagged-only crossing cannot exercise the "
        "deferred dispatch")

    match = _expect_re(dut, DEFERRED_RE, timeout=DEFERRED_TIMEOUT_S,
                       what="`Threshold upload deferred` log line")
    print(f"  [dut] dispatch deferred by {_grp(match, 1)} ms")

    status = _wait_dispatches(dut, baseline_dispatches + 1,
                              timeout=_dispatch_timeout(dut))
    print(f"  [dut] status after the deferred dispatch: {status}")
    assert status["subjob"] == "normal", (
        f"A threshold upload transmits as a plain report: {status}")


# --------------------------------------------------------------------------- #
# 6 - a shortened hold-off lets two crossings inside the default window upload
# --------------------------------------------------------------------------- #

# FW-1225: the hold-off is a configuration resource of EXACT Configuration
# (48931), written by raw LwM2M path because the resource is newer than the DDF
# most tenants hold. Switch to the data-model key once the DDF is uploaded.
THRESHOLD_REPORT_INTERVAL_PATH = "/48931/0/16"
# What this test writes; the settings layer's floor
# (ETC_SETTING_THRESHOLD_REPORT_INTERVAL_SECS_MIN).
SHORT_INTERVAL_S = 60
# Margin on top of the short hold-off before the second crossing is armed, so a
# few seconds of clock skew between the device's uptime and the host cannot
# leave the crossing inside the window.
SHORT_INTERVAL_GUARD_S = 30
# Still below every port, and distinct from the earlier tests' offsets so the
# rewrite is always a real change.
BELOW_MARGIN_SHORT_ARM_C = 8.0
BELOW_MARGIN_SHORT_REARM_C = 9.0


def _write_report_interval(coiote, dut, device, seconds, op_timeout):
    """Write the threshold hold-off and assert the task landed."""
    task_id = coiote.configure_task(
        device,
        [{"write": {"key": THRESHOLD_REPORT_INTERVAL_PATH, "value": str(seconds)}}],
        name="hil-threshold-report-interval")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] {THRESHOLD_REPORT_INTERVAL_PATH} = {seconds} -> "
          f"status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Writing a {seconds}s threshold report interval ended in {status!r}. "
        f"Report: {report}")


def test_threshold_report_interval_shortens_the_hold_off(coiote, dut, request):
    """A 60 s hold-off written from Coiote lets two crossings that the 900 s
    default would have collapsed into one both request an upload.

    Self-contained: it arms slot 0 from scratch rather than inheriting the state
    of the tests above, and restores the default hold-off however it ends. The
    device is not rebooted, so its first crossing may still be inside a hold-off
    an earlier run started; that one only anchors the wait. The proof is the
    second crossing, which falls inside the 900 s default window where the
    firmware before FW-1225 could only have flagged it.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    _require_online(coiote, device)

    _write_report_interval(coiote, dut, device, SHORT_INTERVAL_S, op_timeout)
    try:
        temps = _sample_port_temps(dut)
        coldest_c = min(temps.values())

        # Arming rewrites every resource of the slot, so this crossing does not
        # depend on what the earlier tests left behind.
        _write_slot(coiote, dut, device, SLOT_EXCEEDS,
                    coldest_c - BELOW_MARGIN_SHORT_ARM_C, op_timeout)
        # Only an anchor for the wait below: the device may still be inside a
        # hold-off left by an earlier threshold upload, which is not this test's
        # business either way.
        slots, _ = _await_crossing(dut)
        first_crossing_s = time.monotonic()
        assert slots == 1 << SLOT_EXCEEDS, (
            f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")

        # Measured from the first crossing, which is at or after the last
        # upload the device granted, so this covers the hold-off either way.
        wait_s = SHORT_INTERVAL_S + SHORT_INTERVAL_GUARD_S - (
            time.monotonic() - first_crossing_s)
        if wait_s > 0:
            print(f"  [test] waiting {wait_s:.0f}s for the {SHORT_INTERVAL_S}s "
                  f"hold-off to expire")
            time.sleep(wait_s)

        # Rewriting the Threshold Value re-arms the slot without moving any
        # reading to the other side of it.
        rearm_c = coldest_c - BELOW_MARGIN_SHORT_REARM_C
        _write_threshold_value(coiote, dut, device, SLOT_EXCEEDS,
                               f"{rearm_c:.1f}", op_timeout)
        slots, upload = _await_crossing(dut)
        elapsed = time.monotonic() - first_crossing_s
        assert slots == 1 << SLOT_EXCEEDS, (
            f"Expected only slot {SLOT_EXCEEDS} to cross, got mask 0x{slots:02x}")
        if elapsed >= RATE_LIMIT_S - RATE_LIMIT_GUARD_S:
            pytest.skip(
                f"The second crossing came {elapsed:.0f}s after the first, at "
                f"or beyond the {RATE_LIMIT_S}s default hold-off, so an upload "
                f"proves nothing about the {SHORT_INTERVAL_S}s one that was "
                f"written")
        assert upload == 1, (
            f"The second crossing came {elapsed:.0f}s after the first, past the "
            f"{SHORT_INTERVAL_S}s hold-off that was written, so it must request "
            f"an immediate upload (upload 1)")
    finally:
        try:
            _write_report_interval(coiote, dut, device, RATE_LIMIT_S, op_timeout)
        except Exception as exc:  # never mask the failure under test
            print(f"  [cleanup] WARNING: the hold-off is still "
                  f"{SHORT_INTERVAL_S}s, restore it by hand: {exc}")
