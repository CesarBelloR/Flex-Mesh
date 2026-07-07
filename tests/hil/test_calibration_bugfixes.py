"""Operator-attended HIL tests for the calibration bug fixes.

Covers, on real hardware with a physical calibrator:

  * FW-588  User calibration must survive a reboot (the runtime loader must use
            the user coefficients, not fall back to factory) so readings stay
            accurate after a (re)calibration.
  * FW-612  A calibration must not spuriously fail with "ambient temperature out
            of range" depending on the calibrator's 1-wire orientation.
  * FW-613  The UI LED must reflect the real calibration result (a failed run
            must not show the green/success LED).
  * FW-611  A calibration result must reach the portal, and while a *successful*
            result is still awaiting its upload a new magnet swipe must be
            blocked (a pending *failure* may be superseded).

The calibrator is physical hardware, so the tests prompt an `operator` to
attach/orient it, trigger calibrations and observe the LED. Run with `-s` so the
prompts are visible and stdin works:

    pytest tests/hil/test_calibration_bugfixes.py -v -s \
      --coiote-config /path/to/etc-tools/coiote_api/config.json \
      --coiote-device urn:dev:mac:XXXXXXXXXXXXXXXX

Firmware build (HIL shell hooks; NO rtt.conf so logs appear over USB):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

overlay-hil.conf enables `magnet swipe` (real magnet-swipe path through the data
module) and `calibration init` / `calibration run` / `calibration abort` (drive
etc_calibration directly for deterministic, cloud-independent states).

The tests are ordered so an operator sets up each physical configuration once and
consecutive tests reuse it (the calibrator_state tracker skips the repeat prompt).
Between tests `calibration abort` clears any result still pending its upload, so a
stale success from an earlier shell calibration cannot block the next magnet swipe.

Coiote creds are optional: the 48939/48950 portal cross-checks skip without them;
the LED and serial-log assertions still run. Any Coiote tasks created are left in
place (no teardown) to preserve the audit trail.
"""

import re
import time

import pytest

# --- Object 48939 "EXACT Calibration Status" (device -> portal) --------------
STATUS_KEY = "EXACT Calibration Status.0.Status"
RESULT_KEY = "EXACT Calibration Status.0.Result"
# --- Object 48950 "EXACT Calibration", instance 0 = user, 1 = factory --------
USER_OFFSET_KEY = "EXACT Calibration.0.Offset"
USER_HIGH_KEY = "EXACT Calibration.0.High"
USER_REF_KEY = "EXACT Calibration.0.Reference"

# enum etc_sensor_calibration_result (etc_calibration.h)
RESULT_SUCCESS = "1"
RESULT_NAMES = {
    "0": "NO_STATUS", "1": "SUCCESS", "2": "POST_ADJ_OUT_OF_RANGE",
    "3": "AMBIENT_TEMP_OUT_OF_RANGE", "4": "CALIB_FAIL", "5": "BATTERY_LOW",
}

# Serial-log fragments (from etc_calibration.c / app_module.c / ui_module.c).
AMBIENT_ERR_LOG = "current temperature is not in range"   # ambient gate failure
POST_ADJ_ERR_LOG = "is not valid range"                    # post-adjustment fail
BUSY_LOG = "Calibration busy"                              # FW-611 re-entry block
LED_UPDATE_RE = re.compile(r"led update:\s*(LED_STATE_\w+)")

CALIB_DONE_LOG = "Calibration run complete"          # etc_calibration.c (both paths)
# The firmware appends the run's return code: "Calibration run complete (rc 0)"
# is a clean success; any other rc is a failure (or a run that never adjusted).
CALIB_DONE_RE = re.compile(r"Calibration run complete \(rc (-?\d+)\)")
NO_CALIBRATOR_LOG = "No calibration tool is here"    # etc_calibration.c scan
# Terminal calibration LED states (ui_module.c "led update:" lines).
CALIB_TERMINAL_RE = (
    r"LED_STATE_CALIBRATION_(?:SUCCESS|MEASUREMENT_FAIL|BATTERY_LOW|TIMEOUT)")
CALIB_ACCEPTED_LOG = "LED_STATE_CALIBRATION_IN_PROCESS"  # a swipe was accepted

# Bounded fallbacks; a wait returns the instant its sentinel lands (see
# _wait_for), so these only cap how long we wait before declaring a regression.
DECISION_TIMEOUT_S = 15
# A magnet-swipe calibration runs the full ambient gate + ADC sweep; generous
# cap, but it returns as soon as a terminal LED state appears.
SWIPE_TIMEOUT_S = 40
BOOT_TIMEOUT_S = 15


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


