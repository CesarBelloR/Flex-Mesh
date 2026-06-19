"""End-to-end HIL test for cancelling a reclaim via Coiote (FW-935).

Drives the production cloud path for a directly-connected (cellular) device:
Coiote executes the EXACT Reclaim Cancel resource (48934/0/5), the firmware
fires CLOUD_WRAP_EVT_RECLAIM_CANCEL -> CLOUD_EVT_RECLAIM_CANCEL, the data_module
thread clears the in-progress reclaim state and sets + sends the Status resource
(48934/0/4) to CANCELLED (4).

The device communicates reclaim status changes by *sending* them to the server
(an LwM2M Send), the same as the IN_PROGRESS/SUCCESS transitions — a Coiote Read
is not required. The test asserts on that pushed value: it polls Coiote's cached
Status (updated by the device's Send, never by us reading) until it reports
CANCELLED.

A reclaim is first started over the serial shell (``record reclaim``) as an
independent setup/cross-check that there is active reclaim state for the cloud
Cancel to clear.

Requirements:
  * the board flashed (debug build, no rtt.conf) and **LTE/cloud connected**
  * ``--port`` for the shell setup/cross-check
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)

Run:
  pytest tests/hil/test_reclaim_cancel_coiote.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import re
import time

import pytest

from coiote_client import CoioteError

# Data-model keys derived from the DDF resource names in
# applications/.../exact_objects/exact-reclaim_48934.xml (object "EXACT
# Reclaim", instance 0). If the tenant addresses the object by raw LwM2M path
# instead, switch these to "/48934/0/5" and "/48934/0/4".
CANCEL_KEY = "EXACT Reclaim.0.Cancel"
STATUS_KEY = "EXACT Reclaim.0.Status"

# Status resource value (mirrors ETC_RECLAIM_STATUS_CANCELLED in
# etc_reclaim_obj_48934.h).
STATUS_CANCELLED = "4"

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

def _run_task(coiote, dut, device, operations, name, op_timeout):
    """Queue a Coiote configure task and drive it to completion.

    The DUT is a queue-mode (PSM/eDRX) cellular device with a long registration
    lifetime, so a queued downlink is only delivered when the device next does a
    registration update. We nudge that update from the shell (``app_module
    trigger_tx`` forces a sensor read + cloud send, which wakes LTE and updates
    the registration) on each poll, so Coiote delivers the operation without
    waiting on the device's slow natural cadence.

    Returns the terminal task report. Always deletes the task afterward.
    """
    task_id = coiote.configure_task(device, operations, name=name)
    try:
        deadline = time.monotonic() + op_timeout
        report = None
        while time.monotonic() < deadline:
            _shell(dut, "app_module trigger_tx")  # provoke a registration update
            time.sleep(20)
            report = coiote.get_task_report(task_id, device)
            if report is not None:
                status = report.get("status")
                if status in ("Success", "Warning"):
                    return report
                if status in ("Error", "ErrorRetryable"):
                    raise CoioteError(
                        f"Task {task_id} ({name}) ended in {status}: "
                        f"{report.get('summary')}")
        raise CoioteError(
            f"Task {task_id} ({name}) did not finish within {op_timeout}s "
            f"(last report: {report})")
    finally:
        coiote.delete_task(task_id)


def _cancel_and_await_pushed_status(coiote, dut, device, op_timeout):
    """Execute the Cancel resource, then wait for the device to *push* the
    CANCELLED status.

    The device reports reclaim status changes by sending them to the server (an
    LwM2M Send updating ``48934/0/4``) — a Coiote Read must NOT be required. So
    after the Execute we poll Coiote's cached value (which is updated by the
    device's Send, not by us reading) until it reports CANCELLED, nudging a
    registration update each poll so the queue-mode device sends.

    Returns the Status string.
    """
    # 1. Execute Cancel (no argument).
    _run_task(coiote, dut, device,
              [{"execute": {"key": CANCEL_KEY}}], "hil-reclaim-cancel", op_timeout)

    # 2. Wait for the device to push CANCELLED. read_cached reflects the value
    #    the device last *sent*; we never issue a Read.
    deadline = time.monotonic() + op_timeout
    value = None
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")  # provoke a send/registration update
        time.sleep(20)
        try:
            value = str(coiote.read_cached(device, STATUS_KEY)[0])
        except CoioteError:
            continue
        print(f"  [coiote] pushed status={value!r}")
        if value == STATUS_CANCELLED:
            return value
    return value


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

    Anchor on this command's own echo (a unique sentinel), then capture up to
    the following prompt.
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

def test_reclaim_cancel_e2e(coiote, dut, request):
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

    # Setup over the serial shell: start a reclaim covering recent past data so
    # there is active reclaim state for the cloud Cancel to clear. Best-effort:
    # if the device has no records in the window, the reclaim won't activate,
    # but the cloud Cancel path still drives Status -> CANCELLED.
    now = int(time.time())
    start = now - 30 * 60
    stop = now - 1 * 60
    setup = _shell(dut, f"record reclaim {start} {stop}")
    print(f"  [shell] record reclaim -> {setup.strip()}")

    try:
        status = _cancel_and_await_pushed_status(coiote, dut, device, op_timeout)
        assert status == STATUS_CANCELLED, (
            f"Device-pushed Status after cloud Cancel was {status!r}, "
            f"expected {STATUS_CANCELLED!r} (CANCELLED). The device must SEND the "
            f"updated reclaim status; a Coiote Read should not be required.")
    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch?): {exc}")
