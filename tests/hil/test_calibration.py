"""Operator-attended HIL tests for calibration, on real hardware with a physical
calibrator.

Covers:

  * FW-588  User calibration must survive a reboot (the runtime loader must use
            the user coefficients, not fall back to factory), and a sensor
            acquisition colliding with a calibration must be skipped rather than
            wedge the front-end and deadlock the data module.
  * FW-611  A calibration result must reach the portal; while a *successful*
            result is still awaiting upload a new magnet swipe must be blocked,
            while a pending *failure* may be superseded.
  * FW-612  A calibration must not spuriously fail with "ambient temperature out
            of range" depending on the calibrator's 1-wire orientation.
  * FW-613  The UI LED must reflect the real calibration result.
  * FW-614  A failure must report which fault occurred (not collapse unrelated
            faults onto one code) and carry the measurement that produced it
            plus both ambient temperatures, on serial and in the portal.

Run with `-s` so the operator prompts are visible and stdin works:

    pytest tests/hil/test_calibration.py -v -s \
      --coiote-config /path/to/etc-tools/coiote_api/config.json \
      --coiote-device urn:dev:mac:XXXXXXXXXXXXXXXX

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend these tests read):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

overlay-hil.conf enables `magnet swipe` / `magnet sample` / `magnet status` and
`calibration init` / `calibration run` / `calibration abort` (drive
etc_calibration directly for deterministic, cloud-independent states).

The operator's time is the scarce resource here, so the expensive physical steps
are shared. Each `*_run` fixture performs one calibration (or one held-ownership
window) and caches what it observed; the tests below assert against that cache,
so a single run backs several independent checks and each still passes or fails
on its own. **The order of the tests in this file is load-bearing**: pytest
collects in definition order, and the tests are grouped so the operator sets up
each physical configuration once, in sequence:

    1. calibrator disconnected
    2. calibrator attached in the previously-failing pairing
    3. calibrator attached in a good pairing
    4. calibrator rigged to fail
    5. calibrator removed, normal probe attached

Selecting a single test (`-k`) still works: the run it needs is built lazily and
prompts for just that setup.

The waits are driven by the device's log output (see `_wait_for`), not fixed
wall-clock windows, so a passing run finishes as soon as the log lands; the
timeouts only bound a regression so it fails fast instead of hanging the suite.

Coiote creds are optional: the portal cross-checks skip without them, the LED
and serial-log assertions still run. The portal checks need object 48939 at
ObjectVersion 1.2 in the Coiote data model; against an older DDF the newer
resources are simply absent and those assertions report that rather than a
device fault. Any Coiote tasks created are left in place (no teardown) to
preserve the audit trail.
"""

import math
import re
import time
from dataclasses import dataclass, field
from typing import Optional

import pytest

# --- Object 48939 "EXACT Calibration Status" (device -> portal) --------------
# Named DDF keys, not raw paths; must match the <Name> elements in
# exact-calibration-status_48939.xml.
STATUS_KEY = "EXACT Calibration Status.0.Status"
RESULT_KEY = "EXACT Calibration Status.0.Result"
ERROR_DETAIL_KEY = "EXACT Calibration Status.0.Error detail"
CALIBRATOR_AMBIENT_KEY = "EXACT Calibration Status.0.Calibrator ambient temperature"
DEVICE_AMBIENT_KEY = "EXACT Calibration Status.0.Device ambient temperature"
# --- Object 48950 "EXACT Calibration", instance 0 = user, 1 = factory --------
USER_OFFSET_KEY = "EXACT Calibration.0.Offset"
USER_HIGH_KEY = "EXACT Calibration.0.High"
USER_REF_KEY = "EXACT Calibration.0.Reference"

# enum etc_sensor_calibration_result (etc_calibration.h). Keep in lockstep with
# that enum and with exact-calibration-status_48939.xml.
RESULT_SUCCESS = "1"
RESULT_NAMES = {
    "0": "NO_STATUS", "1": "SUCCESS", "2": "RESERVED",
    "3": "AMBIENT_TEMP_OUT_OF_RANGE", "4": "CALIB_FAIL", "5": "BATTERY_LOW",
    "6": "CALIBRATOR_AMBIENT_SENSOR_FAIL", "7": "DEVICE_AMBIENT_OUT_OF_RANGE",
    "8": "CALIBRATOR_OFFSET_OUT_OF_RANGE", "9": "CALIBRATOR_HIGH_OUT_OF_RANGE",
    "10": "CALIBRATOR_SWITCH_FAIL", "11": "CALIBRATOR_NOT_FOUND",
    "12": "CALIBRATOR_ID_INVALID", "13": "SETTINGS_WRITE_FAIL", "14": "TIMEOUT",
}

# Every failure path funnels through calibration_fail(), which emits one uniform
# line. Matching on the code rather than the prose survives message rewording.
CALIB_FAIL_RE = re.compile(r"Calibration failed \(result (\d+)\): (.*)")

# Result codes for the two ambient gates.
RESULT_CALIBRATOR_AMBIENT_OUT_OF_RANGE = 3
RESULT_CALIB_FAIL = 4
RESULT_CALIBRATOR_AMBIENT_SENSOR_FAIL = 6
RESULT_DEVICE_AMBIENT_OUT_OF_RANGE = 7
RESULT_CALIBRATOR_NOT_FOUND = 11
RESULT_CALIBRATOR_ID_INVALID = 12

# With no usable calibrator the run must stop at one of these two, depending on
# what else sits on the 1-wire bus: NOT_FOUND when the scan finds nothing at all,
# ID_INVALID when it finds a device (a TMP1826 probe, say) whose serial number is
# not a calibrator's. Either is a correct, specific report; the regression this
# guards is setting no result at all.
NO_USABLE_CALIBRATOR_RESULTS = frozenset({
    RESULT_CALIBRATOR_NOT_FOUND,
    RESULT_CALIBRATOR_ID_INVALID,
})