def _run_calibration_via_shell(dut):
    """Drive one calibration through the shell (init scans + selects the 1-wire
    port; run does the ambient gate, ADC sweep and post-adjustment check).
    Returns (rc, out): `rc` is the firmware run return code parsed from
    'Calibration run complete (rc N)' (0 = clean success, non-zero = failure,
    None if the marker was not seen), and `out` is the combined serial output.
    Leaves the module at DATA_UPLOAD with the run's result (the shell path does
    not go through the data module, so the result is not cleared). Waits on the
    firmware's 'Calibration run complete' marker rather than a fixed window."""
    _shell(dut, "calibration init")
    _, out = _wait_for(dut, NO_CALIBRATOR_LOG, 2)
    _shell(dut, "calibration run")
    _, more = _wait_for(dut, CALIB_DONE_LOG, DECISION_TIMEOUT_S)
    text = out + "\n" + more
    m = CALIB_DONE_RE.search(text)
    rc = int(m.group(1)) if m else None
    return rc, text


def _leds_in(text):
    return LED_UPDATE_RE.findall(text)


# --------------------------------------------------------------------------- #
# State / operator-setup fixtures
# --------------------------------------------------------------------------- #
@pytest.fixture(autouse=True)
def _clean_calibration_state(dut):
    """Clear any pending calibration left by a prior test before each test.

    The shell calibration path leaves the module at DATA_UPLOAD/SUCCESS, which the
    FW-611 re-entry guard treats as busy; without a reset the next magnet swipe is
    blocked and no fresh calibration runs. `calibration abort` powers the front-end
    down, releases its ownership and resets status/result. This replaces the old
    per-test reboot (which was heavy-handed and, with the bare `kernel reboot`, a
    no-op)."""
    _shell(dut, "calibration abort")
    _drain(dut, 2)
    yield


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


# --------------------------------------------------------------------------- #
# Coiote helper
# --------------------------------------------------------------------------- #
def _await_pushed(coiote, dut, device, key, timeout, accept=None):
    """Nudge the device to send and poll a cached 48939/48950 value until it is
    present (and, if `accept` is given, until accept(value) is true). Returns the
    value or None on timeout."""
    from coiote_client import CoioteError

    deadline = time.monotonic() + timeout
    value = None
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")
        time.sleep(20)
        try:
            value = str(coiote.read_cached(device, key)[0])
        except CoioteError:
            continue
        print(f"  [coiote] {key} = {value!r}")
        if accept is None or accept(value):
            return value
    return value


# --------------------------------------------------------------------------- #
# FW-612 — ambient-temperature failure must not depend on calibrator orientation
# --------------------------------------------------------------------------- #
def test_fw612_ambient_error_not_orientation_dependent(dut, operator):
    """For each calibrator orientation the operator presents, a calibration must
    complete its ambient-temperature gate without the spurious
    'ambient temperature out of range' error. Pre-fix, a bad orientation failed
    deterministically (the 1-wire scan latched a port whose ROM did not match the
    muxed channel)."""
    orientations = [
        "a NORMALLY-GOOD pairing (e.g. calibrator port 1 -> logger port 1)",
        "the PREVIOUSLY-FAILING pairing for this calibrator (e.g. calibrator "
        "port 3 -> logger port 1); use one you have seen fail before if you can",
    ]
    tested = 0
    for desc in orientations:
        reply = operator.prompt(
            f"FW-612: connect the calibrator to the logger in {desc}.\n"
            "Make sure the room/ambient temperature is within 18-28 C.")
        if reply == "skip":
            continue
        _drain(dut, 2)
        _, out = _run_calibration_via_shell(dut)
        assert AMBIENT_ERR_LOG not in out, (
            f"FW-612 regression: spurious ambient-temperature failure in "
            f"orientation [{desc}].\nSerial tail:\n{out[-1200:]}")
        print(f"  [ok] no ambient-temp error in orientation: {desc}")
        tested += 1

    if tested == 0:
        pytest.skip("operator skipped all FW-612 orientations")


# --------------------------------------------------------------------------- #
# FW-613 — the LED must match the real calibration result
# --------------------------------------------------------------------------- #
def _observe_led(operator, phase):
    resp = operator.ask(
        f"Watch the logger LED during/after the {phase} calibration. What was "
        "the FINAL colour? (green = success, red = fail, other/none)")
    return resp


