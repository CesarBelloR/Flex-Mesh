"""HIL tests for FW-1113: upload scheduling restructure (single dispatch point).

A single global used to conflate "why the next RTC alarm was scheduled" with "a
pending magnet-swipe upload request", was made sticky to paper over the clash,
and was destructively consumed at three sites. The restructure splits the state
(scheduled_tx_reason / pending_upload_reason), merges requests by rank
(MAGNET > LORA_SYNC > NORMAL > NONE) and dispatches at exactly one place, where
an empty request is a no-op so racing consumers collapse to exactly-once.

Scenarios covered (bug numbers from FW-1113):

  bug 1  - a LoRa sync-cloud wake must keep sub-job SYNC_CLOUD_LORA all the way
           to the cloud connect (no clobber to NORMAL mid-connect). Requires
           LoRa logger mode; skips otherwise.
  bug 2  - after a magnet upload completes, the next plain interval TX must not
           reuse the stale SYNC_MAGNET sub-job (no unintended cloud upload).
  magnet near a scheduled BOTH wake  - exactly one upload at magnet priority
           (the double-DATA_READY race the NONE-no-op exists for).
  magnet near a TX-only wake         - exactly one dispatch, no spurious plain
           transmit after the merged magnet sync.
  magnet during a LOG wake           - the request must ride the LOG wake's
           sample out instead of being dropped.

Observability is via the test-shell `magnet status` counters, and every read
anchors on that command's own unique output rather than on the shell prompt:
application logs are interleaved on the same console and constantly redraw the
prompt, so a prompt-anchored read returns before the command's output arrives.

  magnet status -> "settling=<yes|no> skips=<n> pending=<none|normal|lora|magnet>
                    dispatches=<n> subjob=<normal|lora|magnet>"

Timing (measured on an ETC Flex, 2026-07-21): `magnet swipe` injects the HALL
event directly, bypassing the 2 s GPIO debounce, so the upload request is armed
within ~0.5 s and the acquisition it triggers completes ~2.8 s after the swipe.
The race tests swipe SWIPE_LEAD_S before the RTC wake so the wake lands in
between: the request is already armed (~1 s of margin) while the sample is still
in flight (~1.5 s of margin). Swiping too early lets the sample finish first,
which is a legitimate two-upload sequence and not the race under test - hence
also the explicit wait for the analog rail to be settled, since a swipe inside
the settling window is skipped instantly and dispatches before the wake.

Build with CONFIG_ETC_INTERFACE_TEST_SHELL=y and the shell log backend on:

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"
"""

import calendar
import time

import pytest

# Bounded fallbacks; polls return the instant their condition holds, so these
# only cap how long we wait before declaring a regression.
DISPATCH_TIMEOUT_S = 25
WAKE_MARGIN_S = 20
CMD_TIMEOUT_S = 15

# Time from an injected swipe to the request being armed, and to its sample
# completing (measured on hardware; the shell injection skips the GPIO debounce).
ARM_S = 0.5
SAMPLE_DONE_S = 2.8
# Swipe this far ahead of an RTC wake so the wake falls between the two.
SWIPE_LEAD_S = 1.5

_STATUS_PATTERN = (r'settling=(yes|no)\s+skips=(\d+)\s+pending=(\w+)\s+'
                   r'dispatches=(\d+)\s+subjob=(\w+)')

_SETTING_PATTERNS = {
    'tx_interval': r'Tx interval in seconds (\d+)',
    'log_interval': r'Log interval in seconds (\d+)',
    'tx_delay': r'Tx delay in milisecond (\d+)',
}


# --------------------------------------------------------------------------- #
# Serial helpers - every read anchors on the command's own unique output
# --------------------------------------------------------------------------- #
def _grp(match, index):
    value = match.group(index)
    return value.decode('utf-8', 'replace') if isinstance(value, bytes) else value


def _expect_re(dut, pattern, timeout=CMD_TIMEOUT_S, what=None):
    """Wait for `pattern` in the device output, skipping interleaved log lines."""
    try:
        return dut.expect(pattern, timeout=timeout)
    except Exception as exc:
        raise AssertionError(
            f"Timed out waiting for {what or pattern!r} ({timeout}s): {exc}") from None


def _status(dut):
    """Return the parsed `magnet status` fields."""
    dut.write(b'magnet status\r\n')
    m = _expect_re(dut, _STATUS_PATTERN, what='magnet status output')
    return {
        'settling': _grp(m, 1) == 'yes',
        'skips': int(_grp(m, 2)),
        'pending': _grp(m, 3),
        'dispatches': int(_grp(m, 4)),
        'subjob': _grp(m, 5),
    }


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
    """Set a setting for the duration of the test, restoring it on teardown."""
    original = _get_setting(dut, name)
    if original != value:
        request.addfinalizer(lambda: _apply_setting(dut, name, original))
    _apply_setting(dut, name, value)