# Any ambient-related failure. There is no device-ambient-unreadable code: that
# is reported as -273.15 in 48939/0/9 rather than failing the run.
AMBIENT_RESULTS = frozenset({
    RESULT_CALIBRATOR_AMBIENT_OUT_OF_RANGE,
    RESULT_CALIBRATOR_AMBIENT_SENSOR_FAIL,
    RESULT_DEVICE_AMBIENT_OUT_OF_RANGE,
})

# Serial-log fragments. Sources are named so a log-text change is traceable back
# to the code that emits it.
BUSY_LOG = "Calibration busy"                        # FW-611 re-entry block
LED_UPDATE_RE = re.compile(r"led update:\s*(LED_STATE_\w+)")
CALIB_DONE_LOG = "Calibration run complete"          # etc_calibration.c (both paths)
# The firmware appends the run's return code: "Calibration run complete (rc 0)"
# is a clean success; any other rc is a failure (or a run that never adjusted).
CALIB_DONE_RE = re.compile(r"Calibration run complete \(rc (-?\d+)\)")
NO_CALIBRATOR_LOG = "SCAN no calibrator"             # etc_calibration.c scan
# Terminal calibration LED states (ui_module.c "led update:" lines).
CALIB_TERMINAL_RE = (
    r"LED_STATE_CALIBRATION_(?:SUCCESS|MEASUREMENT_FAIL|BATTERY_LOW|TIMEOUT)")
CALIB_ACCEPTED_LOG = "LED_STATE_CALIBRATION_IN_PROCESS"  # a swipe was accepted
# app_module.c FW-492 gate: a magnet swipe within the analog-rail settling window
# (VSEN_MIN_OFF_MS = 2 s after the rail was last powered down) is dropped before
# it reaches the calibration path, so it neither blocks nor starts a calibration.
RAIL_SETTLING_LOG = "analog rail still settling"
ACQUIRING_LOG = "SENSOR_EVT_ENVIRONMENTAL_AQUIRING"  # sensor_module.c
DATA_READY_LOG = "SENSOR_EVT_ENVIRONMENTAL_DATA_READY"  # a completed acquisition
ADC_LOG = r"ADC\[\d\]"                               # etc_sensor.c read_analog_sample()
SKIP_LOG = "skipping acquisition"                    # etc_sensor.c run_acquisition()
RX_OFF_LOG = "CLOUD_EVT_RX_OFF"                      # cloud module
LTE_DISCONNECTED_LOG = "MODEM_EVT_LTE_DISCONNECTED"  # modem_module.c

# Bounded fallbacks; a wait returns the instant its sentinel lands (see
# _wait_for), so these only cap how long we wait before declaring a regression.
DECISION_TIMEOUT_S = 15
# A magnet-swipe calibration runs the full ambient gate + ADC sweep; generous
# cap, but it returns as soon as a terminal LED state appears.
SWIPE_TIMEOUT_S = 40
BOOT_TIMEOUT_S = 15
SETTLE_TIMEOUT_S = 12

# Outside this the ambient resource carried garbage rather than a temperature. A
# genuine no-reading arrives as NaN and is checked separately.
PLAUSIBLE_MIN_C = -40.0
PLAUSIBLE_MAX_C = 85.0
# Two sensors in the same room. Generous: the board self-heats, so this only
# catches a grossly wrong reading.
AMBIENT_AGREEMENT_C = 10.0


# --------------------------------------------------------------------------- #
# Serial helpers
# --------------------------------------------------------------------------- #
def _shell(dut, cmd):
    dut.write(f"{cmd}\r\n".encode())


def _wait_for(dut, pattern, timeout):
    """Read serial output until `pattern` (a regex) appears, then return at once.

    Driven by the device's response, not a fixed wall-clock wait: a passing check
    costs the few hundred ms until the log lands, and `timeout` only bounds a
    regression. Returns (matched, text) so callers can still assert on absence
    over the same window."""
    deadline = time.time() + timeout
    rx = re.compile(pattern)
    buf = ""
    while time.time() < deadline:
        try:
            m = dut.expect(r".+", timeout=0.5)
            s = m.group(0)
            if isinstance(s, bytes):
                s = s.decode("utf-8", "replace")
            buf += s + "\n"
            if rx.search(buf):
                return True, buf
        except Exception:
            pass
    return False, buf


