"""HIL tests for FW-788 / FW-789: opportunistic LTE upload for LoRa loggers.

A LoRa logger that has lost LoRa connectivity accumulates unacknowledged
readings. FW-788 brings up LTE in addition to the normal LoRa attempt once there
are strictly more than CONFIG_ETC_APP_LTE_SYNC_NACK_THRESHOLD (default 4) unacked
readings at a transmit interval. FW-789 is the battery guard: an LTE bring-up is
attempted at most once per 4 x tx_interval (the floor), and that spacing doubles
on every consecutive failed bring-up up to a once-a-day ceiling, resetting to the
floor on the first success.

Driving the arm: the opportunistic arm lives in the RTC transmit-wake branches of
app_module. The plain `app_module trigger_tx` arms a NORMAL upload and does NOT
run the arm (so it cannot perturb the reclaim HIL suites); appending `lte` --
`app_module trigger_tx lte` -- runs the arm exactly as an RTC wake would.

Staging nacks: `record generate <n>` writes n unacknowledged readings directly to
flash without transmitting them, so they persist regardless of any relay (unlike
a real LoRa send, which a relay in range would ack away). The FW-788 sentinel
prints the count it observed, so no separate readout is needed (`record report`
cannot report it).

Determinism: the FW-789 backoff clock lives in retained RAM and survives resets,
so each test first clears it with the test-only `app_module lte_sync_reset`. With
the clock cleared, the first arm is always due; the tx interval is set large so a
natural transmit wake does not race the shell-driven arm.

Every assertion anchors on a FW-788/FW-789 log sentinel and returns the instant
it appears (or times out), never on a fixed capture window - the shell console
interleaves application logs continuously.

  FW-788: <n> unacked readings, adding LTE upload
  FW-789: LTE upload suppressed, backoff active (<n> unacked)
  FW-789: LTE bring-up failed, backoff now <s> s          (needs no LTE coverage)

Requirements:
  * Device in LoRa logger mode (`settings set_device 1`); skipped otherwise.
  * Build WITHOUT rtt.conf, WITH overlay-hil.conf (lte_sync_reset is test-only):

      west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
        -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"
"""

import time

import pytest

ETC_DEVICE_MODE_LORA_LOGGER = 1
NACK_THRESHOLD = 4

# Large interval so a natural transmit wake does not race the shell-driven arm.
# With the backoff cleared, the first arm is due regardless of the interval.
TX_INTERVAL_S = 900

CMD_TIMEOUT_S = 15
STAGE = NACK_THRESHOLD + 5      # readings to generate (comfortably above threshold)
STAGE_WAIT_S = 30               # record generate is async (~3 s per reading)
# The arm logs its sentinel synchronously while the command runs; the dispatch
# (which stamps the backoff clock) follows the JOB_BOTH sensor read (~3 s).
ARM_TIMEOUT_S = 10
DISPATCH_SETTLE_S = 6
# A negative check: how long to watch for a sentinel that must NOT appear.
ABSENT_WINDOW_S = 8

_FW788 = r'FW-788: (\d+) unacked readings, adding LTE upload'
_FW789_SUPPRESS = r'FW-789: LTE upload suppressed, backoff active \((\d+) unacked\)'
_FW789_FAIL = r'FW-789: LTE bring-up failed, backoff now (\d+) s'

_SETTING_PATTERNS = {
    'tx_interval': r'Tx interval in seconds (\d+)',
    'tx_delay': r'Tx delay in milisecond (\d+)',
}


# --------------------------------------------------------------------------- #
# Serial helpers - each read anchors on the command's own unique output
# --------------------------------------------------------------------------- #
def _grp(match, index):
    value = match.group(index)
    return value.decode('utf-8', 'replace') if isinstance(value, bytes) else value


def _expect_re(dut, pattern, timeout=CMD_TIMEOUT_S, what=None):
    try:
        return dut.expect(pattern, timeout=timeout)
    except Exception as exc:
        raise AssertionError(
            f"Timed out waiting for {what or pattern!r} ({timeout}s): {exc}") from None


def _expect_absent(dut, pattern, timeout, what=None):
    """Assert `pattern` does NOT appear within `timeout` seconds."""
    try:
        dut.expect(pattern, timeout=timeout)
    except Exception:
        return
    raise AssertionError(f"Unexpected {what or pattern!r} appeared within {timeout}s")


def _get_setting(dut, name):
    dut.write(f'settings get_{name}\r\n'.encode())
    m = _expect_re(dut, _SETTING_PATTERNS[name], what=f'settings get_{name}')
    return int(_grp(m, 1))


def _apply_setting(dut, name, value):
    dut.write(f'settings set_{name} {value}\r\n'.encode())
    time.sleep(0.5)
    got = _get_setting(dut, name)
    assert got == value, f"settings set_{name} {value} did not take (read back {got})"


def _set_and_restore(dut, request, name, value):
    original = _get_setting(dut, name)
    if original != value:
        request.addfinalizer(lambda: _apply_setting(dut, name, original))
    _apply_setting(dut, name, value)


def _device_mode(dut):
    dut.write(b'settings get_device\r\n')
    m = _expect_re(dut, r'Device mode (\d+)', what='settings get_device')
    return int(_grp(m, 1))