def test_fw613_led_matches_result_on_success(dut, operator, calibrator_state,
                                             coiote_optional, request):
    """A successful calibration shows the green/success LED (and the portal
    records SUCCESS)."""
    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "FW-613 (pass case): connect the calibrator correctly. A magnet swipe "
        "will run a calibration that should PASS.")
    if reply == "skip":
        pytest.skip("operator skipped FW-613 success case")

    _drain(dut, 2)
    _shell(dut, "magnet swipe")
    _, out = _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)
    led = _observe_led(operator, "PASS")

    assert led.startswith("green"), (
        "FW-613: a passing calibration must show the GREEN/success LED, operator "
        f"reported {led!r}. Serial LED states seen: {_leds_in(out)}")

    if coiote_optional is not None:
        device = request.config.getoption("--coiote-device")
        op_timeout = request.config.getoption("--coiote-op-timeout")
        result = _await_pushed(coiote_optional, dut, device, RESULT_KEY,
                               op_timeout, accept=lambda v: v not in (None, "0"))
        assert result == RESULT_SUCCESS, (
            f"FW-613: LED was green but portal Result={result!r} "
            f"({RESULT_NAMES.get(result, '?')}); expected SUCCESS")
    else:
        print("  [skip] no Coiote creds; LED-only success check")