def _drain(dut, seconds=2):
    """Discard whatever the device emits for `seconds` (clears shell echo and
    backlog before an assertion window)."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            dut.expect(r".+", timeout=0.3)
        except Exception:
            pass


def _abort(dut, seconds=2):
    """Clear any calibration left pending upload and release the front-end.

    The shell calibration path leaves the module at DATA_UPLOAD with its result,
    which the FW-611 re-entry guard treats as busy; without this the next magnet
    swipe is blocked and no fresh calibration runs."""
    _shell(dut, "calibration abort")
    _drain(dut, seconds)


def _run_calibration_via_shell(dut):
    """Drive one calibration through the shell (init scans + selects the 1-wire
    port; run does the ambient gate, ADC sweep and post-adjustment check).
    Returns (rc, out): `rc` is the firmware run return code parsed from
    'Calibration run complete (rc N)' (0 = clean success, non-zero = failure,
    None if the marker was not seen), and `out` is the combined serial output.
    Leaves the module at DATA_UPLOAD with the run's result (the shell path does
    not go through the data module, so the result is not cleared)."""
    _shell(dut, "calibration init")
    _, out = _wait_for(dut, NO_CALIBRATOR_LOG, 2)
    _shell(dut, "calibration run")
    _, more = _wait_for(dut, CALIB_DONE_LOG, DECISION_TIMEOUT_S)
    text = out + "\n" + more
    m = CALIB_DONE_RE.search(text)
    rc = int(m.group(1)) if m else None
    return rc, text


def _swipe_until_decided(dut, timeout):
    """Fire `magnet swipe` until it clears the FW-492 rail-settling gate and the
    calibration re-entry path actually decides.

    Right after a shell calibration the analog rail is inside its settling window
    (VSEN_MIN_OFF_MS, 2 s), so a single swipe is dropped as a plain data-get
    before it can be blocked (`Calibration busy`) or accepted
    (`LED_STATE_CALIBRATION_IN_PROCESS`). Re-fire, letting the rail settle between
    attempts, until one of those decisions lands. Returns the combined output."""
    decided = f"{BUSY_LOG}|{CALIB_ACCEPTED_LOG}"
    deadline = time.time() + timeout
    buf = ""
    while time.time() < deadline:
        _shell(dut, "magnet swipe")
        _, chunk = _wait_for(dut, f"{decided}|{RAIL_SETTLING_LOG}", DECISION_TIMEOUT_S)
        buf += chunk
        if re.search(decided, chunk):
            return buf
        _drain(dut, 2)  # outlast the rail-settling window, then swipe again
    return buf


def _calib_failures(text):
    """Every calibration failure in `text`, as a list of (result_code, detail).

    Empty when the run(s) succeeded."""
    return [(int(code), detail.strip())
            for code, detail in CALIB_FAIL_RE.findall(text)]


def _failed_with(text, results):
    """True when any calibration failure in `text` has a result code in
    `results` (an int or an iterable of ints)."""
    if isinstance(results, int):
        results = (results,)
    return any(code in results for code, _ in _calib_failures(text))


def _describe_failures(text):
    """Human-readable summary of the failures in `text`, for assertion output."""
    failures = _calib_failures(text)
    if not failures:
        return "no calibration failure reported"
    return "; ".join(
        f"{RESULT_NAMES.get(str(code), code)}({code}): {detail}"
        for code, detail in failures)


def _leds_in(text):
    return LED_UPDATE_RE.findall(text)


def _as_float(value):
    """Parse a portal value as a float, or None if it is absent/not numeric.

    Coiote renders an unreadable sensor as the string 'NaN', which float()
    accepts and turns into nan. That is a *reported* no-reading and is distinct
    from None, which means the resource never arrived at all. Keep them apart:
    the first is a valid outcome, the second is a failure."""
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


# --------------------------------------------------------------------------- #
# Coiote helper
# --------------------------------------------------------------------------- #
def _await_pushed_multi(coiote, dut, device, wants, timeout):
    """Nudge the device to send and poll several cached values until each is
    accepted, or the timeout expires.

    `wants` maps a DDF key to an accept(value) predicate. Every key is read
    within the same check-in cycle, so one calibration's worth of resources
    costs a single poll loop instead of one per resource. Returns {key: value},
    with the last value seen (None if it never arrived) for a key that never
    satisfied its predicate."""
    from coiote_client import CoioteError

    values = {key: None for key in wants}
    pending = set(wants)
    deadline = time.monotonic() + timeout
    while pending and time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")
        time.sleep(20)
        for key in sorted(pending):
            try:
                value = str(coiote.read_cached(device, key)[0])
            except CoioteError:
                continue
            values[key] = value
            print(f"  [coiote] {key} = {value!r}")
            if wants[key](value):
                pending.discard(key)
    return values


# --------------------------------------------------------------------------- #
# Operator-setup helpers
# --------------------------------------------------------------------------- #
@pytest.fixture(scope="session")
def calibrator_state():
    """Remembers the physical calibrator configuration currently set up so that
    consecutive tests needing the same setup do not re-prompt the operator."""
    return {"setup": None}


def _ensure_setup(operator, state, key, message):
    """Prompt the operator to (re)configure the calibrator only when the required
    physical setup differs from the one already in place. Returns the operator's
    reply ('skip' to skip the test), or '' when the existing setup is reused."""
    if state["setup"] == key:
        print(f"\n  [setup] calibrator already in '{key}' configuration; reusing it")
        return ""
    reply = operator.prompt(message)
    if reply != "skip":
        state["setup"] = key
    return reply


def _observe_led(operator, phase):
    resp = operator.ask(
        f"Watch the logger LED during/after the {phase} calibration. What was "
        "the FINAL colour? (green = success, red = fail, other/none)")
    return resp


def _boot_calibration_values(dut):
    """Reboot and return (offset, high, ref) parsed from the boot-time
    'Calibration value <o> <h> <r>' log emitted by the runtime loader, or None if
    the line was not observed (log level too low)."""
    dut.reboot()
    _, out = _wait_for(dut, r"Calibration value\s+[-\d.]+", BOOT_TIMEOUT_S)
    m = re.search(r"Calibration value\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)", out)
    if not m:
        return None
    return tuple(float(g) for g in m.groups())


# --------------------------------------------------------------------------- #
# Shared runs
#
# Each fixture performs one expensive physical step once and records what it
# saw. Fixtures never raise: a setup skip or an infrastructure problem is stored
# on the record and reported once per dependent test by _require(), so one bad
# run yields one clear line per test instead of a wall of fixture errors.
# --------------------------------------------------------------------------- #
@dataclass
class CalibrationRun:
    """One calibration, plus whatever was observed around it."""
    rc: Optional[int] = None        # 'Calibration run complete (rc N)'
    out: str = ""                   # serial text of the calibration itself
    failures: list = field(default_factory=list)  # (code, detail) pairs in `out`
    swipe_out: str = ""             # serial text of a follow-up magnet swipe
    swipe_failures: list = field(default_factory=list)
    busy_seen: bool = False         # the follow-up swipe was blocked
    accepted: bool = False          # the follow-up swipe was accepted
    failed: bool = False            # a failure was genuinely induced
    led: str = ""                   # operator's LED observation
    portal: Optional[dict] = None   # DDF key -> value; None without Coiote creds
    boot: Optional[tuple] = None    # (offset, high, ref) loaded after a reboot
    skip_reason: Optional[str] = None
    error: Optional[str] = None


@dataclass
class HoldRun:
    """One window in which calibration deterministically owns the front-end."""
    log: str = ""                   # everything read inside the window
    skip_matched: bool = False      # a sample was skipped for ownership
    report_ok: bool = False         # 'calibration report' still answers
    report_out: str = ""
    status_alive: bool = False      # 'magnet status' still answers
    status_out: str = ""
    skip_reason: Optional[str] = None
    error: Optional[str] = None


def _require(record, *, portal=False):
    """Gate a test on the shared run it consumes."""
    if record.error:
        pytest.fail(f"prerequisite run failed: {record.error}")
    if record.skip_reason:
        pytest.skip(record.skip_reason)
    if portal and record.portal is None:
        pytest.skip("no Coiote creds; portal cross-check skipped")


@pytest.fixture(scope="session")
def runs_cache():
    """The shared runs, built once per session. Holds plain data only, never a
    `dut` handle, so a per-test dut is harmless."""
    return {}


@pytest.fixture
def shell_pass_run(dut, operator, calibrator_state, runs_cache):
    """One shell calibration in a good setup, plus the re-swipe it must block.

    Serves FW-612 (good orientation), FW-614 (a clean run reports no ambient
    fault) and the FW-611 re-entry guard: the shell path leaves the result
    pending at DATA_UPLOAD, which is exactly the state that must block a swipe.
    """
    if "shell_pass" in runs_cache:
        return runs_cache["shell_pass"]

    rec = CalibrationRun()
    runs_cache["shell_pass"] = rec
    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "Connect the calibrator to the logger in a NORMALLY-GOOD pairing (e.g. "
        "calibrator port 1 -> logger port 1) and make sure the room/ambient "
        "temperature is within 18-28 C. A shell calibration will run and should "
        "SUCCEED.")
    if reply == "skip":
        rec.skip_reason = "operator skipped the passing calibration setup"
        return rec

    _abort(dut)
    rec.rc, rec.out = _run_calibration_via_shell(dut)
    rec.failures = _calib_failures(rec.out)

    # A pending SUCCESS must block the next swipe; only meaningful once the run
    # has actually left a success pending.
    if rec.rc == 0:
        rec.swipe_out = _swipe_until_decided(dut, SWIPE_TIMEOUT_S)
        rec.busy_seen = BUSY_LOG in rec.swipe_out

    # Clear the pending result so it cannot block the next swipe.
    _abort(dut)
    return rec


@pytest.fixture
def passing_swipe_run(dut, operator, calibrator_state, coiote_optional, request,
                      runs_cache, add_dut_methods, shell_pass_run):
    """One magnet-swipe calibration in the good setup: the full data-module path.

    Serves FW-613 (green LED), FW-611 (result and offset uploaded), FW-614 (both
    ambient resources) and FW-588 (the values survive a reboot). Every portal
    resource is collected in one check-in loop, and the reboot -- the single
    slowest step in the suite -- happens once.

    Depends on shell_pass_run so the "pass" setup is already in place and no
    result is left pending.
    """
    if "pass_swipe" in runs_cache:
        return runs_cache["pass_swipe"]

    rec = CalibrationRun()
    runs_cache["pass_swipe"] = rec
    if shell_pass_run.skip_reason:
        rec.skip_reason = shell_pass_run.skip_reason
        return rec

    _drain(dut, 2)
    _shell(dut, "magnet swipe")
    _, rec.out = _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)
    rec.failures = _calib_failures(rec.out)
    rec.led = _observe_led(operator, "PASS")

    if coiote_optional is not None:
        device = request.config.getoption("--coiote-device")
        op_timeout = request.config.getoption("--coiote-op-timeout")
        rec.portal = _await_pushed_multi(coiote_optional, dut, device, {
            RESULT_KEY: lambda v: v not in (None, "", "0"),
            USER_OFFSET_KEY: lambda v: v not in (None, ""),
            CALIBRATOR_AMBIENT_KEY: lambda v: _as_float(v) is not None,
            DEVICE_AMBIENT_KEY: lambda v: _as_float(v) is not None,
        }, op_timeout)

    # FW-588: the reboot must reload the coefficients this run just wrote. Only
    # meaningful after a run that actually adjusted them.
    if not rec.failures:
        try:
            rec.boot = _boot_calibration_values(dut)
        except Exception as e:
            rec.error = f"reboot after the calibration failed: {e}"
    return rec


@pytest.fixture
def held_calibration_run(dut, operator, calibrator_state, runs_cache):
    """One window in which calibration owns the analog front-end (FW-588).

    `calibration init` enters calibration, powers the rail and claims HW
    ownership synchronously, then waits up to CONFIG_CALIBRATION_TIMEOUT (30 s)
    for `calibration run`. That gap is a stable window in which every sensor
    acquisition must be skipped - far more deterministic than racing a sample
    against the brief `calibration run` measurement, which often lands in the
    post-run rail-settling window instead.

    All concurrency probes share the one window. The ownership-skip probe runs
    first, so a late expiry cannot invalidate it; the liveness readbacks that
    follow stay meaningful either way. Released with `calibration abort`, which
    drops ownership and resets the result without running a calibration.
    """
    if "held" in runs_cache:
        return runs_cache["held"]

    rec = HoldRun()
    runs_cache["held"] = rec
    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "Attach the calibrator to a sensor port (any working pairing). A held "
        "calibration will run while sensor samples are fired at it.")
    if reply == "skip":
        rec.skip_reason = "operator skipped the concurrency setup"
        return rec

    _abort(dut)
    _shell(dut, "calibration init")
    _, init_out = _wait_for(dut, NO_CALIBRATOR_LOG, 2)
    if NO_CALIBRATOR_LOG in init_out:
        rec.skip_reason = ("calibrator not detected by 'calibration init'; check "
                           "the connection and re-run")
        _abort(dut)
        return rec

    # Calibration now owns the front-end. A sample must be skipped, not run
    # against the calibrator. Retry: a sample can also be dropped by the FW-492
    # rail-settling gate before it reaches the ownership check.
    deadline = time.time() + DECISION_TIMEOUT_S
    while time.time() < deadline and not rec.skip_matched:
        _shell(dut, "magnet sample")
        rec.skip_matched, chunk = _wait_for(dut, SKIP_LOG, 5)
        rec.log += chunk

    # The calibration state must still be readable. Pre-fix, every accessor
    # queued behind the wedged sensor thread on the same mutex.
    _shell(dut, "calibration report")
    rec.report_ok, rec.report_out = _wait_for(dut, r"Ref", DECISION_TIMEOUT_S)

    # Both threads must still be alive afterwards.
    _shell(dut, "magnet status")
    rec.status_alive, rec.status_out = _wait_for(dut, r"settling=", DECISION_TIMEOUT_S)

    _abort(dut)
    return rec


@pytest.fixture
def failing_run(dut, operator, calibrator_state, coiote_optional, request,
                runs_cache):
    """One failing shell calibration and the magnet swipe that supersedes it.

    Serves FW-614 (result code + detail on serial and in the portal), FW-613
    (no green LED on a failure) and the FW-611 supersede case. These compose:
    the shell run leaves a *failure* pending, so the swipe that proves the
    supersede path is allowed is also the swipe that drives a real, uploaded
    failing calibration through the data module.
    """
    if "fail" in runs_cache:
        return runs_cache["fail"]

    rec = CalibrationRun()
    runs_cache["fail"] = rec
    reply = _ensure_setup(
        operator, calibrator_state, "fail",
        "Set up a calibration that will FAIL. Any of these will do:\n"
        "  - disconnect one calibrator probe lead,\n"
        "  - present an out-of-spec reference resistor,\n"
        "  - use a calibrator/orientation known to fail post-adjustment.\n"
        "A shell calibration will run, then a magnet swipe.")
    if reply == "skip":
        rec.skip_reason = "operator skipped the failing calibration setup"
        return rec

    _abort(dut)
    rec.rc, rec.out = _run_calibration_via_shell(dut)
    rec.failures = _calib_failures(rec.out)
    if not rec.failures and rec.rc in (0, None):
        rec.skip_reason = ("no calibration failure was induced; re-run and force "
                           "a failure")
        _abort(dut)
        return rec

    # A pending FAILURE is discardable, so this swipe must be allowed through -
    # and being allowed through, it runs the full data-module calibration whose
    # result and detail the portal checks below read.
    rec.swipe_out = _swipe_until_decided(dut, SWIPE_TIMEOUT_S)
    rec.busy_seen = BUSY_LOG in rec.swipe_out
    rec.accepted = CALIB_ACCEPTED_LOG in rec.swipe_out
    if rec.accepted:
        _, rest = _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)
        rec.swipe_out += "\n" + rest
    rec.swipe_failures = _calib_failures(rec.swipe_out)

    rec.failed = (bool(rec.swipe_failures)
                  or "LED_STATE_CALIBRATION_MEASUREMENT_FAIL" in rec.swipe_out
                  or "LED_STATE_CALIBRATION_BATTERY_LOW" in rec.swipe_out)
    if not rec.failed:
        rec.failed = operator.confirm(
            "The serial log did not clearly show a failure. Did the calibration "
            "actually FAIL this run?")
    if rec.failed:
        rec.led = _observe_led(operator, "FAIL")

    if coiote_optional is not None and rec.swipe_failures:
        device = request.config.getoption("--coiote-device")
        op_timeout = request.config.getoption("--coiote-op-timeout")
        rec.portal = _await_pushed_multi(coiote_optional, dut, device, {
            RESULT_KEY: lambda v: v not in (None, "", "0"),
            ERROR_DETAIL_KEY: lambda v: v not in (None, ""),
        }, op_timeout)

    _abort(dut)
    return rec


# --------------------------------------------------------------------------- #
# 1. Calibrator disconnected
# --------------------------------------------------------------------------- #
def test_fw614_missing_calibrator_is_its_own_code(dut, operator, calibrator_state):
    """No usable calibrator must report its own specific code, not a stale result
    from an earlier session.

    Pre-fix this pre-run check set no result at all, so app_module read whatever
    the previous calibration had left behind. Which of the two codes fires
    depends on the bench: leaving probes attached gives ID_INVALID rather than
    NOT_FOUND, because the scan finds a device but its serial number is not a
    calibrator's."""
    reply = _ensure_setup(
        operator, calibrator_state, "absent",
        "DISCONNECT the calibrator (leave any normal probes as they are), then a "
        "shell calibration will run and should refuse to calibrate.")
    if reply == "skip":
        pytest.skip("operator skipped the missing-calibrator case")

    _abort(dut)
    _shell(dut, "calibration init")
    _, out = _wait_for(dut, r"Calibration failed \(result \d+\)", DECISION_TIMEOUT_S)
    _abort(dut)

    assert _failed_with(out, NO_USABLE_CALIBRATOR_RESULTS), (
        f"FW-614: expected CALIBRATOR_NOT_FOUND({RESULT_CALIBRATOR_NOT_FOUND}) or "
        f"CALIBRATOR_ID_INVALID({RESULT_CALIBRATOR_ID_INVALID}) with no calibrator "
        f"attached, got: {_describe_failures(out)}\n"
        f"Serial tail:\n{out[-1200:]}")
    print(f"  [ok] {_describe_failures(out)}")


