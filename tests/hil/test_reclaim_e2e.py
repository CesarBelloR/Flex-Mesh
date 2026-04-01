"""End-to-end reclaim test with a real logger.

Sets a reclaim window covering recent past data, triggers the relay's LoRa RX
listen, and waits for the full reclaim cycle to complete:

  1. Relay listens for LoRa (app_module trigger_rx)
  2. Logger sends normal data -> relay receives and sends reclaim ACK with
     [start_time, stop_time] back to the logger
  3. Logger retransmits historical samples from the requested window
  4. Relay receives retransmitted data, calls etc_clear_reclaim_request_if_satisfied
  5. Request is cleared when a record timestamp falls within [start, stop]

No LTE required — the relay communicates with the logger entirely over LoRa.

Requires logger 10000597 to be active and within LoRa range.

Duration is controlled by --e2e-timeout (default 30 minutes).
"""

import time


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
    """Clear all retained RAM reclaim state and relay record buffer."""
    _shell_cmd_output(dut, 'relay_reclaim clear_all', r'uart:~\$')
    # Erase relay record buffer — if full, the relay can't write incoming
    # data and the ACK + clear path is skipped entirely.
    _shell_cmd_output(dut, 'record erase', r'uart:~\$', timeout=15)


def test_end_to_end_reclaim_cleared_by_logger(dut, request):
    """End-to-end: set a reclaim window covering recent past data, trigger
    relay LoRa RX, and wait for the logger to retransmit historical samples
    that clear the reclaim request.
    """
    timeout_min = request.config.getoption('--e2e-timeout')
    max_wait = timeout_min * 60

    # Skip reboot — USB CDC ACM can drop during reboot and kill the session.
    # Just flush and clean state instead.
    _flush_and_wait(dut)
    _clean_state(dut)

    now = int(time.time())
    start = now - 11 * 60   # 11 minutes ago
    stop = now - 1 * 60     # 1 minute ago

    _shell_cmd_output(dut, f'relay_reclaim set 10000597 {start} {stop}', r'uart:~\$')

    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert '10000597' in text, \
        f"Reclaim entry not found after set.\nOutput: {text}"

    print(f"\nReclaim window: [{start} - {stop}]")
    print(f"  = [{time.strftime('%H:%M:%S', time.localtime(start))} - "
          f"{time.strftime('%H:%M:%S', time.localtime(stop))}]")
    print(f"Waiting up to {timeout_min} min for logger to retransmit...")

    # Start relay LoRa RX listen so the relay can receive logger data
    # and send reclaim ACKs without needing LTE.
    _shell_cmd_output(dut, 'app_module trigger_rx', r'uart:~\$')
    print("Relay LoRa RX triggered")

    # Poll every 30s. On each poll, re-trigger RX to keep the relay
    # listening across multiple LoRa cycles.
    # The full reclaim cycle takes multiple exchanges:
    #   - Logger sends data -> relay ACKs with reclaim window (~1 min)
    #   - Logger retransmits historical records (~1-2 min)
    #   - Relay processes and clears
    poll_interval = 30
    elapsed = 0

    while elapsed < max_wait:
        time.sleep(poll_interval)
        elapsed += poll_interval

        text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
        if 'No active reclaim requests' in text or '10000597' not in text:
            print(f"\nReclaim cleared after {elapsed}s ({elapsed // 60}m {elapsed % 60}s)")
            return

        # Re-trigger RX to keep relay listening
        _shell_cmd_output(dut, 'app_module trigger_rx', r'uart:~\$')

        remaining = max_wait - elapsed
        print(f"  [{elapsed}s] Still active, {remaining}s remaining...")

    # If we get here, the request was never cleared
    text = _shell_cmd_output(dut, 'relay_reclaim list', r'uart:~\$')
    assert False, \
        f"Reclaim request was not cleared after {max_wait}s. " \
        f"Logger may not have retransmitted data for window [{start}-{stop}].\n" \
        f"Output: {text}"
