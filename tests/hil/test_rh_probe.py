"""Operator-attended HIL test for the RH (SHT31) probe on real hardware.

Covers FW-1195: after the FW-588 grounded-line guard, the first port whose
1-Wire line idled high was probed for the functional-test jig and never read as
a sensor, so an RH probe on port 1 produced neither humidity nor its own
temperature until it was moved to another port. Nothing is persisted, which is
why a same-port reconnect or a reboot never recovered it.

Run with `-s` so the operator prompts are visible and stdin works:

    pytest tests/hil/test_rh_probe.py -v -s

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

**The order of the tests in this file is load-bearing.** Each test leaves the
probe where the next one expects it.
"""

import datetime
import time

import pytest

from acquisition import acquire_digital
from coiote_client import CoioteError

# Physically plausible %RH; the SHT31 reports 0..100.
RH_MIN = 1.0
RH_MAX = 100.0

# Cached-data-model key for 48936/0/5700 by DDF resource name, with the raw
# LwM2M path as fallback for a tenant that addresses the object by path. The
# port resource (48936/0/2, "Logger port") is not part of the humidity Send
# path list, so it is not checked here.
HUMID_VALUE_KEYS = ("EXACT Humidity.0.Sensor Value", "/48936/0/5700")

SEND_TIMEOUT_S = 90


def _expect_rh_on_input(dut, input_index):
    humidity, index, functional_test = acquire_digital(dut)
    assert not functional_test, "functional-test jig detected with probes attached"
    assert RH_MIN <= humidity <= RH_MAX, (
        f"humidity {humidity} %RH out of range; the RH probe was not read")
    assert index == input_index, (
        f"humidity attributed to input {index + 1}, expected input {input_index + 1}")
    return humidity


def test_rh_probe_on_port_one_is_read(dut, operator):
    """The regression: an RH probe on port 1 with nothing else attached."""
    operator.prompt("""
        Wire the device as follows, then power-cycle it:
          - port 1: RH probe
          - ports 2, 3, 4: empty
    """)

    _expect_rh_on_input(dut, 0)


def test_rh_probe_survives_same_port_reconnect(dut, operator):
    """QA's first workaround attempt, which did not recover the reading."""
    operator.prompt("""
        Unplug the RH probe from port 1 and plug it back into port 1.

        Do NOT power-cycle or reset the device.
    """)

    _expect_rh_on_input(dut, 0)


def test_rh_probe_moved_to_port_two(dut, operator):
    """Port 1 open, RH probe on port 2: the case that always worked."""
    operator.prompt("""
        Move the RH probe from port 1 to port 2. Leave the other ports empty.

        Do NOT power-cycle or reset the device.
    """)

    _expect_rh_on_input(dut, 1)


def test_rh_probe_on_port_one_after_cold_boot(dut, operator):
    """Back on port 1 across a cold boot, the way a field unit updates."""
    operator.prompt("""
        Move the RH probe back to port 1. Leave the other ports empty.
    """)
    dut.cold_boot()

    _expect_rh_on_input(dut, 0)


def _read_cached_any(coiote, device, keys):
    last = None
    for key in keys:
        try:
            return coiote.read_cached(device, key)
        except CoioteError as exc:
            last = exc
    raise last


def test_rh_reading_reaches_coiote(dut, operator, coiote_optional, request):
    """The cloud receives the humidity value (LTE-mode DUT)."""
    if coiote_optional is None:
        pytest.skip("No Coiote config (use --coiote-config or $COIOTE_CONFIG)")
    device = request.config.getoption("--coiote-device")

    started = datetime.datetime.now(datetime.timezone.utc)
    humidity = _expect_rh_on_input(dut, 0)
    dut.write(b"app_module trigger_tx\r\n")

    deadline = time.time() + SEND_TIMEOUT_S
    while True:
        value, updated = _read_cached_any(coiote_optional, device, HUMID_VALUE_KEYS)
        if updated is not None and updated >= started:
            break
        assert time.time() < deadline, (
            f"48936/0 not updated on Coiote within {SEND_TIMEOUT_S}s "
            f"(last value {value!r} at {updated})")
        time.sleep(5)

    assert abs(float(value) - humidity) < 5.0, (
        f"Coiote humidity {value} differs from device {humidity}")