# --------------------------------------------------------------------------- #
# 2. Calibrator attached in the previously-failing pairing
# --------------------------------------------------------------------------- #
def test_fw612_no_ambient_error_in_failing_orientation(dut, operator,
                                                       calibrator_state):
    """A calibration in the previously-failing 1-wire pairing must still clear
    its ambient-temperature gate.

    Pre-fix this orientation failed deterministically: the 1-wire scan latched a
    port whose ROM did not match the muxed channel, so the calibrator's ambient
    sensor read as out of range."""
    reply = _ensure_setup(
        operator, calibrator_state, "orientation_fail",
        "Connect the calibrator to the logger in the PREVIOUSLY-FAILING pairing "
        "for this calibrator (e.g. calibrator port 3 -> logger port 1); use one "
        "you have seen fail before if you can.\n"
        "Make sure the room/ambient temperature is within 18-28 C.")
    if reply == "skip":
        pytest.skip("operator skipped the failing-orientation case")

    _abort(dut)
    _, out = _run_calibration_via_shell(dut)
    _abort(dut)

    assert not _failed_with(out, AMBIENT_RESULTS), (
        "FW-612 regression: spurious ambient-temperature failure in the "
        f"previously-failing orientation: {_describe_failures(out)}\n"
        f"Serial tail:\n{out[-1200:]}")
    print("  [ok] no ambient-temp error in the previously-failing orientation")


