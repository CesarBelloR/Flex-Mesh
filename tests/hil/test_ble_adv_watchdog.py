"""HIL test for FW-1203: an advertiser the host stopped silently is restarted.

A connection attempt that fails to establish ends the advertising set with no
`disconnected` callback; before the fix only the next sensor sample (one log
interval, default 15 min) brought advertising back. The host's `recycled`
callback now resumes the set as soon as the pending connection object is
released, and the advertising watchdog re-checks every
CONFIG_ETC_BLE_ADV_WATCH_PERIOD_SEC (60 s), leaving a running set alone.

The silent stop is reproduced with the HIL-only shell command `ble adv_kill`,
which stops the set exactly as the host does behind the app's back. The test
reads the resume (or watchdog) log line and, when `bleak` is available on the
host, confirms the device is back on air.

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

The DUT must be in BLE mode (`settings set_device 3`); the test skips otherwise.

Run:

    pytest tests/hil/test_ble_adv_watchdog.py -v -s
"""

import asyncio
import re
import time

import pytest

# Mirrors CONFIG_ETC_BLE_ADV_WATCH_PERIOD_SEC.
WATCH_PERIOD_S = 60
RESTARTED = "Advertising had stopped, restarted"
RESUMED = "Advertising resumed"
RESTORED_RE = re.compile("|".join(re.escape(t) for t in (RESUMED, RESTARTED)))
KILLED_RE = re.compile(r"Advertising set stopped \(rc (-?\d+)\)")
SCAN_TIMEOUT_S = 20.0
# Manufacturer data lengths as bleak reports them (26 - 2 or 42 - 2 bytes).
FLEX_MFG_LENS = (24, 40)


def _text(value):
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def _drain(dut, seconds):
    deadline = time.time() + seconds
    buf = ""
    while time.time() < deadline:
        try:
            buf += _text(dut.expect(r".+", timeout=0.5).group(0)) + "\n"
        except Exception:
            pass
    return buf


def _ensure_advertising(dut):
    """BLE mode only: elsewhere the magnet window of the first swipe ends
    advertising partway through the suite and the checks pass for the wrong
    reason. A swipe restarts advertising so the start line is fresh."""
    dut.write(b"settings get_device\r\n")
    mode = int(dut.expect(r"Device mode (\d+)", timeout=10).group(1))
    if mode != 3:
        pytest.skip(f"DUT is device mode {mode}, not BLE (3); run 'settings set_device 3'")
    dut.write(b"magnet swipe\r\n")
    dut.expect(re.escape("Advertising successfully started"), timeout=30)


async def _flex_on_air(timeout):
    """True if a Flex advertisement is seen; None if bleak is unavailable."""
    try:
        from bleak import BleakScanner
    except ImportError:
        return None

    seen = asyncio.Event()

    def _cb(device, adv):
        name = adv.local_name or device.name or ""
        by_layout = (len(adv.manufacturer_data) == 1
                     and len(next(iter(adv.manufacturer_data.values()))) in FLEX_MFG_LENS)
        if name.startswith("Flex_") or by_layout:
            seen.set()

    async with BleakScanner(_cb):
        try:
            await asyncio.wait_for(seen.wait(), timeout)
        except asyncio.TimeoutError:
            return False
    return True


def test_silently_stopped_advertiser_is_restarted(dut):
    _ensure_advertising(dut)
    _drain(dut, 2.0)

    dut.write(b"ble adv_kill\r\n")
    m = dut.expect(KILLED_RE, timeout=10)
    assert int(_text(m.group(1))) == 0, f"adv_kill could not stop the set (rc {_text(m.group(1))})"

    started = time.time()
    m = dut.expect(RESTORED_RE, timeout=WATCH_PERIOD_S + 15)
    print(f"\nadvertising back after {time.time() - started:.0f} s ({_text(m.group(0))})")

    on_air = asyncio.run(_flex_on_air(SCAN_TIMEOUT_S))
    if on_air is None:
        print("bleak not installed; on-air check skipped")
    else:
        assert on_air, "device not seen advertising after the restart"


def test_running_advertiser_is_left_alone(dut):
    _ensure_advertising(dut)

    log = _drain(dut, WATCH_PERIOD_S + 10)

    assert RESTARTED not in log, "watchdog restarted a healthy advertiser"
    assert RESUMED not in log, "a healthy advertiser was resumed"
    assert "Advertising had stopped" not in log, "watchdog reported a healthy advertiser as stopped"
    on_air = asyncio.run(_flex_on_air(SCAN_TIMEOUT_S))
    assert on_air is not False, "device stopped advertising during the period"