def _rtc_now(dut):
    """Return the device RTC time (UTC epoch seconds) via `date get`."""
    dut.write(b'date get\r\n')
    m = _expect_re(dut, r'(\d{4})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})\s+UTC',
                   what='date get output')
    parts = tuple(int(_grp(m, i)) for i in range(1, 7))
    return calendar.timegm(parts + (0, 0, 0))


def _sync_rtc(dut, timeout=8):
    """Poll `date get` until the RTC second ticks over.

    Returns (epoch, host_monotonic_at_tick) so the caller can align to a
    sub-second offset despite the RTC's one-second read resolution."""
    prev = _rtc_now(dut)
    deadline = time.time() + timeout
    while time.time() < deadline:
        now = _rtc_now(dut)
        if now != prev:
            return now, time.monotonic()
        prev = now
    return prev, time.monotonic()


def _sleep_until_second(dut, target_sec):
    """Block until the device RTC is `target_sec` seconds into some minute."""
    epoch, t_ref = _sync_rtc(dut)
    wait = (target_sec - (epoch % 60)) % 60
    if wait < 1.0:
        wait += 60
    remaining = wait - (time.monotonic() - t_ref)
    if remaining > 0:
        time.sleep(remaining)


def _drain(dut, seconds=2):
    """Discard whatever the device emits for `seconds`."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            dut.expect(r'.+', timeout=0.3)
        except Exception:
            pass


def _wait_dispatches(dut, at_least, timeout=DISPATCH_TIMEOUT_S):
    """Poll `magnet status` until dispatches >= at_least; return final status."""
    deadline = time.time() + timeout
    st = _status(dut)
    while st['dispatches'] < at_least and time.time() < deadline:
        time.sleep(0.5)
        st = _status(dut)
    assert st['dispatches'] >= at_least, \
        f"Timed out waiting for dispatch #{at_least} (status: {st})"
    return st


def _assert_no_extra_dispatch(dut, expected, seconds=10):
    """Over `seconds`, dispatches must stay at `expected` (exactly-once check)."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        time.sleep(1.5)
        st = _status(dut)
        assert st['dispatches'] == expected, \
            f"Extra dispatch detected: expected {expected}, status: {st}"


def _wait_ready_to_swipe(dut, timeout=120):
    """Wait until no request is pending and the analog rail has settled.

    A swipe inside the settling window is dropped instantly and dispatches
    before the RTC wake, which is not the overlap the race tests exercise."""
    deadline = time.time() + timeout
    st = _status(dut)
    while (st['pending'] != 'none' or st['settling']) and time.time() < deadline:
        time.sleep(1)
        st = _status(dut)
    assert st['pending'] == 'none' and not st['settling'], \
        f"Device never reached a clean pre-swipe state: {st}"
    return st


def _one_minute_wakes(dut, request, tx_delay_ms=0, tx_interval=60):
    """Configure short wakes so a test does not wait on the production schedule."""
    _set_and_restore(dut, request, 'tx_interval', tx_interval)
    _set_and_restore(dut, request, 'log_interval', 60)
    _set_and_restore(dut, request, 'tx_delay', tx_delay_ms)
    _drain(dut, 2)


# --------------------------------------------------------------------------- #
# bug 2 - stale SYNC_MAGNET sub-job must not leak into the next interval TX
# --------------------------------------------------------------------------- #
def test_bug2_no_stale_magnet_subjob_on_interval_tx(dut, request):
    """A magnet upload runs as SYNC_MAGNET; the next plain interval transmit
    must dispatch with sub-job NORMAL again (the old code never reset it on a
    TX_RX wake, causing unintended cloud uploads indefinitely)."""
    _one_minute_wakes(dut, request)
    base = _wait_ready_to_swipe(dut)['dispatches']

    # Magnet swipe: its sample (or a skipped sample) dispatches the magnet sync.
    dut.write(b'magnet swipe\r\n')
    st = _wait_dispatches(dut, base + 1)
    assert st['subjob'] == 'magnet', f"Magnet dispatch should set SYNC_MAGNET: {st}"
    assert st['pending'] == 'none', f"Request must be consumed by dispatch: {st}"

    # The next scheduled interval wake must dispatch as a plain NORMAL transmit.
    st = _wait_dispatches(dut, base + 2, timeout=60 + WAKE_MARGIN_S + DISPATCH_TIMEOUT_S)
    assert st['subjob'] == 'normal', \
        f"Interval TX after a magnet upload must reset the sub-job (bug 2): {st}"


