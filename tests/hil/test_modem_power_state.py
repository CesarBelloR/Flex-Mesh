"""HIL tests for FW-1152: unified modem power state and bounded power down.

The app used to run pm_device_action_run() itself, racing the driver's own
view of the modem power state. A resume against a modem the driver believed
was already on failed with ``Can't resume device: -120``, and a wedged power
down was retried 10x for up to ~340 s per pass before the modem_enter_sleep()
assert rebooted the device (Memfault: 446 devices / 1848 traces). Power
transitions now go through the driver (MODEM_API_CMD_POWER_ON/OFF), which
folds -EALREADY to success and bounds the power down (~112 s worst case).

Sentinels (every wait anchors on one and returns the instant it appears; no
fixed-duration capture windows, and nothing anchors on the shell prompt):

  ``Modem power <FROM> -> <TO>``
      the driver's power state setter; states are OFF, WAITING_FOR_APP_RDY,
      ON, PSM_PENDING, PSM.
  ``Shell power on done (<rc>)`` / ``Shell power off done (<rc>)``
      logged by the modem module thread once it has processed a shell power
      request (the shell only submits an event; the transition itself runs on
      the module thread).
  ``app: <state>, driver: <state>, pm: <state>``
      reply of ``modem_module state`` - the three-views-agree cross-check.

Coverage gap - the PWRKEY fallback: ``AT+QPOWD unavailable, using PWRKEY``
has no fault injection hook, so no test here can reach it. It is verified
manually with a scripted GDB breakpoint over SWD that rewrites the QPOWD
return, the same technique test_bg95_connect_force_close.py (FW-1139)
documents in its docstring. Record the observed log chain there when running
it; until a fault-injection shell lands, that path is GDB-verified only.

Requirements:
  * Build with ``overlay-hil.conf`` (adds ``modem_module power``/``state``)
    and WITHOUT rtt.conf (it sets CONFIG_SHELL_LOG_BACKEND=n and hides every
    line this test reads):

      west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \\
        -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Run:
  pytest tests/hil/test_modem_power_state.py -v -s --port /dev/ttyACM0
"""

import re
import time

# Quick exchanges: a state reply, or a power request the module can answer
# without touching the modem.
CMD_TIMEOUT_S = 15

# Bounded power down: 30 s tx lock + 10 s QPOWD + 65 s POWERED DOWN wait +
# ~7 s probe/pulse = ~112 s worst case. The old behaviour was ~340 s per
# attempt, so 150 s separates the two cleanly.
POWER_DOWN_BOUND_S = 150

# Full bring-up: PWRKEY pulse, APP RDY wait and modem setup. Setup waits for
# network registration and retries up to 3 attempts, so a cold attach in a
# congested lab runs well past two minutes.
POWER_ON_TIMEOUT_S = 600

STATE_RE = re.compile(r"app: (\S+), driver: (\S+), pm: (\S+)")
DONE_ON_RE = re.compile(r"Shell power on done \((-?\d+)\)")
DONE_OFF_RE = re.compile(r"Shell power off done \((-?\d+)\)")
TO_ON_RE = re.compile(r"Modem power \S+ -> ON")
ON_TO_OFF_RE = re.compile(r"Modem power ON -> OFF")
ON_TO_OFF = "Modem power ON -> OFF"

# None of these may appear anywhere while the tests drive power transitions.
FORBIDDEN = (
    "Can't resume device: -120",
    "ASSERTION FAIL",
    ">>> ZEPHYR FATAL ERROR",
)