# --------------------------------------------------------------------------- #
# 3. Calibrator attached in a good pairing
# --------------------------------------------------------------------------- #
def test_fw612_no_ambient_error_in_good_orientation(shell_pass_run):
    """The same calibration in a known-good pairing must also clear the ambient
    gate, so FW-612 cannot be masked by only ever testing one orientation."""
    _require(shell_pass_run)
    assert not _failed_with(shell_pass_run.out, AMBIENT_RESULTS), (
        "FW-612 regression: spurious ambient-temperature failure in the "
        f"normally-good orientation: {_describe_failures(shell_pass_run.out)}\n"
        f"Serial tail:\n{shell_pass_run.out[-1200:]}")
    print("  [ok] no ambient-temp error in the normally-good orientation")


def test_fw614_no_ambient_fault_on_success(shell_pass_run):
    """A run that succeeds must not also report an ambient fault.

    Guards the ordering of the two ambient gates: recording a result without
    aborting would leave a failure attached to a run that returned rc=0."""
    _require(shell_pass_run)
    if shell_pass_run.rc != 0:
        pytest.skip("the calibration did not succeed; re-run with a good setup "
                    f"({_describe_failures(shell_pass_run.out)})")

    assert not _failed_with(shell_pass_run.out, AMBIENT_RESULTS), (
        "FW-614: a run that returned rc=0 still reported an ambient failure: "
        f"{_describe_failures(shell_pass_run.out)}")
    print("  [ok] clean run, no ambient fault reported")


