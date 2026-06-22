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

# Relay reclaim slot capacity. Mirrors ETC_RECLAIM_RELAY_MAX_ELEMENT in
# applications/.../etc_devices/etc_relay_reclaim.h.
RELAY_RECLAIM_CAPACITY = 20

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


def _list_count(dut):
    """Return the number of active reclaim slots via the relay_reclaim shell."""
    out = _shell(dut, "relay_reclaim list")
    if "No active reclaim requests" in out:
        return 0
    # cmd_relay_reclaim_list prints one "[<idx>] <logger> ..." line per slot.
    return len(re.findall(r"^\s*\[\d+\]\s", out, re.MULTILINE))


def _await_cloud_contact(coiote, dut, device, warmup_timeout=180):
    """Return a recent Coiote-contact age, nudging check-ins, or None if offline.

    pytest resets the device on port open, so a queue-mode (PSM/eDRX) relay may
    not have re-registered yet at test start. Nudge ``app_module trigger_tx``
    (forces a cloud send / registration update) and poll ``lastContactTime``
    until it is within MAX_CONTACT_AGE, or give up (device genuinely offline).
    """
    deadline = time.monotonic() + warmup_timeout
    while True:
        last = coiote.device_last_contact(device)
        if last is not None:
            age = datetime.datetime.now(datetime.timezone.utc) - last
            if age <= MAX_CONTACT_AGE:
                return age
        if time.monotonic() >= deadline:
            return None
        _shell(dut, "data relay_send")  # relay cloud-send => connect + registration update
        time.sleep(20)


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


def test_buffer_full_execute_fails(coiote, dut, request):
    """FW-967: an add that overflows the reclaim buffer fails the LwM2M Execute.

    When all RELAY_RECLAIM_CAPACITY slots are occupied and none is stale-evictable,
    ``etc_relay_reclaim_set()`` returns ``-ENOMEM``; the dispatcher propagates it so
    ``relay_exec_cb`` returns ``-ENOMEM`` and the Zephyr engine answers the CoAP
    Execute with 5.00 Internal Server Error. Coiote then marks the task as Error,
    which surfaces here as a ``CoioteError`` whose message says "ended in" — the
    regression guard for "buffer-full surfaces as a failed Execute" on the original
    call channel (not merely a side-channel resource write).

    The buffer is filled locally over the shell (fast; no cloud round-trips); only
    the overflowing add is driven through the real cloud Execute path.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    # Pre-flight: only meaningful if the device is currently cloud-connected.
    # Warm up a freshly-rebooted queue-mode device before deciding it is offline.
    age = _await_cloud_contact(coiote, dut, device)
    if age is None:
        pytest.skip(f"Device {device} did not contact Coiote in warm-up (offline)")
    print(f"\nDevice {device} last contact {age} ago")

    now = int(time.time())
    start = now - 10 * 60
    stop = now - 1 * 60
    # Throwaway logger ids in the 9999xxxx range so we never touch real state.
    fill_loggers = [f"9999{i:04d}" for i in range(RELAY_RECLAIM_CAPACITY)]
    overflow_logger = "99990099"

    try:
        # Start from a known-empty buffer.
        _shell(dut, "relay_reclaim clear_all")
        assert _list_count(dut) == 0, "buffer not empty after clear_all"

        # Fill every slot locally. Fresh created_at => no stale eviction, so the
        # next add genuinely has nowhere to go. (set fills the first free slot
        # with no dedup, so distinct loggers occupy distinct slots.)
        for lid in fill_loggers:
            out = _shell(dut, f"relay_reclaim set {lid} {start} {stop}")
            assert "Reclaim request set" in out, \
                f"failed to seed slot for {lid} (device in relay mode? RTC synced?):\n{out}"
        assert _list_count(dut) == RELAY_RECLAIM_CAPACITY, \
            "buffer did not fill to capacity"

        # Device-layer guard: a local add now reports ENOMEM (-12).
        out = _shell(dut, f"relay_reclaim set {overflow_logger} {start} {stop}")
        assert "Failed to set reclaim request: -12" in out, \
            f"full buffer should reject local add with -ENOMEM:\n{out}"

        # The overflowing add over the REAL cloud Execute path must fail: a full
        # buffer => CoAP 5.00 => Coiote task ends in Error => CoioteError. Use an
        # execute-only task so the task status reflects solely the Execute result
        # (a trailing Read could otherwise muddy it into a partial "Warning"). Per
        # the keep-tasks convention this task is left in place for the audit trail.
        task_id = coiote.configure_task(
            device,
            [{
                "execute": {
                    "key": COMMAND_KEY,
                    "argumentList": [{
                        "digit": "0",
                        "argument": f"RECLAIM:{overflow_logger},{start},{stop}",
                    }],
                }
            }],
            name="hil-relay-cmd-buffer-full")
        # Queue-mode (PSM/eDRX) device: nudge a registration update each poll
        # (app_module trigger_tx forces a cloud send) so Coiote delivers the
        # queued Execute without waiting on the device's slow natural cadence.
        # The task is left in place for the audit trail (no delete_task).
        deadline = time.monotonic() + op_timeout
        status = None
        while time.monotonic() < deadline:
            _shell(dut, "data relay_send")  # relay cloud-send => connect + registration update
            time.sleep(20)
            report = coiote.get_task_report(task_id, device)
            if report is None:
                continue
            status = report.get("status")
            if status in ("Error", "ErrorRetryable"):
                break  # the failed Execute we expect
            if status in ("Success", "Warning"):
                pytest.fail("overflowing Execute unexpectedly succeeded "
                            f"(expected CoAP 5.00): {report.get('summary')}")
        # Error => the buffer-full failure surfaced on the call channel. A None
        # status here is an inconclusive queue-mode timeout, not a pass.
        assert status in ("Error", "ErrorRetryable"), \
            f"overflowing Execute did not fail within {op_timeout}s (last status: {status})"

        # The rejected add stored nothing and evicted nothing.
        assert _list_count(dut) == RELAY_RECLAIM_CAPACITY, \
            "overflowing add must not change the buffer"
        listing = _shell(dut, "relay_reclaim list")
        assert overflow_logger not in listing, \
            f"overflow logger {overflow_logger} must not be stored:\n{listing}"
    finally:
        # Restore a clean buffer so we don't disturb real reclaims or later tests.
        _shell(dut, "relay_reclaim clear_all")
