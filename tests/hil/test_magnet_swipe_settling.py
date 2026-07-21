"""HIL test for FW-492: repeated magnet swipes must not re-power the analog
sensor rail while it is still settling.

The 3V analog rail (VCC_A) is fed by a linear regulator that misbehaves if it is
switched off and back on within a few hundred ms (settling time grows to several
seconds, corrupting readings). A magnet swipe used to power the rail on for the
calibrator scan unconditionally; this test verifies the swipe now skips that scan
while the rail is settling.

Observability is via shell commands only (no reliance on log strings), so the
test works regardless of whether application logs are routed to RTT or the serial
console:

  magnet swipe   -> inject a HALL (magnet swipe) event, bypassing the GPIO debounce
  magnet status  -> "settling=<yes|no> skips=<count> pending=<reason>
                     dispatches=<count> subjob=<sub-job>"

`skips` counts magnet swipes that skipped the calibrator scan because the rail was
still settling. Both commands require a build with CONFIG_ETC_INTERFACE_TEST_SHELL=y;
add the `overlay-hil.conf` overlay to the build (it is intentionally kept out of
debug.conf so normal debug builds don't carry the test-only command overhead), e.g.:

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;rtt.conf;overlay-memfault.conf;overlay-hil.conf"
"""

import time


def _shell_cmd_output(dut, cmd, end_pattern=r'uart:~\$', timeout=10):
    """Run a shell command, return full output up to and including end_pattern."""
    dut.write(f'{cmd}\r\n'.encode())
    time.sleep(0.3)
    match = dut.expect(end_pattern, timeout=timeout)
    before = dut.pexpect_proc.before
    if isinstance(before, bytes):
        before = before.decode('utf-8', errors='replace')
    matched = match.group(0)
    if isinstance(matched, bytes):
        matched = matched.decode('utf-8', errors='replace')
    return before + matched


def _flush_and_wait(dut, seconds=2):
    """Wait for boot/log spam to settle then drain the buffer."""
    time.sleep(seconds)
    try:
        while True:
            dut.expect(r'.+', timeout=0.5)
    except Exception:
        pass


def _grp(match, index):
    value = match.group(index)
    return value.decode('utf-8', 'replace') if isinstance(value, bytes) else value


def _magnet_status(dut, timeout=15):
    """Return the parsed `magnet status` fields.

    Anchored on the command's own unique output rather than the shell prompt:
    application logs share this console and constantly redraw the prompt, so a
    prompt-anchored read returns before the command's output has arrived."""
    dut.write(b'magnet status\r\n')
    try:
        m = dut.expect(r'settling=(yes|no)\s+skips=(\d+)\s+pending=(\w+)\s+'
                       r'dispatches=(\d+)\s+subjob=(\w+)', timeout=timeout)
    except Exception as exc:
        raise AssertionError(f"Timed out reading `magnet status` ({timeout}s): {exc}") from None
    return {
        'settling': _grp(m, 1) == 'yes',
        'skips': int(_grp(m, 2)),
        'pending': _grp(m, 3),
        'dispatches': int(_grp(m, 4)),
        'subjob': _grp(m, 5),
    }


def _status(dut):
    """Return (settling: bool, skips: int) parsed from `magnet status`."""
    st = _magnet_status(dut)
    return st['settling'], st['skips']


def _dispatch_count(dut):
    """Return the upload dispatch counter from `magnet status` (FW-1113)."""
    return _magnet_status(dut)['dispatches']


def _wait_for_settling(dut, want, timeout_s=20):
    """Poll `magnet status` until settling == want (bool); return final skips."""
    deadline = time.time() + timeout_s
    settling, skips = _status(dut)
    while settling != want and time.time() < deadline:
        time.sleep(0.5)
        settling, skips = _status(dut)
    assert settling == want, \
        f"Timed out waiting for settling={want} (last settling={settling}, skips={skips})"
    return skips


def test_repeated_swipe_skips_scan_while_rail_settling(dut):
    """A second magnet swipe inside the settling window skips the calibrator scan,
    while a swipe after the window runs it normally."""
    dut.reboot()
    _flush_and_wait(dut)

    # Start from a settled rail so swipe #1 is guaranteed to run the scan (a
    # boot-time poll may have left the rail briefly settling).
    _wait_for_settling(dut, want=False)

    # Swipe #1: rail is not settling, so it runs the scan + a full acquisition.
    _, skips_baseline = _status(dut)
    _shell_cmd_output(dut, 'magnet swipe')

    # The acquisition powers the rail on, samples, then turns it off — at which
    # point the rail starts settling. Wait for that transition.
    skips_after_1 = _wait_for_settling(dut, want=True)
    assert skips_after_1 == skips_baseline, \
        f"Swipe #1 should run the scan (not skip): baseline={skips_baseline}, after={skips_after_1}"

    # Swipe #2 while the rail is settling: the calibrator scan must be skipped so
    # the rail is not re-energised inside the danger window.
    _shell_cmd_output(dut, 'magnet swipe')
    deadline = time.time() + 5
    skips_after_2 = skips_after_1
    while skips_after_2 == skips_after_1 and time.time() < deadline:
        time.sleep(0.3)
        _, skips_after_2 = _status(dut)
    assert skips_after_2 == skips_after_1 + 1, \
        (f"Swipe #2 during settling should skip the scan exactly once: "
         f"before={skips_after_1}, after={skips_after_2}")

    # Once the settling window elapses, a swipe must run the scan again (no skip).
    _wait_for_settling(dut, want=False)
    _shell_cmd_output(dut, 'magnet swipe')
    time.sleep(2)
    _, skips_after_3 = _status(dut)
    assert skips_after_3 == skips_after_2, \
        (f"Swipe after the settling window must not skip the scan: "
         f"before={skips_after_2}, after={skips_after_3}")


def test_swipe_during_settling_still_dispatches_upload(dut):
    """FW-1113: a swipe whose own sample is dropped by the settling window must
    still produce an upload (from the records already on flash) instead of the
    request silently waiting for the next log interval."""
    dut.reboot()
    _flush_and_wait(dut)
    _wait_for_settling(dut, want=False)

    # Swipe #1 on a settled rail: full acquisition, DATA_READY dispatches the
    # magnet upload.
    d0 = _dispatch_count(dut)
    _shell_cmd_output(dut, 'magnet swipe')
    deadline = time.time() + 20
    d1 = d0
    while d1 == d0 and time.time() < deadline:
        time.sleep(0.5)
        d1 = _dispatch_count(dut)
    assert d1 == d0 + 1, \
        f"Swipe #1 (settled rail) should dispatch exactly once: {d0} -> {d1}"

    # Swipe #2 inside the settling window: the sample is dropped, but the
    # skipped-sample event must still deliver the pending upload.
    _wait_for_settling(dut, want=True)
    _shell_cmd_output(dut, 'magnet swipe')
    deadline = time.time() + 20
    d2 = d1
    while d2 == d1 and time.time() < deadline:
        time.sleep(0.5)
        d2 = _dispatch_count(dut)
    assert d2 == d1 + 1, \
        (f"Swipe during settling must still dispatch its upload exactly once: "
         f"before={d1}, after={d2}")