# --------------------------------------------------------------------------- #
# magnet swipe just before a scheduled BOTH wake - exactly one upload
# --------------------------------------------------------------------------- #
def test_magnet_near_scheduled_both_wake_dispatches_once(dut, request):
    """A swipe just before a BOTH wake produces two samples and therefore two
    ready/skipped events racing for one merged request; exactly one upload must
    go out, at magnet priority (the scenario the NONE-no-op exists for)."""
    _one_minute_wakes(dut, request)
    base = _wait_ready_to_swipe(dut)['dispatches']

    # Swipe so the request is armed ~1 s before the :00 BOTH wake while its
    # own sample is still running when the wake fires.
    _sleep_until_second(dut, 60 - SWIPE_LEAD_S)
    dut.write(b'magnet swipe\r\n')

    st = _wait_dispatches(dut, base + 1)
    assert st['subjob'] == 'magnet', \
        f"The merged request must dispatch at magnet priority: {st}"
    assert st['pending'] == 'none', f"Request must be consumed: {st}"
    _assert_no_extra_dispatch(dut, base + 1)


# --------------------------------------------------------------------------- #
# magnet swipe just before a TX-only wake - merged sync, no spurious transmit
# --------------------------------------------------------------------------- #
def test_magnet_near_txrx_wake_dispatches_once(dut, request):
    """On a TX_RX wake the dispatch happens immediately (no sample to wait for);
    a swipe whose sample is still in flight must ride that dispatch, and its
    late sample must not produce a second, plain transmit."""
    # A 20 s tx delay splits the wake: alarm 2 (LOG) at :00, alarm 1 (TX_RX) at :20.
    _one_minute_wakes(dut, request, tx_delay_ms=20000)
    base = _wait_ready_to_swipe(dut)['dispatches']

    _sleep_until_second(dut, 20 - SWIPE_LEAD_S)
    dut.write(b'magnet swipe\r\n')

    st = _wait_dispatches(dut, base + 1)
    assert st['subjob'] == 'magnet', \
        f"TX_RX wake must dispatch the merged magnet request: {st}"
    # The swipe's late sample finds pending == NONE and must be a no-op.
    _assert_no_extra_dispatch(dut, base + 1)


# --------------------------------------------------------------------------- #
# magnet swipe riding a LOG wake - the request must still go out
# --------------------------------------------------------------------------- #
def test_magnet_during_log_wake_still_uploads(dut, request):
    """With the transmit alarm far away, a swipe landing around an alarm-2 LOG
    wake must still deliver its upload (the old job-based gate hit the LOG
    branch and powered peripherals off without ever dispatching)."""
    # LOG wakes every minute; the transmit alarm is a day out.
    _one_minute_wakes(dut, request, tx_interval=86400)
    base = _wait_ready_to_swipe(dut)['dispatches']

    _sleep_until_second(dut, 60 - SWIPE_LEAD_S)
    dut.write(b'magnet swipe\r\n')

    st = _wait_dispatches(dut, base + 1)
    assert st['subjob'] == 'magnet', \
        f"The LOG wake must still deliver the magnet request: {st}"
    _assert_no_extra_dispatch(dut, base + 1)


# --------------------------------------------------------------------------- #
# bug 1 - LoRa sync-cloud wake keeps its sub-job through the cloud connect
# --------------------------------------------------------------------------- #
ETC_DEVICE_MODE_LORA_LOGGER = 1
# How far before the sync wake to steer the clock, and how many times to retry
# if the device's network time re-sync reverts the steer first.
JUMP_LEAD_S = 30
JUMP_ATTEMPTS = 3


def _wait_modem_idle(dut, timeout=240):
    """Best-effort wait for the modem to enter PSM.

    A clock steer applied while the modem is connecting is overwritten within
    seconds by the network time it obtains; once the modem is idle the steer
    survives long enough (~70 s) to reach a wake scheduled JUMP_LEAD_S out."""
    try:
        dut.expect(r'Entering PSM', timeout=timeout)
    except Exception:
        print("  [warn] no PSM log seen; steering the clock anyway")


def _device_mode(dut):
    dut.write(b'settings get_device\r\n')
    m = _expect_re(dut, r'Device mode (\d+)', what='settings get_device')
    return int(_grp(m, 1))


