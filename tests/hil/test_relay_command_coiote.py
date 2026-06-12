"""End-to-end HIL test for the relay-command Response resource via Coiote.

Drives the production cloud path: Coiote executes the EXACT Relay Command
resource (48935/0/3) with a ``RECLAIM:*`` argument, the firmware dispatcher
(``etc_relay_command_dispatch``) runs in the LwM2M engine thread and writes its
textual reply to the EXACT Relay Response resource (48935/0/4), and the test
reads that reply back through the Coiote API and asserts it.

This is the only test that exercises the moved Response resource over the real
cloud round trip. The serial ``dut`` is used once as an independent cross-check
that the cloud Execute actually mutated on-device state.

Requirements:
  * the board flashed (debug build, no rtt.conf) and **LTE/cloud connected**
  * ``--port`` for the shell cross-check
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)

Run:
  pytest tests/hil/test_relay_command_coiote.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import re
import time

import pytest

from coiote_client import CoioteError, _parse_iso

# Data-model keys derived from the DDF object/resource names in
# applications/.../exact_objects/exact-relay_48935.xml (object "EXACT Relay",
# instance 0). If the updated tenant addresses the object by raw LwM2M path
# instead, switch these to "/48935/0/3" and "/48935/0/4".
COMMAND_KEY = "EXACT Relay.0.Command"
RESPONSE_KEY = "EXACT Relay.0.Response"

# Throwaway logger id so the test never disturbs real reclaim state.
TEST_LOGGER = "99999999"

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

def _read_fresh(coiote, device, report, op_timeout):
    """Read RESPONSE_KEY, ensuring the cached value is from this task's read op.

    Requires the cached ``updateTime`` to be at/after the task ``startTime``
    (minus a small skew) so we never assert on a stale prior response.
    """
    start = report.get("startTime")
    start_dt = None
    if start:
        start_dt = _parse_iso(start) - datetime.timedelta(seconds=2)

    deadline = time.monotonic() + min(op_timeout, 60)
    last_value = None
    while time.monotonic() < deadline:
        value, update_dt = coiote.read_cached(device, RESPONSE_KEY)
        last_value = value
        if start_dt is None or update_dt is None or update_dt >= start_dt:
            return value
        time.sleep(3)
    return last_value


def _exec_and_read(coiote, device, argument, op_timeout):
    """Execute Command with ``argument`` + read Response in one Coiote task.

    Returns the Response string. Always deletes the task afterward.
    """
    task_id = coiote.configure_execute_read(device, COMMAND_KEY, argument, RESPONSE_KEY)
    try:
        report = coiote.wait_for_task(task_id, device, op_timeout)
        value = _read_fresh(coiote, device, report, op_timeout)
        print(f"  [coiote] {argument!r} -> {value!r}")
        return value
    finally:
        coiote.delete_task(task_id)


def _drain(dut, settle=0.5):
    """Discard any pending serial output (incl. a stale prompt) before a cmd."""
    time.sleep(settle)
    try:
        while True:
            dut.expect(r".+", timeout=0.4)
    except Exception:
        pass


def _shell(dut, cmd, timeout=10):
    """Run a shell command and return its output, tolerant of interleaved logs.

    The shell log backend mirrors logs onto the console, so a bare prompt match
    can race a stale prompt. Anchor on this command's own echo (a unique
    sentinel), then capture up to the following prompt.
    """
    _drain(dut)
    dut.write(f"{cmd}\r\n".encode())
    dut.expect(re.escape(cmd), timeout=timeout)  # consume this command's echo
    dut.expect(r"uart:~\$", timeout=timeout)
    before = dut.pexpect_proc.before
    if isinstance(before, bytes):
        before = before.decode("utf-8", errors="replace")
    return before


# --------------------------------------------------------------------------- #
# Test
# --------------------------------------------------------------------------- #

def test_relay_command_response_e2e(coiote, dut, request):
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    # Pre-flight: only meaningful if the device is currently cloud-connected.
    last_contact = coiote.device_last_contact(device)
    if last_contact is None:
        pytest.skip(f"Device {device} not found in Coiote")
    age = datetime.datetime.now(datetime.timezone.utc) - last_contact
    if age > MAX_CONTACT_AGE:
        pytest.skip(f"Device {device} last contacted Coiote {age} ago (offline)")
    print(f"\nDevice {device} last contact {age} ago")

    # Window used by the add subcommand (values are echoed back by RECLAIM:i).
    now = int(time.time())
    start = now - 10 * 60
    stop = now - 1 * 60

    try:
        # Reset on-device store via the cloud, then confirm it is empty.
        cleared = _exec_and_read(coiote, device, "RECLAIM:clear", op_timeout)
        assert cleared.startswith("OK,cleared="), f"unexpected clear reply: {cleared!r}"

        assert _exec_and_read(coiote, device, "RECLAIM:count", op_timeout) == "count=0"

        # Add one request through the cloud Execute path.
        added = _exec_and_read(
            coiote, device, f"RECLAIM:{TEST_LOGGER},{start},{stop}", op_timeout)
        assert added in ("OK", "OK,evicted=1"), f"unexpected add reply: {added!r}"

        # Independent cross-check over the serial shell: the cloud Execute must
        # have actually mutated device state, not just produced a cached string.
        listing = _shell(dut, "relay_reclaim list")
        assert TEST_LOGGER in listing, \
            f"logger {TEST_LOGGER} not in on-device list after cloud add:\n{listing}"

        assert _exec_and_read(coiote, device, "RECLAIM:count", op_timeout) == "count=1"

        idx = _exec_and_read(coiote, device, "RECLAIM:i,0", op_timeout)
        assert idx.startswith(f"idx=0,logger={TEST_LOGGER},start={start},stop={stop}"), \
            f"unexpected idx reply: {idx!r}"

        # Final clear removes exactly the one request we added.
        assert _exec_and_read(coiote, device, "RECLAIM:clear", op_timeout) == "OK,cleared=1"

    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch?): {exc}")
