"""HIL tests for FW-1139: force-close the modem SSL context on a failed connect.

A failed AT+QSSLOPEN used to leave the modem-side SSL context open, because
socket_close() only emits AT+QSSLCLOSE when sock->is_connected or force_close is
set and force_close was a dead store. The connect ID stayed occupied, every later
open of it returned 550/563, and __ASSERT_NO_MSG(ret != 550) rebooted the device
once per transmit cycle (Memfault issue 1211222: 1941 devices, 120k traces).

The asserts are deliberately retained, so the failure path can only be exercised
with an error the assert does not cover. 563 (MDM_TCP_ERROR_SOCKET_IN_USE) is
that error; provoking a real 550 panics the device by design.

Injecting the error
-------------------
`modem send <idx> AT+...` writes raw bytes to the modem UART outside the driver's
sem_tx_lock and prints nothing. On this hardware it does NOT perturb the driver:
occupying the connect ID that way was tried and the stack still connected
normally. So the failure is injected over SWD instead, with the J-Link attached.

Verified procedure (2026-07-30, board 1050313184, BG95M3LAR02A03_01.201.01.201):

    JLinkGDBServerCLExe -select USB=<jlink-sn> -device nRF52840_xxAA -if SWD \
        -speed 4000 -port 2331 -singlerun -nogui

    arm-zephyr-eabi-gdb -q build-hil/zephyr/zephyr.elf -x inject563.gdb

  where inject563.gdb arms an automatic breakpoint on the line just after the
  open result is read (`if (ret != 0)` in offload_connect) and rewrites it:

    break quectel-bg95.c:<line of `if (ret != 0)`>
    commands
      silent
      printf "BP HIT: ret=%d sock->id=%d force_close=%d\n", ret, sock->id, force_close
      set var ret = 563
      continue
    end
    continue

  then trigger a transmit on the console (`app_module trigger_tx`).

Observed with the fix -- the full expected chain, and recovery 1.9 s later:

    <inf> modem_quectel_bg95: Status of open TLS socket: 0
    <err> modem_quectel_bg95: Closing the socket!!! error 563
    <wrn> modem_quectel_bg95: Forcing close of modem socket 0
    <err> net_lwm2m_engine: Cannot connect UDP (-112)
    <inf> net_lwm2m_engine: Connected, sock id 3

  GDB reported `force_close=1` already set when the result was read, confirming
  the arm happens before the AT+QSSLOPEN write. -112 is EADDRINUSE: before the
  fix this path left ret at 563, so exit set errno = -563 and lwm2m_socket_start
  computed -errno = +563, which sm_do_registration treats as success.

Requirements:
  * Device in LTE logger mode (`settings get_device` == 2); skipped otherwise.
  * Build WITHOUT rtt.conf (it sets SHELL_LOG_BACKEND=n and hides these lines):

      west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
        -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf"

The tests below are the parts that need no debugger: they prove the fix does not
regress normal operation and that the device neither asserts nor reboots across
repeated connect cycles. The force-close path itself is covered by the GDB
procedure above and by the modem_tcp_error unit suite in modules/exact.
"""

import re
import time

import pytest

ETC_DEVICE_MODE_LTE_LOGGER = 2

# Large interval so a natural transmit wake cannot race the shell-driven one.
TX_INTERVAL_S = 900

CMD_TIMEOUT_S = 15
CONNECT_TIMEOUT_S = 120
# Window to watch for something that must NOT appear.
ABSENT_WINDOW_S = 10
# CONFIG_LWM2M_QUEUE_MODE_UPTIME=30: the engine keeps the DTLS socket up for this
# long after the last exchange. A transmit inside that window reuses the open
# socket and never calls offload_connect, so repeated-connect coverage has to
# wait the socket out first.
QUEUE_MODE_UPTIME_S = 30
QUEUE_MODE_SETTLE_S = QUEUE_MODE_UPTIME_S + 8

_SSL_STATUS = r'Status of open TLS socket: (-?\d+)'
_CONNECTED = r'Connected, sock id (\d+)'
_FORCE_CLOSE = r'Forcing close of modem socket (\d+)'
_CLOSE_ERR = r'Closing the socket!!! error (-?\d+)'
_CONN_FAIL = r'Cannot connect UDP \((-?\d+)\)'
_ASSERT = r'ASSERTION FAIL'
_BOOT = r'Booting nRF Connect SDK'