def _next_lora_sync_epoch(dut, timeout=CMD_TIMEOUT_S):
    """Force a transmit-alarm reprogram and return the next LoRa sync-cloud wake.

    app_module logs 'Transmit time for each mode [<type>] <lora> <no_probe>
    <normal>' whenever the alarm is reprogrammed, and <lora> is the sync-cloud
    epoch (-1 when none is planned). The reprogram must be forced by *changing* a
    schedule setting: etc_set_log_interval_secs() returns early on a same-value
    write, so rewriting the current value reschedules nothing."""
    original = _get_setting(dut, 'log_interval')
    toggled = 61 if original == 60 else 60
    dut.write(f'settings set_log_interval {toggled}\r\n'.encode())
    m = _expect_re(dut, r'Transmit time for each mode \[\d+\] (-?\d+) ', timeout=timeout,
                   what='transmit schedule log line (needs DBG app logs)')
    lora_epoch = int(_grp(m, 1))
    _apply_setting(dut, 'log_interval', original)
    return lora_epoch


def test_bug1_lora_sync_subjob_survives_connect(dut, request, coiote_optional):
    """Steer the RTC to just before the daily LoRa sync-cloud wake and verify
    the dispatch keeps sub-job SYNC_CLOUD_LORA (the old code consumed the type
    at the wake and the sensor's DATA_READY then reset the sub-job to NORMAL
    mid-connect, uploading nothing)."""
    mode = _device_mode(dut)
    if mode != ETC_DEVICE_MODE_LORA_LOGGER:
        pytest.skip(f"Device is in mode {mode}; the daily LoRa sync-cloud wake only "
                    f"exists in LoRa logger mode (`settings set_device 1`)")

    # tx_delay 0 makes the sync wake a single JOB_BOTH wakeup, which is the shape
    # bug 1 needs: the sync dispatches and the sensor read completes afterwards.
    _set_and_restore(dut, request, 'tx_delay', 0)
    # Keep ordinary interval transmits out of the way so the daily sync-cloud
    # wake is the soonest transmit, and therefore the one actually scheduled.
    _set_and_restore(dut, request, 'tx_interval', 86400)
    # The test steers the RTC; put a real time back afterwards.
    request.addfinalizer(lambda: dut.write(f'date set {int(time.time())}\r\n'.encode()))

    lora_epoch = _next_lora_sync_epoch(dut)
    if lora_epoch <= 0:
        pytest.skip("No LoRa sync-cloud wake planned for today")

    # Steer the RTC to just before the sync wake. The device re-syncs its clock
    # from the network, which reverts the jump after ~70-80 s when the modem is
    # idle (and within seconds while it is connecting), so wait for PSM first and
    # keep the whole wake-plus-assert sequence inside that window. Retry if a
    # re-sync steals the window anyway.
    st = None
    for attempt in range(JUMP_ATTEMPTS):
        _wait_modem_idle(dut)
        base = _status(dut)['dispatches']
        dut.write(f'date set {lora_epoch - JUMP_LEAD_S}\r\n'.encode())
        try:
            st = _wait_dispatches(dut, base + 1,
                                  timeout=JUMP_LEAD_S + DISPATCH_TIMEOUT_S)
            break
        except AssertionError:
            if attempt == JUMP_ATTEMPTS - 1:
                raise AssertionError(
                    f"The sync-cloud wake never fired in {JUMP_ATTEMPTS} attempts; "
                    f"the clock re-sync reverted the steer before the wake")
            print(f"  [retry] clock re-sync beat the wake (attempt {attempt + 1})")

    assert st['subjob'] == 'lora', \
        f"Sync-cloud wake must dispatch with sub-job SYNC_CLOUD_LORA (bug 1): {st}"

    # The sensor read completing after the sync dispatch must not clobber the
    # sub-job back to NORMAL while the modem is connecting. This is the actual
    # regression: pre-fix the DATA_READY handler reset it here.
    time.sleep(5)
    st = _status(dut)
    assert st['subjob'] == 'lora', \
        f"Sub-job was clobbered after the sync dispatch (bug 1): {st}"
    assert st['dispatches'] == base + 1, f"Spurious extra dispatch (bug 1): {st}"

    if coiote_optional is None:
        print("  [coiote] no credentials; portal cross-check skipped")
        return

    # End-to-end: the connect must actually reach the portal. Tasks created by
    # the Coiote client are deliberately left in place (audit trail).
    device = request.config.getoption("--coiote-device")
    deadline = time.time() + 300
    seen = None
    while time.time() < deadline:
        try:
            seen = coiote_optional.read_cached(device, "Device.0.Current Time")[0]
            break
        except Exception:
            time.sleep(15)
    assert seen is not None, "Sync-cloud upload did not reach the portal"