def test_fw611_reentry_blocked_while_success_pending(shell_pass_run):
    """A magnet swipe while a SUCCESSFUL result is pending upload must be
    blocked, protecting the un-uploaded result from being overwritten."""
    _require(shell_pass_run)
    if shell_pass_run.rc != 0:
        pytest.skip(f"shell calibration did not cleanly succeed "
                    f"(rc={shell_pass_run.rc}); re-run with a good setup")

    assert shell_pass_run.busy_seen, (
        "FW-611: a magnet swipe while a SUCCESSFUL result is pending upload must "
        f"be blocked (expected '{BUSY_LOG}'). The un-uploaded result could "
        f"otherwise be overwritten.\nSerial tail:\n{shell_pass_run.swipe_out[-1000:]}")
    print("  [ok] pending success blocked the re-swipe")


def test_fw613_led_green_on_success(passing_swipe_run):
    """A successful calibration shows the green/success LED (and the portal
    records SUCCESS)."""
    _require(passing_swipe_run)
    assert passing_swipe_run.led.startswith("green"), (
        "FW-613: a passing calibration must show the GREEN/success LED, operator "
        f"reported {passing_swipe_run.led!r}. Serial LED states seen: "
        f"{_leds_in(passing_swipe_run.out)}")

    if passing_swipe_run.portal is None:
        print("  [skip] no Coiote creds; LED-only success check")
        return
    result = passing_swipe_run.portal[RESULT_KEY]
    assert result == RESULT_SUCCESS, (
        f"FW-613: LED was green but portal Result={result!r} "
        f"({RESULT_NAMES.get(result, '?')}); expected SUCCESS")


def test_fw611_result_and_offset_uploaded_to_portal(passing_swipe_run):
    """After a calibration, the status/result (48939) and the calibration values
    (48950) must appear in the portal."""
    _require(passing_swipe_run, portal=True)
    result = passing_swipe_run.portal[RESULT_KEY]
    assert result not in (None, "0"), (
        "FW-611: no calibration Result reached the portal (48939/0/Result "
        f"stayed {result!r})")
    print(f"  [ok] portal Result={result} ({RESULT_NAMES.get(result, '?')})")

    # On a success the calibration values object should be populated too.
    if result == RESULT_SUCCESS:
        offset = passing_swipe_run.portal[USER_OFFSET_KEY]
        assert offset not in (None, ""), (
            "FW-611: successful calibration but 48950/0/Offset (user values) "
            "did not reach the portal")