def _clean_records(dut):
    dut.write(b'record clean\r\n')
    time.sleep(1.5)


def _reset_backoff(dut):
    dut.write(b'app_module lte_sync_reset\r\n')
    _expect_re(dut, r'LTE backoff state reset', what='lte_sync_reset ack')


def _stage_nacks(dut, n):
    """Generate n persistent unacked readings (not transmitted, so relay-safe)."""
    dut.write(f'record generate {n}\r\n'.encode())
    time.sleep(STAGE_WAIT_S)


def _trigger(dut, lte=False):
    dut.write(b'app_module trigger_tx lte\r\n' if lte else b'app_module trigger_tx\r\n')


def _lora_logger_setup(dut, request):
    if _device_mode(dut) != ETC_DEVICE_MODE_LORA_LOGGER:
        pytest.skip("Not a LoRa logger; the opportunistic LTE upload only exists there")
    _set_and_restore(dut, request, 'tx_interval', TX_INTERVAL_S)
    _set_and_restore(dut, request, 'tx_delay', 0)
    _clean_records(dut)
    _reset_backoff(dut)
    request.addfinalizer(lambda: _clean_records(dut))
    request.addfinalizer(lambda: _reset_backoff(dut))


# --------------------------------------------------------------------------- #
# FW-788: threshold gating
# --------------------------------------------------------------------------- #
def test_below_threshold_no_lte_upload(dut, request):
    """With no accumulated nacks, the arm must NOT add an LTE upload."""
    _lora_logger_setup(dut, request)

    # A freshly cleaned logger has 0 unacked readings, below the threshold.
    _trigger(dut, lte=True)
    _expect_absent(dut, _FW788, ABSENT_WINDOW_S, what='FW-788 sentinel below threshold')


# --------------------------------------------------------------------------- #
# FW-788 + FW-789: above threshold uploads, then the floor suppresses the next
# --------------------------------------------------------------------------- #
def test_above_threshold_uploads_then_backoff_suppresses(dut, request):
    """> threshold nacks add an LTE upload; a second attempt inside the floor
    must be suppressed (FW-789)."""
    _lora_logger_setup(dut, request)
    _stage_nacks(dut, STAGE)

    _trigger(dut, lte=True)
    m = _expect_re(dut, _FW788, timeout=ARM_TIMEOUT_S, what='FW-788 sentinel above threshold')
    reported = int(_grp(m, 1))
    assert reported > NACK_THRESHOLD, \
        f"FW-788 fired with {reported} unacked, not above the threshold of {NACK_THRESHOLD}"

    # Let the dispatch stamp the backoff clock, then a second attempt inside the
    # floor must be suppressed, not re-uploaded.
    time.sleep(DISPATCH_SETTLE_S)
    _trigger(dut, lte=True)
    m = _expect_re(dut, _FW789_SUPPRESS, timeout=ARM_TIMEOUT_S,
                   what='FW-789 backoff-suppress sentinel')
    assert int(_grp(m, 1)) > NACK_THRESHOLD
    _expect_absent(dut, _FW788, ABSENT_WINDOW_S, what='second FW-788 inside the backoff floor')


# --------------------------------------------------------------------------- #
# FW-789: consecutive failures double the backoff (needs no LTE coverage)
# --------------------------------------------------------------------------- #
@pytest.mark.slow
def test_failed_bringups_grow_backoff(dut, request):
    """Each failed LTE bring-up doubles the backoff window ("double immediately").

    Requires a bench with no usable LTE so the bring-up times out; the failure
    sentinel prints the new window, so growth is asserted directly from the log.
    Slow, and skipped where LTE actually connects (the office bench)."""
    _set_and_restore(dut, request, 'tx_interval', 60)  # small floor (240 s) for the wait
    floor = 60 * 4
    if _device_mode(dut) != ETC_DEVICE_MODE_LORA_LOGGER:
        pytest.skip("Not a LoRa logger; the opportunistic LTE upload only exists there")
    _set_and_restore(dut, request, 'tx_delay', 0)
    _clean_records(dut)
    _reset_backoff(dut)
    request.addfinalizer(lambda: _reset_backoff(dut))
    request.addfinalizer(lambda: _clean_records(dut))
    _stage_nacks(dut, STAGE)

    _trigger(dut, lte=True)
    _expect_re(dut, _FW788, timeout=ARM_TIMEOUT_S, what='initial FW-788 sentinel')
    m = _expect_re(dut, _FW789_FAIL, timeout=300, what='first FW-789 failure sentinel')
    first_window = int(_grp(m, 1))
    assert first_window == floor * 2, \
        f"After one failure the window should double to {floor * 2} s, got {first_window}"

    time.sleep(first_window + 2)
    _trigger(dut, lte=True)
    _expect_re(dut, _FW788, timeout=ARM_TIMEOUT_S, what='second FW-788 sentinel')
    m = _expect_re(dut, _FW789_FAIL, timeout=300, what='second FW-789 failure sentinel')
    assert int(_grp(m, 1)) == floor * 4, \
        f"After two failures the window should be {floor * 4} s"