def _decode(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def _expect(dut, pattern, windows, timeout):
    """Wait for a sentinel, appending everything read (interleaved application
    logs included) to `windows` so the test can scan it afterwards."""
    match = dut.expect(pattern, timeout=timeout)
    windows.append(_decode(dut.pexpect_proc.before))
    windows.append(_decode(match.group(0)))
    return match


def _assert_clean(windows):
    text = "".join(windows)
    for bad in FORBIDDEN:
        assert bad not in text, f"'{bad}' seen during a power transition.\nOutput: {text}"


def _state(dut, windows):
    """Ask `modem_module state` and return (app, driver, pm). Doubles as the
    liveness probe: a wedged module thread or a rebooted device never answers."""
    dut.write(b"modem_module state\r\n")
    match = _expect(dut, STATE_RE, windows, timeout=CMD_TIMEOUT_S)
    return tuple(_decode(match.group(i)) for i in (1, 2, 3))


def _power(dut, direction, windows, timeout):
    """Issue a power request and wait for the module thread's done sentinel.
    Returns the rc the sentinel reported."""
    dut.write(b"modem_module power %s\r\n" % direction.encode())
    done_re = DONE_ON_RE if direction == "on" else DONE_OFF_RE
    match = _expect(dut, done_re, windows, timeout=timeout)
    return int(_decode(match.group(1)))


def _ensure_driver_on(dut, windows):
    """Bring the driver to ON, whatever state the previous test left."""
    if _state(dut, windows)[1] == "ON":
        return
    rc = _power(dut, "on", windows, timeout=POWER_ON_TIMEOUT_S)
    assert rc == 0, f"power on failed with {rc}"
    if _state(dut, windows)[1] != "ON":
        # A bring-up was already in flight (driver WAITING_FOR_APP_RDY), so
        # the request was a no-op; anchor on the driver reaching ON itself.
        _expect(dut, TO_ON_RE, windows, timeout=POWER_ON_TIMEOUT_S)
        assert _state(dut, windows)[1] == "ON"


def _ensure_driver_off(dut, windows):
    """Bring the driver to OFF, whatever state the previous test left."""
    if _state(dut, windows)[1] == "OFF":
        return
    rc = _power(dut, "off", windows, timeout=POWER_DOWN_BOUND_S)
    assert rc == 0, f"power off failed with {rc}"
    assert _state(dut, windows)[1] == "OFF"


def test_power_off_is_idempotent(dut):
    """A second power off against an already-off modem must be a clean no-op.

    The driver folds -EALREADY to success, so the tightened
    __ASSERT_NO_MSG(rc == 0) in modem_enter_sleep() must not fire and the
    module thread must stay responsive.
    """
    windows = []
    _ensure_driver_on(dut, windows)

    for _ in range(2):
        rc = _power(dut, "off", windows, timeout=POWER_DOWN_BOUND_S)
        assert rc == 0, f"power off reported {rc}, expected the -EALREADY fold to 0"

    app, driver, pm = _state(dut, windows)
    assert (app, driver, pm) == ("STATE_DISCONNECTED", "OFF", "suspended"), (
        f"views disagree after double power off: app={app} driver={driver} pm={pm}"
    )
    _assert_clean(windows)


def test_power_on_is_idempotent(dut):
    """A second power on against an already-on modem must be a clean no-op.

    This is the direct regression test for the fleet's
    'Can't resume device: -120' signature: the app resuming a modem the
    driver already considered on.
    """
    windows = []
    _ensure_driver_off(dut, windows)

    for _ in range(2):
        rc = _power(dut, "on", windows, timeout=POWER_ON_TIMEOUT_S)
        assert rc == 0, f"power on reported {rc}"

    app, driver, pm = _state(dut, windows)
    assert driver == "ON" and pm == "active", (
        f"views disagree after double power on: app={app} driver={driver} pm={pm}"
    )
    assert app in ("STATE_CONNECTING", "STATE_CONNECTED"), f"unexpected app state {app}"
    _assert_clean(windows)


def test_power_down_completes_within_the_bounded_window(dut):
    """The power down must land inside the new bounded worst case (~112 s).

    The old path could spend ~340 s per attempt (180 s lock wait + 10x10 s
    QPOWD retries + 60 s shutdown wait); anything under 150 s proves the
    bounded ladder is in charge.
    """
    windows = []
    _ensure_driver_on(dut, windows)

    started = time.monotonic()
    dut.write(b"modem_module power off\r\n")
    _expect(dut, ON_TO_OFF_RE, windows, timeout=POWER_DOWN_BOUND_S + 30)
    elapsed = time.monotonic() - started

    # Let the module thread finish before the next test drives it.
    match = _expect(dut, DONE_OFF_RE, windows, timeout=CMD_TIMEOUT_S)
    assert int(_decode(match.group(1))) == 0

    assert elapsed < POWER_DOWN_BOUND_S, (
        f"power down took {elapsed:.1f} s, over the {POWER_DOWN_BOUND_S} s bound"
    )
    _assert_clean(windows)


def test_power_cycle_reports_one_state_transition(dut):
    """One power off must produce exactly one ON -> OFF driver transition.

    Two transitions would mean the modem was powered down through two entry
    points at once (the old app-side/driver-side split).
    """
    setup_windows = []
    _ensure_driver_on(dut, setup_windows)
    _assert_clean(setup_windows)

    windows = []
    rc = _power(dut, "off", windows, timeout=POWER_DOWN_BOUND_S)
    assert rc == 0

    # Anchor the end of the observation on the state reply so everything the
    # power off logged is in `windows` before counting.
    assert _state(dut, windows)[1] == "OFF"

    transitions = "".join(windows).count(ON_TO_OFF)
    assert transitions == 1, (
        f"expected exactly one '{ON_TO_OFF}', saw {transitions}.\n"
        f"Output: {''.join(windows)}"
    )
    _assert_clean(windows)
