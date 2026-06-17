"""HIL tests for LoRa reclaim request reliability (retained RAM persistence and
completion-driven clearing).

Reclaim data is stored in a dedicated 1 KB retained RAM region that survives warm
reboot / reset but is cleared on a full power cycle. Application logs go to RTT
(J-Link) only; tests do not rely on log strings. Shell commands
(relay_reclaim set / list / clear_all / satisfy) provide full test observability.
"""

import time
import re


def _flush_and_wait(dut, seconds=2):
    """Wait for log spam to settle then flush buffer."""
    time.sleep(seconds)
    try:
        while True:
            dut.expect(r'.+', timeout=0.5)
    except Exception:
        pass


def _shell_cmd_output(dut, cmd, end_pattern, timeout=10):
    """Run a shell command, return full output until end_pattern matches."""
    dut.write(f'{cmd}\r\n'.encode())
    time.sleep(0.5)
    match = dut.expect(end_pattern, timeout=timeout)
    before = dut.pexpect_proc.before
    if isinstance(before, bytes):
        before = before.decode('utf-8', errors='replace')
    matched = match.group(0)
    if isinstance(matched, bytes):
        matched = matched.decode('utf-8', errors='replace')
    return before + matched


def _clean_state(dut):
    """Clear all retained RAM reclaim state so tests start from a known-empty baseline."""
    _shell_cmd_output(dut, 'relay_reclaim clear_all', r'uart:~\$')


def test_reclaim_request_survives_reboot(dut):
    """Verify a pending reclaim request persists across a device reboot.

    Retained RAM survives warm reboot/reset but not a power cycle.
    """
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    now = int(time.time())
    _shell_cmd_output(dut, f'relay_reclaim set 10000597 {now - 600} {now}',
                      r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry not found after set.\nOutput: {text}"

    dut.reboot()
    _flush_and_wait(dut)

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry missing after reboot — retained RAM persistence failed.\nOutput: {text}"


def test_reclaim_cleared_when_data_arrives(dut):
    """Verify etc_relay_reclaim_clear_if_satisfied() removes a request when
    a matching timestamp arrives.

    Uses 'relay_reclaim satisfy' to simulate an incoming reclaim data packet
    directly via the shell, avoiding a dependency on logger firmware cooperation.
    The end-to-end LoRa path (logger sending reclaim data) is verified separately
    via manual integration testing.
    """
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    T = int(time.time())
    start = T - 120
    stop = T
    mid = T - 60  # a timestamp inside the window

    _shell_cmd_output(dut, f'relay_reclaim set 10000597 {start} {stop}', r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry not found after set.\nOutput: {text}"

    # Simulate an in-window reclaim data arrival
    text = _shell_cmd_output(dut, f'relay_reclaim satisfy 10000597 {mid}', r'uart:~\$')
    assert 'Reclaim satisfied' in text, \
        f"satisfy command did not report success.\nOutput: {text}"

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert 'No active reclaim requests' in text, \
        f"Reclaim entry still present after satisfaction.\nOutput: {text}"

    # Also verify out-of-window timestamps do NOT satisfy
    _shell_cmd_output(dut, f'relay_reclaim set 10000597 {start} {stop}', r'uart:~\$')

    text = _shell_cmd_output(dut, f'relay_reclaim satisfy 10000597 1000000', r'uart:~\$')
    assert 'No matching' in text, \
        f"Out-of-window timestamp incorrectly satisfied the request.\nOutput: {text}"

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Entry was incorrectly cleared by out-of-window satisfy.\nOutput: {text}"


def test_reclaim_not_cleared_by_out_of_window_data(dut):
    """Verify a reclaim request is NOT cleared when received LoRa data falls outside the window."""
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    _shell_cmd_output(dut, 'relay_reclaim set 10000597 1000000 1000120',
                      r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry not found after set.\nOutput: {text}"

    # Wait long enough for at least 2 LoRa packets to arrive (logger sends once per minute)
    time.sleep(130)

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry was incorrectly cleared by out-of-window data.\nOutput: {text}"


def test_reclaim_request_survives_multiple_reboots(dut):
    """Verify a pending reclaim request persists across three consecutive reboots."""
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    _shell_cmd_output(dut, 'relay_reclaim set 10000597 1000000 1000120',
                      r'uart:~\$')

    for i in range(3):
        dut.reboot()
        _flush_and_wait(dut)

        text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
        assert '10000597' in text, \
            f"Reclaim entry missing after reboot {i + 1}.\nOutput: {text}"


def test_stale_reclaim_evicted_when_buffer_full(dut):
    """Verify that a stale entry (created_at >= 5 days old) is evicted when the
    buffer is full and a new request arrives.

    Logger 10000001 is set with created_at=1000000 (year 2001 — always stale).
    Loggers 10000002-10000020 fill the remaining 19 slots with fresh timestamps.
    Adding logger 10000021 must evict 10000001 (the only stale entry).
    """
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    now = int(time.time())
    fresh_start = now - 120
    fresh_stop = now

    # Slot 1: stale entry (created_at far in the past via optional 5th arg)
    _shell_cmd_output(dut, 'relay_reclaim set 10000001 900000 1000000 1000000', r'uart:~\$')

    # Slots 2-20: fresh entries
    for n in range(2, 21):
        logger_id = f'1000{n:04d}'
        _shell_cmd_output(dut,
                          f'relay_reclaim set {logger_id} {fresh_start} {fresh_stop}',
                          r'uart:~\$')

    # All 20 slots occupied — verify
    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000001' in text, f"Stale entry 10000001 not present before eviction.\nOutput: {text}"
    count_before = text.count('start=')
    assert count_before == 20, f"Expected 20 entries, got {count_before}.\nOutput: {text}"

    # Add 21st entry — should evict 10000001
    _shell_cmd_output(dut, f'relay_reclaim set 10000021 {fresh_start} {fresh_stop}',
                      r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000021' in text, \
        f"New entry 10000021 not present after eviction.\nOutput: {text}"
    assert '10000001' not in text, \
        f"Stale entry 10000001 was NOT evicted.\nOutput: {text}"
    count_after = text.count('start=')
    assert count_after == 20, \
        f"Expected 20 entries after eviction, got {count_after}.\nOutput: {text}"


def test_fresh_reclaim_not_evicted_when_buffer_full(dut):
    """Verify that a new request is rejected (not added) when the buffer is full
    and all existing entries are fresh (not stale).
    """
    dut.reboot()
    _flush_and_wait(dut)
    _clean_state(dut)

    now = int(time.time())
    fresh_start = now - 120
    fresh_stop = now

    # Fill all 20 slots with fresh entries
    for n in range(1, 21):
        logger_id = f'1000{n:04d}'
        _shell_cmd_output(dut,
                          f'relay_reclaim set {logger_id} {fresh_start} {fresh_stop}',
                          r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    count_before = text.count('start=')
    assert count_before == 20, f"Expected 20 entries, got {count_before}.\nOutput: {text}"

    # Attempt to add a 21st entry — should fail
    text = _shell_cmd_output(dut, f'relay_reclaim set 10000021 {fresh_start} {fresh_stop}',
                             r'uart:~\$')
    assert 'Failed to set reclaim request' in text, \
        f"Expected failure when buffer full with fresh entries.\nOutput: {text}"

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000021' not in text, \
        f"Entry 10000021 was incorrectly added.\nOutput: {text}"
    count_after = text.count('start=')
    assert count_after == 20, \
        f"Expected 20 entries, got {count_after}.\nOutput: {text}"