def test_fw614_ambient_resources_reach_portal(passing_swipe_run):
    """After a calibration, 48939/0/8 and /9 must carry both ambient readings.

    The calibrator's must be a real measurement; the device's may legitimately
    be NaN, since not every board has a working onboard ambient sensor."""
    _require(passing_swipe_run, portal=True)
    calibrator = _as_float(passing_swipe_run.portal[CALIBRATOR_AMBIENT_KEY])
    device_amb = _as_float(passing_swipe_run.portal[DEVICE_AMBIENT_KEY])

    assert calibrator is not None, (
        "FW-614: 48939/0/8 (calibrator ambient) did not reach the portal. If the "
        "resource is missing entirely, upload the ObjectVersion 1.2 DDF for "
        "48939 to Coiote.")
    assert device_amb is not None, (
        "FW-614: 48939/0/9 (device ambient) did not reach the portal. If the "
        "resource is missing entirely, upload the ObjectVersion 1.2 DDF for "
        "48939 to Coiote.")

    # The calibrator's sensor gates the run, so it must have been read.
    assert not math.isnan(calibrator), (
        "FW-614: calibrator ambient came back NaN, but the run reached the ADC "
        "sweep, which is only possible once its ambient gate passed")
    assert PLAUSIBLE_MIN_C <= calibrator <= PLAUSIBLE_MAX_C, (
        f"FW-614: calibrator ambient {calibrator} C is not a plausible reading")

    # The device ambient is optional hardware, so NaN is a legitimate outcome.
    # Only compare the two sensors when both read.
    if math.isnan(device_amb):
        print(f"  [ok] portal ambients: calibrator={calibrator:.2f} C, "
              f"device=NaN (no working onboard ambient sensor on this board)")
        return

    assert PLAUSIBLE_MIN_C <= device_amb <= PLAUSIBLE_MAX_C, (
        f"FW-614: device ambient {device_amb} C is not a plausible reading")

    assert abs(calibrator - device_amb) <= AMBIENT_AGREEMENT_C, (
        f"FW-614: the two ambient sensors disagree by "
        f"{abs(calibrator - device_amb):.2f} C (calibrator {calibrator:.2f}, "
        f"device {device_amb:.2f}). One of them is misreporting — which is "
        f"exactly what reporting them separately is meant to expose.")

    print(f"  [ok] portal ambients: calibrator={calibrator:.2f} C, "
          f"device={device_amb:.2f} C")


def test_fw588_user_calibration_survives_reboot(passing_swipe_run, operator):
    """After a successful user calibration, a reboot must load the USER
    coefficients (not the factory ones). Verified by comparing the boot-time
    loaded values to the portal's user calibration (48950/0); falls back to an
    operator accuracy check when Coiote is unavailable."""
    _require(passing_swipe_run)
    if passing_swipe_run.failures:
        pytest.skip("the calibration did not succeed; re-run with a good setup "
                    f"({_describe_failures(passing_swipe_run.out)})")
    loaded = passing_swipe_run.boot

    if passing_swipe_run.portal is None:
        if loaded is None:
            pytest.skip("no Coiote creds and boot 'Calibration value' log not "
                        "visible; verify accuracy manually")
        assert operator.confirm(
            f"After reboot the device loaded calibration offset/high/ref="
            f"{loaded}. Place the logger at a known reference temperature and "
            "confirm the reading is ACCURATE."), \
            "FW-588: operator reported inaccurate readings after reboot"
        return

    # The user instance (48950/0) mirrors what a correct loader must use.
    user_offset = passing_swipe_run.portal[USER_OFFSET_KEY]
    if user_offset in (None, ""):
        pytest.skip("user calibration (48950/0/Offset) not present in portal yet")
    if loaded is None:
        pytest.skip("boot 'Calibration value' log not visible at this log level; "
                    "cannot compare loaded vs user coefficients")

    # The loaded offset must match the freshly-written USER offset. Pre-fix the
    # loader fell through to the FACTORY values, so this would not match.
    assert abs(loaded[0] - float(user_offset)) < 1.0, (
        f"FW-588: after reboot the device loaded offset={loaded[0]} but the "
        f"user calibration offset is {user_offset}; the loader is not using the "
        "user coefficients")
    print(f"  [ok] boot loaded user offset {loaded[0]} ~= portal {user_offset}")


def test_sample_during_calibration_is_skipped_not_hung(held_calibration_run):
    """A sample requested while calibration owns the front-end must be skipped,
    and both threads must stay alive.

    The direct FW-588 reproducer. Pre-fix, the acquisition ran against the live
    calibrator (which pulls the ports low, so they were misdetected as 1-wire)
    and wedged on a transfer that never completed - still holding the lock."""
    _require(held_calibration_run)
    log = held_calibration_run.log

    assert held_calibration_run.skip_matched, (
        "the sample was never skipped for calibration ownership (expected "
        f"'{SKIP_LOG}'); it may be blocked before the acquisition starts:\n{log}")
    # The acquiring event is emitted just before the ownership gate rejects it.
    assert ACQUIRING_LOG in log, (
        f"sensor module never emitted the acquiring event:\n{log}")
    # Load-bearing: the sample must not have read the calibrator. Pre-fix this is
    # where ADC[0]/ADC[3] appeared with identical values.
    assert not re.search(ADC_LOG, log), (
        "the sensor sampled the analog front-end while calibration owned it; "
        f"the reading is the calibrator's reference network, not the probes:\n{log}")
    assert held_calibration_run.status_alive, (
        "sensor/app path is unresponsive after the collision:\n"
        f"{held_calibration_run.status_out}")


def test_data_module_survives_concurrent_sample(held_calibration_run):
    """The data module must not deadlock behind the sensor thread.

    Pre-fix, its status read blocked on the same mutex the wedged sensor thread
    held, so the send never happened and the device dropped off the network
    (CLOUD_EVT_RX_OFF then MODEM_EVT_LTE_DISCONNECTED). Rather than watching for
    that slow signature, this probes liveness directly: a wedged data/sensor path
    cannot answer an accessor."""
    _require(held_calibration_run)
    log = held_calibration_run.log

    assert not (RX_OFF_LOG in log and LTE_DISCONNECTED_LOG in log), (
        "observed the RX_OFF + LTE_DISCONNECTED signature of a wedged data "
        f"module:\n{log}")
    assert held_calibration_run.report_ok, (
        "calibration state unreadable after the collision - the data/sensor path "
        f"may be wedged on the front-end mutex:\n{held_calibration_run.report_out}")


