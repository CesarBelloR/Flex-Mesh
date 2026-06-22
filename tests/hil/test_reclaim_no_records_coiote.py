"""End-to-end HIL test for a reclaim over an empty period via Coiote (FW-966).

Drives the production cloud path for a directly-connected (cellular) device:
Coiote writes the reclaim window (48934/0/1 Start time, 48934/0/2 End time) and
executes the Reclaim resource (48934/0/3) over a period that contains no stored
readings. The firmware's Execute handler checks synchronously that no record
falls in the window, so:

  * the Execute Operation FAILS (the device returns a CoAP error), and
  * the device *sends* the Status resource (48934/0/4) as NO_RECORDS (5).

The device reports reclaim status by sending it to the server (an LwM2M Send) —
in production nothing reads/polls the Status. So the test issues NO Read: it
asserts the Execute task ends in error, then polls Coiote's cached Status (which
is updated only by the device's Send) until it reports NO_RECORDS.

A future window is used as the empty period — stored readings are always in the
past, so a window starting an hour from now is guaranteed to contain none
without disturbing any real data on the device.

Requirements:
  * the board flashed (debug build, no rtt.conf) and **LTE/cloud connected**
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)

Run:
  pytest tests/hil/test_reclaim_no_records_coiote.py -v -s \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import time

import pytest

from coiote_client import CoioteError

# Data-model keys derived from the DDF resource names in
# applications/.../exact_objects/exact-reclaim_48934.xml (object "EXACT
# Reclaim", instance 0). If the tenant addresses the object by raw LwM2M path
# instead, switch these to "/48934/0/1", "/48934/0/2", "/48934/0/3", "/48934/0/4".
START_KEY = "EXACT Reclaim.0.Start time"
END_KEY = "EXACT Reclaim.0.End time"
RECLAIM_KEY = "EXACT Reclaim.0.Reclaim"
STATUS_KEY = "EXACT Reclaim.0.Status"

# Status resource value (mirrors ETC_RECLAIM_STATUS_NO_RECORDS in
# etc_reclaim_obj_48934.h).
STATUS_NO_RECORDS = "5"

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

def _shell(dut, cmd):
    """Provoke a registration update so the queue-mode device check-ins."""
    dut.write(f"{cmd}\r\n".encode())


def _drive_task(coiote, dut, device, task_id, op_timeout):
    """Poll a queued task to a terminal report, nudging the device each poll.

    Unlike CoioteClient.wait_for_task, this does NOT raise on an Error status —
    a failed Execute is the expected outcome here. Returns the terminal report.

    The task is intentionally NOT deleted afterward: leaving it on Coiote keeps a
    record of what the test executed, which is valuable when investigating runs.
    """
    deadline = time.monotonic() + op_timeout
    report = None
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")  # provoke a registration update
        time.sleep(20)
        report = coiote.get_task_report(task_id, device)
        if report is not None and report.get("status") in (
            "Success", "Warning", "Error", "ErrorRetryable"):
            return report
    raise CoioteError(
        f"Task {task_id} did not finish within {op_timeout}s "
        f"(last report: {report})")


def _await_pushed_status(coiote, dut, device, op_timeout):
    """Wait for the device to *push* the NO_RECORDS status, with no Read.

    read_cached reflects the value the device last *sent* (LwM2M Send); we never
    issue a Read. Nudge a registration update each poll so the queue-mode device
    delivers the pending send.
    """
    deadline = time.monotonic() + op_timeout
    value = None
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")
        time.sleep(20)
        try:
            value = str(coiote.read_cached(device, STATUS_KEY)[0])
        except CoioteError:
            continue
        print(f"  [coiote] pushed status={value!r}")
        if value == STATUS_NO_RECORDS:
            return value
    return value


# --------------------------------------------------------------------------- #
# Test
# --------------------------------------------------------------------------- #

def test_reclaim_no_records_e2e(coiote, dut, request):
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

    # A future window is guaranteed to contain no stored readings.
    start = int(time.time()) + 3600
    stop = start + 60

    try:
        # Write the empty window, then Execute the reclaim. The device finds no
        # records in the period, so the Execute must FAIL.
        task_id = coiote.configure_task(
            device,
            [
                # Coiote v3 expects the write value as a string (like execute args).
                {"write": {"key": START_KEY, "value": str(start)}},
                {"write": {"key": END_KEY, "value": str(stop)}},
                {"execute": {"key": RECLAIM_KEY}},
            ],
            name="hil-reclaim-no-records",
        )
        report = _drive_task(coiote, dut, device, task_id, op_timeout)
        status = report.get("status")
        print(f"  [coiote] execute task status={status!r}")
        assert status in ("Error", "ErrorRetryable"), (
            f"Reclaim Execute over an empty period should fail, but the task "
            f"ended in {status!r}. Report: {report}")

        # The device must SEND Status -> NO_RECORDS (5); no Read is issued.
        value = _await_pushed_status(coiote, dut, device, op_timeout)
        assert value == STATUS_NO_RECORDS, (
            f"Device-pushed Status after empty-period Execute was {value!r}, "
            f"expected {STATUS_NO_RECORDS!r} (NO_RECORDS). The device must SEND "
            f"the updated reclaim status; a Read should not be required.")
    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch?): {exc}")
