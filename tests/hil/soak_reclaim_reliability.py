"""Soak test for FW-899: LoRa reclaim request reliability.

Directly re-triggers the two original bugs in a loop:

  Bug 1 — Request destroyed on first ACK:
    etc_get_reclaim_request_for_relay_with_logger_id() used to clear the
    request flag the moment the ACK was prepared. With real LoRa traffic,
    this silently lost the request after one packet exchange. The fix keeps
    the request alive until confirmed in-window data arrives.

  Bug 2 — Requests lost on reboot:
    list_reclaim_request[] was zeroed on every boot. The fix uses retained
    RAM so the request survives warm reset.

Each iteration:
  1. Set a reclaim window far in the past (will never be satisfied by
     current logger traffic — only real in-window data clears it).
  2. Wait 90 s for at least one real LoRa packet from logger 10000597.
  3. Assert the request is still present  <- catches Bug 1.
  4. Reboot the relay.
  5. Assert the request survived the reboot  <- catches Bug 2.

Duration is controlled by the --soak-hours pytest option (default 24).
Each iteration takes ~2 min, so 24 h ~ 700 iterations.
"""

import time
import pexpect
import pytest
import serial


def _flush(dut):
    """Drain any stale data from the pexpect buffer."""
    try:
        while dut.pexpect_proc.read_nonblocking(size=4096, timeout=0.1):
            pass
    except (pexpect.TIMEOUT, pexpect.EOF):
        pass


def _shell(dut, cmd, timeout=10, retries=2):
    for attempt in range(retries + 1):
        _flush(dut)
        try:
            dut.write(f'{cmd}\r\n'.encode())
            time.sleep(0.5)
            dut.expect(r'uart:~\$', timeout=timeout)
            before = dut.pexpect_proc.before
            if isinstance(before, bytes):
                before = before.decode('utf-8', errors='replace')
            return before
        except (pexpect.TIMEOUT, serial.SerialException, OSError):
            if attempt == retries:
                raise
            time.sleep(2)


def _reboot(dut):
    try:
        dut.write(b'kernel reboot\r\n')
    except (serial.SerialException, OSError):
        pass  # Port may drop during reboot — that's OK
    time.sleep(8)
    # Retry prompt detection — USB CDC ACM may re-enumerate after reboot
    for attempt in range(3):
        _flush(dut)
        try:
            dut.write(b'\r\n')
            dut.expect(r'uart:~\$', timeout=10)
            return
        except (pexpect.TIMEOUT, serial.SerialException, OSError):
            time.sleep(3)
    raise RuntimeError("Device did not come back after reboot")


def test_reclaim_survives_lora_traffic_and_reboots(dut, request):
    """Soak: re-trigger Bug 1 and Bug 2 continuously for --soak-hours hours."""
    hours = request.config.getoption('--soak-hours')
    deadline = time.time() + hours * 3600

    # A reclaim window in 2001 -- no current logger data will ever satisfy it.
    start, stop = 1000000, 1000120

    _reboot(dut)
    time.sleep(3)

    iteration = 0
    serial_recoveries = 0
    max_serial_recoveries = 10
    while time.time() < deadline:
        iteration += 1

        try:
            # Fresh state each iteration
            _shell(dut, 'relay_reclaim clear_all')
            _shell(dut, f'relay_reclaim set 10000597 {start} {stop}')

            out = _shell(dut, 'relay_reclaim list')
            assert '10000597' in out, \
                f"[iter {iteration}] Request not set. Output: {out}"

            # Wait for real LoRa traffic -- logger 10000597 sends once per minute.
            # 90 s guarantees at least one full RX cycle.
            time.sleep(90)

            # Bug 1 check: request must still be active after real LoRa packets
            out = _shell(dut, 'relay_reclaim list')
            assert '10000597' in out, \
                f"[iter {iteration}] Request cleared by out-of-window LoRa data (Bug 1). Output: {out}"

            # Bug 2 check: request must survive a reboot
            _reboot(dut)
            time.sleep(3)

            out = _shell(dut, 'relay_reclaim list')
            assert '10000597' in out, \
                f"[iter {iteration}] Request lost after reboot (Bug 2). Output: {out}"

        except (serial.SerialException, OSError) as e:
            # USB CDC ACM can drop during reboot. Wait for re-enumeration,
            # then re-establish the connection and continue.
            serial_recoveries += 1
            print(f"\n[iter {iteration}] Serial error ({e}), recovery "
                  f"{serial_recoveries}/{max_serial_recoveries}")
            if serial_recoveries > max_serial_recoveries:
                raise RuntimeError(
                    f"Too many serial recoveries ({serial_recoveries})") from e
            time.sleep(10)
            # Re-open the serial port after USB re-enumeration
            if hasattr(dut, 'serial') and hasattr(dut.serial, 'close'):
                try:
                    dut.serial.close()
                except Exception:
                    pass
                time.sleep(2)
                try:
                    dut.serial.open()
                except Exception:
                    pass
            time.sleep(5)
            _flush(dut)
            try:
                dut.write(b'\r\n')
                dut.expect(r'uart:~\$', timeout=20)
            except Exception:
                time.sleep(5)
                dut.write(b'\r\n')
                dut.expect(r'uart:~\$', timeout=20)
            print(f"[iter {iteration}] Recovered, continuing soak")
            continue

    print(f"\nSoak completed: {iteration} iterations over {hours:.1f} h "
          f"-- all passed ({serial_recoveries} serial recoveries).")
