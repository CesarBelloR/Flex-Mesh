"""HIL test for FW-1194: a magnet swipe during boot must not reset the device.

The advertising work item used to be initialised at the end of etc_ble_init(),
after the slow Bluetooth enable and settings load. A swipe handled before that
submitted a work item with no handler and tripped a kernel assert (reboot
reason "Assert at z_work_submit_to_queue" in Memfault). The swipe is now
latched and replayed once the stack is up.

Swipes are injected every 100 ms from the moment the boot marker appears until
"Bluetooth initialized" is logged. The window between the app module
registering its input handler and the BLE stack coming up is a few hundred
milliseconds on the bench, so several land inside it; swipes injected before
the handler exists are dropped by etc_interface. The test skips when no swipe
was handled before init, because such a run proves nothing about the gate.

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Run:

    pytest tests/hil/test_ble_boot_swipe.py -v -s
"""

import re
import time

import pytest

BT_READY = "Bluetooth initialized"
ADVERTISING = "Advertising successfully started"
BOOT_BANNER = "Booting nRF Connect SDK"
SWIPE_LOGGED = "UI -> ETC_INTERFACE_EVENT_HALL"

# The boot marker cold_boot() waits for lands well before the BLE thread has
# finished enabling the stack, which is the window this test aims at.
INIT_TIMEOUT_S = 60
SWIPE_PERIOD_S = 0.1
ADV_TIMEOUT_S = 30
QUIET_S = 10


def _text(value):
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def _drain(dut, seconds):
    """Read everything the device logs for `seconds` and return it."""
    deadline = time.time() + seconds
    buf = ""
    while time.time() < deadline:
        try:
            buf += _text(dut.expect(r".+", timeout=0.5).group(0)) + "\n"
        except Exception:
            pass
    return buf


def test_magnet_swipe_during_boot_does_not_reset(dut):
    dut.cold_boot()

    swipes = 0
    deadline = time.time() + INIT_TIMEOUT_S
    while True:
        dut.write(b"magnet swipe\r\n")
        swipes += 1
        try:
            m = dut.expect(re.escape(BT_READY), timeout=SWIPE_PERIOD_S)
            break
        except Exception:
            if time.time() > deadline:
                pytest.fail(f"{BT_READY!r} not logged within {INIT_TIMEOUT_S}s")
    before_init = _text(dut.pexpect_proc.before) + _text(m.group(0))
    handled_before_init = before_init.count(SWIPE_LOGGED)
    print(f"\n{swipes} swipes injected, {handled_before_init} handled before {BT_READY!r}")
    if handled_before_init == 0:
        pytest.skip("no swipe was handled before Bluetooth init; the window was missed")

    dut.expect(re.escape(ADVERTISING), timeout=ADV_TIMEOUT_S)

    after = _drain(dut, QUIET_S)
    assert BOOT_BANNER not in after, "device rebooted after the boot-time swipe"
    assert "ASSERTION FAIL" not in after, "assert logged after the boot-time swipe"
