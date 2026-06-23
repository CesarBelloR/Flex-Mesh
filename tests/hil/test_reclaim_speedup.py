"""HIL test for FW-965: the relay extends its LoRa listening window while a
reclaim is queued, so an active reclaim drains in fewer rendezvous.

The relay logs ``Max waiting time <ms>`` (over the USB console) at the start of
every receive cycle in module_lora_relay_wait_packet(). That value is
``(rx_duration + extension) * 1000``. The extension is the default rx timeout
(10 s) normally, and 20 s (LORA_RX_TIMEOUT_RECLAIM_SECS) while a reclaim is
queued — so the logged value is exactly 10000 ms larger with a reclaim queued,
independent of the device's configured rx_duration / rx_timeout.

This test drives only the relay DUT; the logger-side shorter tx_delay is
verified via manual two-device integration, consistent with the other reclaim
HIL tests.

The relay runs a chatty debug build whose shell redraws its prompt after every
async log line, so this test never anchors on the generic ``uart:~$`` prompt —
each step waits for a unique sentinel string instead.
"""

# Extra listen time (ms) granted while a reclaim is queued, relative to the
# normal rx-timeout extension: LORA_RX_TIMEOUT_RECLAIM_SECS(20) - default(10).
RECLAIM_EXTRA_LISTEN_MS = 10000

# Far-past window so incoming LoRa data never satisfies (and clears) the entry,
# keeping the reclaim queued across the next relay receive cycle.
_RECLAIM_LOGGER = '10000597'
_RECLAIM_START = 1000000
_RECLAIM_STOP = 1000120

# The relay logs 'Max waiting time' once per RX cycle, spaced by its tx/rx
# interval. Must exceed that interval (currently a 5-minute rendezvous) plus
# scheduling jitter.
RX_CYCLE_TIMEOUT_S = 420


def _send(dut, cmd):
    dut.write(f'{cmd}\r\n'.encode())


def _clear_reclaims(dut):
    _send(dut, 'relay_reclaim clear_all')
    dut.expect(r'All reclaim requests cleared', timeout=10)


def _next_max_waiting_time_ms(dut, timeout=RX_CYCLE_TIMEOUT_S):
    """Wait for the next relay receive cycle and return its 'Max waiting time'."""
    match = dut.expect(r'Max waiting time (\d+)', timeout=timeout)
    value = match.group(1)
    if isinstance(value, bytes):
        value = value.decode('utf-8', errors='replace')
    return int(value)


def test_relay_extends_listen_window_during_reclaim(dut):
    """The relay's listen window grows by 10 s (10 s -> 20 s extension) while a
    reclaim is queued, compared with no reclaim queued."""
    _clear_reclaims(dut)

    # Baseline: no reclaim queued.
    baseline_ms = _next_max_waiting_time_ms(dut)

    # Queue a reclaim that stays buffered (far-past, never satisfied by data).
    _send(dut, f'relay_reclaim set {_RECLAIM_LOGGER} {_RECLAIM_START} {_RECLAIM_STOP}')
    dut.expect(rf'Reclaim request set for {_RECLAIM_LOGGER}', timeout=10)

    # With the reclaim queued, the next receive cycle must listen 10 s longer.
    reclaim_ms = _next_max_waiting_time_ms(dut)

    _clear_reclaims(dut)

    assert reclaim_ms - baseline_ms == RECLAIM_EXTRA_LISTEN_MS, (
        f"Expected listen window to grow by {RECLAIM_EXTRA_LISTEN_MS} ms while a "
        f"reclaim is queued, got baseline={baseline_ms} ms, reclaim={reclaim_ms} ms.")