# --------------------------------------------------------------------------- #
# 4. Calibrator rigged to fail
# --------------------------------------------------------------------------- #
def test_fw614_failure_reports_specific_code_and_detail(failing_run):
    """A failing calibration must report a specific result code together with the
    measurement that produced it.

    The operator induces any failure they can: the point is the *shape* of the
    report, not which fault fires."""
    _require(failing_run)
    assert failing_run.failures, (
        "FW-614: the run failed (rc %s) but reported no result code via "
        "calibration_fail(). Every failure path must record one.\n"
        "Serial tail:\n%s" % (failing_run.rc, failing_run.out[-1500:]))

    for code, detail in failing_run.failures:
        assert str(code) in RESULT_NAMES, (
            f"FW-614: unknown result code {code}; RESULT_NAMES here and "
            f"exact-calibration-status_48939.xml are out of step with "
            f"enum etc_sensor_calibration_result")
        assert detail, (
            f"FW-614: result {RESULT_NAMES[str(code)]}({code}) carried no "
            f"detail text; the code alone is not actionable in the field")
        print(f"  [ok] {RESULT_NAMES[str(code)]}({code}): {detail}")


def test_fw611_pending_failure_superseded_by_swipe(failing_run):
    """A magnet swipe while a FAILED result is pending must be allowed to
    supersede it: unlike a success, a failed result is discardable."""
    _require(failing_run)
    assert not failing_run.busy_seen, (
        "FW-611: a magnet swipe while a FAILED result is pending must be allowed "
        "to supersede it, but it was blocked ('Calibration busy').\n"
        f"Serial tail:\n{failing_run.swipe_out[-1000:]}")
    assert failing_run.accepted, (
        "FW-611: the swipe was neither blocked nor accepted; the calibration "
        f"never started.\nSerial tail:\n{failing_run.swipe_out[-1000:]}")
    print("  [ok] pending failure was superseded by the re-swipe")


def test_fw613_led_not_green_on_failure(failing_run):
    """A failed calibration must NOT show the green LED (the original bug showed
    green on a post-adjustment failure)."""
    _require(failing_run)
    if not failing_run.failed:
        pytest.skip("no calibration failure was induced; re-run and force a fail")

    assert not failing_run.led.startswith("green"), (
        "FW-613 regression: a FAILED calibration showed the GREEN/success LED. "
        f"Serial LED states seen: {_leds_in(failing_run.swipe_out)}")

    if failing_run.portal is None:
        print("  [skip] no Coiote creds; LED-only failure check")
        return
    result = failing_run.portal[RESULT_KEY]
    assert result not in (None, RESULT_SUCCESS), (
        f"FW-613: LED was not green but portal Result={result!r} "
        f"({RESULT_NAMES.get(result, '?')}); expected a failure code")


def test_fw614_error_detail_reaches_portal(failing_run):
    """A failed calibration must land a non-empty 48939/0/7 (Error detail) in the
    portal alongside its result code, and the two must agree."""
    _require(failing_run, portal=True)
    if not failing_run.swipe_failures:
        pytest.skip("no calibration failure was induced; re-run and force a fail")
    expected_code, expected_detail = failing_run.swipe_failures[-1]

    result = failing_run.portal[RESULT_KEY]
    assert result not in (None, "", "0"), (
        f"FW-614: the device reported "
        f"{RESULT_NAMES.get(str(expected_code), expected_code)} on serial but no "
        f"Result reached the portal (48939/0/Result stayed {result!r})")
    assert int(result) == expected_code, (
        f"FW-614: portal Result={result} disagrees with the serial log, which "
        f"reported {RESULT_NAMES.get(str(expected_code), expected_code)}"
        f"({expected_code})")

    detail = failing_run.portal[ERROR_DETAIL_KEY]
    assert detail not in (None, ""), (
        "FW-614: a failed calibration left 48939/0/7 (Error detail) empty. If "
        "the resource is missing entirely, upload the ObjectVersion 1.2 DDF for "
        "48939 to Coiote.")

    # The firmware truncates to 96 bytes; compare the portion that survived.
    assert expected_detail.startswith(detail) or detail.startswith(expected_detail), (
        f"FW-614: portal detail {detail!r} does not match the serial log's "
        f"{expected_detail!r}")

    print(f"  [ok] portal Result={result} "
          f"({RESULT_NAMES.get(result, '?')}), detail={detail!r}")


# --------------------------------------------------------------------------- #
# 5. Calibrator removed, normal probe attached
# --------------------------------------------------------------------------- #
def test_sample_after_calibration_reads_real_probes(dut, operator, calibrator_state,
                                                    runs_cache):
    """Once calibration releases the hardware, sampling must work normally.

    Confirms the ownership gate reopens: a permanently-stuck flag would silently
    stop all sensor readings, which is worse than the original hang."""
    if not runs_cache:
        # Selected on its own: nothing has held the front-end yet, so run one
        # calibration for this to be a release check at all.
        reply = _ensure_setup(
            operator, calibrator_state, "pass",
            "Attach the calibrator; a calibration will run before the probe check.")
        if reply == "skip":
            pytest.skip("operator skipped the post-calibration sampling case")
        _abort(dut)
        _run_calibration_via_shell(dut)
        _abort(dut)

    reply = _ensure_setup(
        operator, calibrator_state, "probe",
        "Remove the calibrator and attach a normal probe.")
    if reply == "skip":
        pytest.skip("operator skipped the post-calibration sampling case")
    _drain(dut, 1)

    # The rail must settle (FW-492, ~2 s) and ownership must be released. Wait
    # for the acquisition's *outcome* (a completed read vs. a skip), not just the
    # acquiring event, so a skip in progress is not mistaken for a success.
    acquired = False
    log = ""
    deadline = time.time() + 30
    while time.time() < deadline and not acquired:
        _shell(dut, "magnet sample")
        _, chunk = _wait_for(dut, f"{DATA_READY_LOG}|{SKIP_LOG}", SETTLE_TIMEOUT_S)
        log += chunk
        if DATA_READY_LOG in chunk and SKIP_LOG not in chunk:
            acquired = True

    assert acquired, (
        f"no normal acquisition after calibration released the hardware:\n{log}")
