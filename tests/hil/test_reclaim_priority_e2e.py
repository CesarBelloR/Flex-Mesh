"""End-to-end HIL test for FW-954: current readings preempt an active reclaim.

Scenario on a live LTE logger:

  1. Schedule a reclaim over historical (already-sent) data via the device shell
     (``record reclaim <start> <stop>``). This re-queues a backlog of old records
     for re-transmission.
  2. Inject fresh readings via the shell (``app_module trigger_tx``).
  3. Confirm through the Coiote API that the fresh readings reach the cloud *while
     the reclaim is still IN_PROGRESS* -- the cloud's latest reading Timestamp
     (EXACT Temperature 48932/0/5518) becomes recent, not stuck in the old
     reclaim window -- i.e. new readings take precedence. Then confirm the reclaim
     still reaches COMPLETE (EXACT Reclaim 48934/0/4), proving no data is lost.

The rigorous per-record ordering and data-integrity proof lives in the Twister
unit test ``tests/applications/etc-app/etc_device`` (``test_10``/``test_11``);
this test validates the same behaviour over the real cloud round trip.

The test puts the DUT into **LTE logger** mode first (FW-954 only changes the
logger record path; relays use a separate reclaim path). If a mode change is
needed it reboots the device and waits for it to re-attach to Coiote.

Requirements:
  * the board flashed (debug build) and **LTE/cloud connected** to Coiote
  * ``--port`` for the shell
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the test skips)
  * the EXACT Temperature (48932) and EXACT Reclaim (48934) objects present in the
    tenant data model (else the test skips -- see FW-991)

Run:
  pytest tests/hil/test_reclaim_priority_e2e.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import re
import time

import pytest

from coiote_client import CoioteError, _parse_iso

# Cached data-model keys, addressed by DDF object/resource name (the convention
# used by test_relay_command_coiote.py). If the tenant addresses resources by raw
# LwM2M path instead, switch to "/48932/0/5518" and "/48934/0/4".
READING_TS_KEY = "EXACT Temperature.0.Timestamp"  # 48932/0/5518, measurement time
RECLAIM_STATUS_KEY = "EXACT Reclaim.0.Status"     # 48934/0/4

# EXACT Reclaim status values (etc_reclaim_obj_48934.h).
RECLAIM_IDLE = 0
RECLAIM_IN_PROGRESS = 1
RECLAIM_COMPLETE = 2
RECLAIM_ERROR = 3

# Device mode for an LTE logger (etc_device.h: ETC_DEVICE_MODE_LTE_LOGGER).
LTE_LOGGER_MODE = 2

# How many fresh readings to inject while the reclaim runs.
NUM_FRESH = 3

# Seeding: trigger a burst of readings so the logger holds reclaimable history
# with timestamps just before the fresh readings. Each trigger samples + stores
# (and sends) one record; spacing spreads their timestamps.
SEED_COUNT = 8
SEED_SPACING = 6      # seconds between seeded readings
SEED_SETTLE = 8       # seconds to let the last seeded record settle

# The DUT is a queue-mode (PSM) cellular device: between scheduled tx windows it
# sleeps and does not transmit. Drop the tx interval for the test so the device
# wakes and uplinks frequently enough to observe within the poll timeout; the
# original value is restored afterwards.
TEST_TX_INTERVAL = 60

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


# --------------------------------------------------------------------------- #
# Shell helpers (tolerant of interleaved log output)
# --------------------------------------------------------------------------- #

def _drain(dut, settle=0.5):
    """Discard any pending serial output (incl. a stale prompt) before a cmd."""
    time.sleep(settle)
    try:
        while True:
            dut.expect(r".+", timeout=0.4)
    except Exception:
        pass


def _shell(dut, cmd, timeout=10):
    """Run a shell command and return its output, anchored on the command echo."""
    _drain(dut)
    dut.write(f"{cmd}\r\n".encode())
    dut.expect(re.escape(cmd), timeout=timeout)  # consume this command's echo
    dut.expect(r"uart:~\$", timeout=timeout)
    before = dut.pexpect_proc.before
    if isinstance(before, bytes):
        before = before.decode("utf-8", errors="replace")
    return before


# --------------------------------------------------------------------------- #
# Coiote helpers
# --------------------------------------------------------------------------- #

def _reading_epoch(value):
    """Coerce a cached Time-resource value to epoch seconds (int).

    Coiote may render an LwM2M Time resource either as a numeric epoch or as an
    ISO-8601 string, depending on tenant config; accept both.
    """
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return int(_parse_iso(value).timestamp())


def _reclaim_status(coiote, device):
    value, _ = coiote.read_cached(device, RECLAIM_STATUS_KEY)
    return int(float(value))


def _latest_reading_ts(coiote, device):
    value, update = coiote.read_cached(device, READING_TS_KEY)
    return _reading_epoch(value), update


def _device_mode(dut):
    out = _shell(dut, "settings get_device")
    m = re.search(r"Device mode\s+(\d+)", out)
    assert m, f"could not read device mode:\n{out}"
    return int(m.group(1))


def _ensure_lte_logger_mode(dut):
    """Put the DUT into LTE logger mode. Returns True if a reboot was performed.

    A mode change persists the setting but the transport stack is selected at
    boot, so the device must be rebooted (and then re-attach to the cloud).
    """
    if _device_mode(dut) == LTE_LOGGER_MODE:
        return False
    out = _shell(dut, f"settings set_device {LTE_LOGGER_MODE}")
    assert "successful" in out.lower(), f"failed to set LTE logger mode:\n{out}"
    dut.reboot()
    assert _device_mode(dut) == LTE_LOGGER_MODE, "device did not enter LTE logger mode"
    return True


def _get_tx_interval(dut):
    out = _shell(dut, "settings get_tx_interval")
    m = re.search(r"Tx interval in seconds\s+(\d+)", out)
    assert m, f"could not read tx interval:\n{out}"
    return int(m.group(1))


def _set_tx_interval(dut, secs):
    _shell(dut, f"settings set_tx_interval {secs}")


def _wait_cloud_connected(coiote, device, timeout):
    """Return the contact age once the device is online, else None on timeout."""
    deadline = time.monotonic() + timeout
    while True:
        last = coiote.device_last_contact(device)
        if last is not None:
            age = datetime.datetime.now(datetime.timezone.utc) - last
            if age <= MAX_CONTACT_AGE:
                return age
        if time.monotonic() >= deadline:
            return None
        time.sleep(15)


# --------------------------------------------------------------------------- #
# Test
# --------------------------------------------------------------------------- #

def test_current_reading_preempts_reclaim_e2e(coiote, dut, request):
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")
    complete_timeout = request.config.getoption("--e2e-timeout") * 60

    # Ensure the DUT is an LTE logger before anything else: FW-954 only changes
    # the logger record path, and the cloud cross-check needs the LTE transport.
    # A mode change reboots the device, after which it must re-attach to Coiote.
    rebooted = _ensure_lte_logger_mode(dut)
    # Allow time to re-attach: a full window after a mode-change reboot, or a
    # shorter grace period otherwise (the device may still be mid-reattach, e.g.
    # just after a flash).
    reconnect_timeout = complete_timeout if rebooted else min(complete_timeout, 300)
    age = _wait_cloud_connected(coiote, device, reconnect_timeout)
    if age is None:
        pytest.skip(f"Device {device} not connected to Coiote (offline)")
    print(f"\nDevice {device} is an LTE logger, last contact {age} ago")

    # Confirm the resources we need exist in this tenant's data model; if the
    # EXACT objects aren't provisioned (FW-991), skip rather than hard-fail.
    try:
        _reclaim_status(coiote, device)
        _latest_reading_ts(coiote, device)
    except CoioteError as exc:
        pytest.skip(f"Required EXACT object not in tenant data model: {exc}")

    orig_tx_interval = _get_tx_interval(dut)
    _set_tx_interval(dut, TEST_TX_INTERVAL)
    try:
        _run_reclaim_priority(dut, coiote, device, op_timeout, complete_timeout)
    finally:
        _set_tx_interval(dut, orig_tx_interval)


def _run_reclaim_priority(dut, coiote, device, op_timeout, complete_timeout):
    # 1. Seed reclaimable history: a burst of readings whose timestamps land
    #    just before the fresh readings triggered later. (The precedence check
    #    distinguishes new vs reclaimed purely by the reclaim window's upper
    #    bound, so the seeded records need not be acknowledged first.)
    seed_start = int(time.time())
    for _ in range(SEED_COUNT):
        _shell(dut, "app_module trigger_tx")
        time.sleep(SEED_SPACING)
    seed_end = int(time.time())
    time.sleep(SEED_SETTLE)

    # 2. Schedule a reclaim covering the seeded records but stopping before any
    #    fresh reading. Margins absorb small device/host clock skew.
    win_start = seed_start - 10
    win_stop = seed_end + 3
    fresh_after = win_stop  # a fresh reading's Timestamp must exceed the window
    out = _shell(dut, f"record reclaim {win_start} {win_stop}")
    m = re.search(r"Start ID\s+(-?\d+)\s*-\s*Stop\s+(-?\d+)", out)
    assert m, f"reclaim was not scheduled:\n{out}"
    start_id, stop_id = int(m.group(1)), int(m.group(2))
    assert start_id >= 0 and stop_id >= 0, (
        f"seeded reclaim window matched no records (ids {start_id}..{stop_id}); "
        f"seeding or clock alignment failed:\n{out}")
    print(f"Reclaim scheduled over record ids {start_id}..{stop_id}")

    # 3. Inject fresh readings (strictly newer than the window) while the
    #    reclaim is pending.
    while int(time.time()) <= win_stop:
        time.sleep(1)
    for _ in range(NUM_FRESH):
        _shell(dut, "app_module trigger_tx")
        time.sleep(2)

    # 4. Precedence: the cloud's latest reading Timestamp must become newer than
    #    the window (a fresh reading) while the reclaim is still IN_PROGRESS. If
    #    precedence were broken, the cloud would keep showing old reclaim-window
    #    timestamps until the whole backlog drained. Re-injecting a reading each
    #    poll also forces the queue-mode device to wake and uplink.
    deadline = time.monotonic() + op_timeout
    fresh_seen = False
    reclaim_active_when_fresh = False
    while time.monotonic() < deadline:
        _shell(dut, "app_module trigger_tx")
        reading_ts, _ = _latest_reading_ts(coiote, device)
        status = _reclaim_status(coiote, device)
        print(f"  poll: reading_ts={reading_ts} fresh_after={fresh_after} status={status}")
        if reading_ts >= fresh_after:
            fresh_seen = True
            reclaim_active_when_fresh = status == RECLAIM_IN_PROGRESS
            print(f"Fresh reading ts={reading_ts} reached cloud, reclaim status={status}")
            break
        time.sleep(20)

    assert fresh_seen, (
        "a fresh reading never reached the cloud within the timeout; new readings "
        "did not preempt the reclaim backlog")
    assert reclaim_active_when_fresh, (
        "the fresh reading only appeared after the reclaim had already finished, "
        "so precedence cannot be proven; rerun with a wider reclaim window")

    # 5. Data integrity: the reclaim backlog must still complete.
    deadline = time.monotonic() + complete_timeout
    final_status = None
    while time.monotonic() < deadline:
        final_status = _reclaim_status(coiote, device)
        assert final_status != RECLAIM_ERROR, "reclaim reported ERROR status"
        if final_status == RECLAIM_COMPLETE:
            break
        time.sleep(15)

    assert final_status == RECLAIM_COMPLETE, (
        f"reclaim did not reach COMPLETE within {complete_timeout}s "
        f"(last status {final_status}); backlog may not have been fully delivered")
    print("Reclaim reached COMPLETE: backlog delivered, no readings lost")