# --------------------------------------------------------------------------- #
# Serial helpers - each read anchors on a sentinel, never a fixed window
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


def _device_mode(dut):
    dut.write(b'settings get_device\r\n')
    m = _expect_re(dut, r'Device mode (\d+)', what='settings get_device')
    return int(_grp(m, 1))


def _get_tx_interval(dut):
    dut.write(b'settings get_tx_interval\r\n')
    m = _expect_re(dut, r'Tx interval in seconds (\d+)', what='settings get_tx_interval')
    return int(_grp(m, 1))


def _set_tx_interval(dut, value):
    dut.write(f'settings set_tx_interval {value}\r\n'.encode())
    time.sleep(0.5)
    got = _get_tx_interval(dut)
    assert got == value, f"set_tx_interval {value} did not take (read back {got})"


def _lte_logger_setup(dut, request):
    if _device_mode(dut) != ETC_DEVICE_MODE_LTE_LOGGER:
        pytest.skip("Not an LTE logger; the DTLS connect path only runs there")
    original = _get_tx_interval(dut)
    if original != TX_INTERVAL_S:
        request.addfinalizer(lambda: _set_tx_interval(dut, original))
    _set_tx_interval(dut, TX_INTERVAL_S)


def _trigger(dut):
    dut.write(b'app_module trigger_tx\r\n')


# --------------------------------------------------------------------------- #
# Non-regression: the fix must not disturb a healthy connect
# --------------------------------------------------------------------------- #
def test_connect_succeeds_without_forcing_close(dut, request):
    """A healthy connect reports +QSSLOPEN 0 and must NOT force a close.

    force_close is armed before the AT+QSSLOPEN write and cleared only once the
    open reports success, so a successful connect must never log the sentinel.
    """
    _lte_logger_setup(dut, request)

    # Ensure this transmit actually opens a socket rather than reusing a live one.
    time.sleep(QUEUE_MODE_SETTLE_S)
    _trigger(dut)
    m = _expect_re(dut, _SSL_STATUS, timeout=CONNECT_TIMEOUT_S, what='+QSSLOPEN status')
    assert int(_grp(m, 1)) == 0, f"expected a clean open, got {_grp(m, 1)}"

    _expect_re(dut, _CONNECTED, timeout=CONNECT_TIMEOUT_S, what='LwM2M connected')
    _expect_absent(dut, _FORCE_CLOSE, ABSENT_WINDOW_S,
                   what='force-close on a successful connect')


def test_repeated_connects_do_not_assert_or_reboot(dut, request):
    """The 15-minute assert/reboot loop must not reproduce across connect cycles.

    Each cycle waits the queue-mode socket out so the transmit genuinely runs
    offload_connect again, then anchors on that cycle's own Connected sentinel
    before checking for the panic and boot banners the FW-1139 loop produced.
    """
    _lte_logger_setup(dut, request)

    for cycle in range(3):
        # Let queue mode drop the DTLS socket, otherwise the transmit reuses it.
        time.sleep(QUEUE_MODE_SETTLE_S)
        _trigger(dut)
        _expect_re(dut, _CONNECTED, timeout=CONNECT_TIMEOUT_S,
                   what=f'LwM2M connected (cycle {cycle})')
        _expect_absent(dut, _ASSERT, ABSENT_WINDOW_S, what=f'assert (cycle {cycle})')
        _expect_absent(dut, _BOOT, ABSENT_WINDOW_S, what=f'reboot (cycle {cycle})')


def test_connect_errors_surface_as_negative_errno(dut, request):
    """Any connect failure must report a POSIX errno, never a raw modem code.

    Passive: it only asserts if a failure happens to occur during the run. The
    deterministic version is the GDB procedure in the module docstring; the
    exhaustive version is the modem_tcp_error ztest suite.
    """
    _lte_logger_setup(dut, request)

    time.sleep(QUEUE_MODE_SETTLE_S)
    _trigger(dut)
    try:
        m = dut.expect(_CONN_FAIL, timeout=ABSENT_WINDOW_S * 3)
    except Exception:
        pytest.skip("No connect failure occurred; nothing to assert on")

    err = abs(int(_grp(m, 1)))
    assert err < 256, (
        f"Cannot connect UDP ({_grp(m, 1)}) is a raw modem code, not an errno. "
        "563/550 leaking here is the FW-1139 positive-return bug.")