# --------------------------------------------------------------------------- #
# FW-611 — result reaches the portal
# --------------------------------------------------------------------------- #
def test_fw611_result_uploaded_to_portal(dut, operator, calibrator_state, coiote,
                                         request):
    """After a calibration, the status/result (48939) and the calibration values
    (48950) must appear in the portal. Uses the `coiote` fixture, so it skips
    without creds."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "FW-611: connect the calibrator correctly, then a magnet swipe will run "
        "a calibration whose result should be uploaded to the portal.")
    if reply == "skip":
        pytest.skip("operator skipped FW-611 upload case")

    _drain(dut, 2)
    _shell(dut, "magnet swipe")
    _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)

    result = _await_pushed(coiote, dut, device, RESULT_KEY, op_timeout,
                           accept=lambda v: v not in (None, "0"))
    assert result not in (None, "0"), (
        "FW-611: no calibration Result reached the portal (48939/0/Result "
        f"stayed {result!r})")
    print(f"  [ok] portal Result={result} ({RESULT_NAMES.get(result, '?')})")

    # On a success the calibration values object should be populated too.
    if result == RESULT_SUCCESS:
        offset = _await_pushed(coiote, dut, device, USER_OFFSET_KEY, op_timeout,
                               accept=lambda v: v not in (None, ""))
        assert offset not in (None, ""), (
            "FW-611: successful calibration but 48950/0/Offset (user values) "
            "did not reach the portal")


# --------------------------------------------------------------------------- #
# FW-588 — user calibration survives a reboot (readings stay accurate)
# --------------------------------------------------------------------------- #
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


def test_fw588_user_calibration_survives_reboot(dut, operator, calibrator_state,
                                                coiote_optional, request):
    """After a successful user calibration, a reboot must load the USER
    coefficients (not the factory ones). Verified by comparing the boot-time
    loaded values to the portal's user calibration (48950/0) vs factory
    (48950/1); falls back to an operator accuracy check when Coiote is
    unavailable."""
    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "FW-588: connect the calibrator correctly. A magnet swipe will perform "
        "a user calibration that writes new coefficients; we then reboot and "
        "check they are what the device loads.")
    if reply == "skip":
        pytest.skip("operator skipped FW-588")

    _drain(dut, 2)
    _shell(dut, "magnet swipe")
    _, out = _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)
    if (AMBIENT_ERR_LOG in out) or (POST_ADJ_ERR_LOG in out):
        pytest.skip("the calibration did not succeed; re-run with a good setup")

    loaded = _boot_calibration_values(dut)

    if coiote_optional is None:
        if loaded is None:
            pytest.skip("no Coiote creds and boot 'Calibration value' log not "
                        "visible; verify accuracy manually")
        assert operator.confirm(
            f"After reboot the device loaded calibration offset/high/ref="
            f"{loaded}. Place the logger at a known reference temperature and "
            "confirm the reading is ACCURATE."), \
            "FW-588: operator reported inaccurate readings after reboot"
        return

    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    # The user instance (48950/0) mirrors what a correct loader must use.
    user_offset = _await_pushed(coiote_optional, dut, device, USER_OFFSET_KEY,
                                op_timeout, accept=lambda v: v not in (None, ""))
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


# --------------------------------------------------------------------------- #
# FW-611 — re-entry guard (pending success blocks, pending failure supersedes)
# --------------------------------------------------------------------------- #
def test_fw611_reentry_blocks_pending_success_supersedes_fail(dut, operator,
                                                              calibrator_state):
    """FW-611 re-entry guard, driven deterministically via the calibration shell
    (which leaves the result pending at DATA_UPLOAD because it does not go
    through the data-module ACK path):

      * after a SUCCESS is pending, a magnet swipe is blocked (-EBUSY, logs
        'Calibration busy'), protecting the un-uploaded result;
      * after a FAILURE is pending, a magnet swipe is allowed (no 'Calibration
        busy'), superseding the discardable result.
    """
    # --- pending SUCCESS must block a re-swipe -----------------------------
    reply = _ensure_setup(
        operator, calibrator_state, "pass",
        "FW-611 re-entry (block case): connect the calibrator correctly and "
        "ensure ambient is 18-28 C so the calibration SUCCEEDS.")
    if reply == "skip":
        pytest.skip("operator skipped FW-611 re-entry case")

    _drain(dut, 2)
    rc, out = _run_calibration_via_shell(dut)
    if rc != 0:
        pytest.skip(f"shell calibration did not cleanly succeed (rc={rc}); "
                    "re-run with a good setup")

    _shell(dut, "magnet swipe")
    _, swipe_out = _wait_for(dut, BUSY_LOG, DECISION_TIMEOUT_S)
    assert BUSY_LOG in swipe_out, (
        "FW-611: a magnet swipe while a SUCCESSFUL result is pending upload must "
        "be blocked (expected 'Calibration busy'). The un-uploaded result could "
        f"otherwise be overwritten.\nSerial tail:\n{swipe_out[-1000:]}")
    print("  [ok] pending success blocked the re-swipe")

    # --- pending FAILURE may be superseded ---------------------------------
    reply = _ensure_setup(
        operator, calibrator_state, "fail",
        "FW-611 re-entry (supersede case): now set up a calibration that will "
        "FAIL (e.g. disconnect a probe lead), so a failed result is left "
        "pending.")
    if reply == "skip":
        pytest.skip("operator skipped FW-611 supersede case (block case passed)")

    _drain(dut, 2)
    rc, out = _run_calibration_via_shell(dut)
    if rc == 0:
        pytest.skip("shell calibration did not fail; re-run and force a failure")

    _shell(dut, "magnet swipe")
    _, swipe_out = _wait_for(dut, f"{BUSY_LOG}|{CALIB_ACCEPTED_LOG}", DECISION_TIMEOUT_S)
    assert BUSY_LOG not in swipe_out, (
        "FW-611: a magnet swipe while a FAILED result is pending must be allowed "
        "to supersede it, but it was blocked ('Calibration busy').\n"
        f"Serial tail:\n{swipe_out[-1000:]}")
    print("  [ok] pending failure was superseded by the re-swipe")


# --------------------------------------------------------------------------- #
# FW-613 — a failed calibration must NOT show the green LED
# --------------------------------------------------------------------------- #
def test_fw613_led_matches_result_on_failure(dut, operator, calibrator_state,
                                             coiote_optional, request):
    """A failed calibration must NOT show the green LED (the original bug showed
    green on a post-adjustment failure). The operator induces a genuine failure;
    the test confirms one actually occurred before checking the LED."""
    reply = _ensure_setup(
        operator, calibrator_state, "fail",
        "FW-613 (fail case): set up a calibration that will FAIL. Options:\n"
        "  - disconnect one calibrator probe lead, or\n"
        "  - use a calibrator/orientation known to fail post-adjustment, or\n"
        "  - present an out-of-spec reference.\n"
        "A magnet swipe will run it.")
    if reply == "skip":
        pytest.skip("operator skipped FW-613 failure case")

    _drain(dut, 2)
    _shell(dut, "magnet swipe")
    _, out = _wait_for(dut, CALIB_TERMINAL_RE, SWIPE_TIMEOUT_S)

    failed = (AMBIENT_ERR_LOG in out) or (POST_ADJ_ERR_LOG in out) or \
             ("LED_STATE_CALIBRATION_MEASUREMENT_FAIL" in out) or \
             ("LED_STATE_CALIBRATION_BATTERY_LOW" in out)
    if not failed and not operator.confirm(
            "The serial log did not clearly show a failure. Did the calibration "
            "actually FAIL this run?"):
        pytest.skip("no calibration failure was induced; re-run and force a fail")

    led = _observe_led(operator, "FAIL")
    assert not led.startswith("green"), (
        "FW-613 regression: a FAILED calibration showed the GREEN/success LED. "
        f"Serial LED states seen: {_leds_in(out)}")

    if coiote_optional is not None:
        device = request.config.getoption("--coiote-device")
        op_timeout = request.config.getoption("--coiote-op-timeout")
        result = _await_pushed(coiote_optional, dut, device, RESULT_KEY,
                               op_timeout, accept=lambda v: v not in (None, "0"))
        assert result not in (None, RESULT_SUCCESS), (
            f"FW-613: LED was not green but portal Result={result!r} "
            f"({RESULT_NAMES.get(result, '?')}); expected a failure code")
