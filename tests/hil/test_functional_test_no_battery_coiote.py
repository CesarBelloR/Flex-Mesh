"""HIL test for the functional test battery-connected check via Coiote (FW-169).

The production functional test reports its outcome in object 48937 (resource 1,
"Result"). FW-169 adds a check that a battery is physically connected and a
dedicated result code ``7`` (battery not connected). The device *sends* the
result (an LwM2M Send); nothing polls it. This test reads Coiote's cached Result
(updated only by the device's Send) and asserts it.

Unlike the reclaim tests, the functional test is **not** triggerable from the
cloud — it runs automatically at boot when the production test jig presents the
expected sensor reference values while the charger is attached. So this is an
**operator-assisted** test:

  1. Mount the unit on the functional-test jig (presents the reference sensor
     values) with the charger attached, **battery removed**.
  2. Power-cycle the unit so the functional test runs and completes; wait for it
     to cloud-connect and send its result.
  3. Run this test pointing at the same device.

The test asserts the device-pushed Result is ``7`` (battery not connected). A
companion positive run (battery installed) should report ``0`` (success); pass
``--functional-test-expect 0`` for that case.

Requirements:
  * the board flashed (debug build) and **LTE/cloud connected**
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)

Run:
  pytest tests/hil/test_functional_test_no_battery_coiote.py -v -s \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import time

import pytest

from coiote_client import CoioteError

# Data-model key derived from the DDF resource name in
# applications/.../exact_objects/exact-functional-test_48937.xml (object "EXACT
# Functional Test", instance 0, resource "Result"). If the tenant addresses the
# object by raw LwM2M path instead, switch this to "/48937/0/1".
RESULT_KEY = "EXACT Functional Test.0.Result"

# Result value (mirrors FUNC_TEST_FAIL_BAT_DISCONNECTED in etc_functional_test.h
# / the 48937 XML spec).
RESULT_BAT_DISCONNECTED = "7"
RESULT_NOT_STARTED = "99"

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


def _shell(dut, cmd):
    dut.write(f"{cmd}\r\n".encode())


def _await_pushed_result(coiote, dut, device, op_timeout):
    """Wait for the device-pushed Result (no Read), nudging registration updates."""
    deadline = time.monotonic() + op_timeout
    value = None
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")
        time.sleep(20)
        try:
            value = str(coiote.read_cached(device, RESULT_KEY)[0])
        except CoioteError:
            continue
        print(f"  [coiote] pushed functional-test result={value!r}")
        if value not in (None, RESULT_NOT_STARTED):
            return value
    return value


def test_functional_test_no_battery(coiote, dut, request):
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    expected = str(request.config.getoption("--functional-test-expect"))

    last_contact = coiote.device_last_contact(device)
    if last_contact is None:
        pytest.skip(f"Device {device} not found in Coiote")
    age = datetime.datetime.now(datetime.timezone.utc) - last_contact
    if age > MAX_CONTACT_AGE:
        pytest.skip(f"Device {device} last contacted Coiote {age} ago (offline)")
    print(f"\nDevice {device} last contact {age} ago")

    value = _await_pushed_result(coiote, dut, device, op_timeout)
    if value in (None, RESULT_NOT_STARTED):
        pytest.skip(
            "No functional-test result was pushed (got "
            f"{value!r}). Run the functional test on the jig first "
            "(see this file's docstring).")

    assert value == expected, (
        f"Device-pushed functional-test Result was {value!r}, expected "
        f"{expected!r}. For the no-battery case expect "
        f"{RESULT_BAT_DISCONNECTED!r} (battery not connected); for the "
        f"battery-installed case run with --functional-test-expect 0.")
