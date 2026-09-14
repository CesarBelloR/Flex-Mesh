"""Cloud HIL tests for the EXACT Threshold object (48944) via Coiote (FW-1178).

Drives the production cloud path for the immediate-report threshold
configuration: Coiote writes the four configuration resources of a threshold
slot (RIDs 1-4), the firmware validates and stores them in NVS, and the test
reads them back through the Coiote API. Covered here:

  * write + read-back of two slots in one configure task (the Portal example
    from docs/specs/lwm2m-immediate-report-thresholds.md section 8), covering an
    integral and a fractional Threshold Value plus a value that float32 cannot
    represent exactly,
  * persistence of RIDs 1-4 across a cold reboot, together with the volatile
    status resources (Alert RID 5, Trigger Count RID 7) being reset at boot,
  * rejection of out-of-range writes (Value Type outside 1..12, Alert Type
    outside 0..1) with the stored configuration left unchanged.

Trigger behaviour itself (a sensor reading crossing a threshold causing an
immediate report) needs a controlled stimulus and is not covered here.

The DUT is a queue-mode (PSM/eDRX) cellular device, so every configure task is
delivered on a registration update; each task is nudged along with
``app_module trigger_tx`` and can still take minutes. Budget ~20 min for the
file.

Requirements:
  * the board flashed (debug build, no rtt.conf) and **LTE/cloud connected**
  * ``--port`` (the reboot in the persistence test goes over the serial console)
  * Coiote creds via ``--coiote-config`` / ``$COIOTE_CONFIG`` (else the tests skip)

Run:
  pytest tests/hil/test_threshold_coiote.py -v -s \
    --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
    --coiote-config /path/to/etc-tools/coiote_api/config.json
"""

import datetime
import struct
import time

import pytest

from coiote_client import CoioteError, _parse_iso

# Data-model keys derived from the DDF object/resource names in
# applications/.../exact_objects/exact-threshold_48944.xml (object "EXACT
# Threshold", instances 0..3). The DDF must be uploaded to the Coiote tenant
# data model before these keys resolve (spec section 8). Without it, fall back to
# raw keys by setting OBJECT to "48944" and the resource names to their RIDs,
# which gives the dot-separated form "48944.0.2". That is the only raw spelling
# this tenant resolves: the slashed forms "/48944/0/2" and "48944/0/2" are both
# rejected as untranslatable in the tenant dialect.
OBJECT = "EXACT Threshold"
R_ENABLED = "Enabled"
R_VALUE_TYPE = "Value Type"
R_ALERT_TYPE = "Alert Type"
R_THRESHOLD_VALUE = "Threshold Value"
R_ALERT = "Alert"
R_TRIGGER_COUNT = "Trigger Count"

# The four configuration resources, in the order they must be written: Enabled
# last, so the slot arms atomically on a complete configuration (spec 8).
CONFIG_RESOURCES = (R_VALUE_TYPE, R_ALERT_TYPE, R_THRESHOLD_VALUE, R_ENABLED)

# The Portal example configuration, "alert below 10 C or above 20 C" (the upper
# bound nudged to 20.25 C to carry a fraction). The two
# Threshold Values deliberately differ in kind: Coiote encodes a whole number as
# a CBOR integer and a fractional one as a CBOR float, which are separate paths
# through the device-side decoder, so slot 0 stays integral and slot 1 carries a
# fraction (20.25 is exact in float32, so only a decode bug can move it).
SLOT_LOW = 0
SLOT_HIGH = 1
CONFIG_LOW = {R_VALUE_TYPE: "1", R_ALERT_TYPE: "1",
              R_THRESHOLD_VALUE: "10.0", R_ENABLED: "true"}
CONFIG_HIGH = {R_VALUE_TYPE: "1", R_ALERT_TYPE: "0",
               R_THRESHOLD_VALUE: "20.25", R_ENABLED: "true"}

# A value float32 cannot hold exactly; the read-back must be the float32
# neighbour of it, not the decimal written.
ROUNDED_VALUE = "21.1"

# Slot used by the rejection test; kept away from the two configured slots.
SLOT_BAD = 2
BAD_VALUE_TYPE = "13"  # outside 1..12
BAD_ALERT_TYPE = "2"   # outside 0..1

# The device stores Threshold Value as a float32, so the expected read-back is
# the value rounded to single precision; the tolerance then only has to absorb
# the portal's decimal rendering, not the precision loss.
FLOAT_TOL = 1e-6

# Skip if the device hasn't talked to Coiote within this window (it's offline).
MAX_CONTACT_AGE = datetime.timedelta(minutes=20)


# --------------------------------------------------------------------------- #
# Keys and value parsing
# --------------------------------------------------------------------------- #

def _key(slot, resource):
    """Data-model key for one resource of one threshold slot."""
    return f"{OBJECT}.{slot}.{resource}"


def _as_float(value):
    """Parse a portal value as a float, or None if absent/not numeric."""
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _as_float32(value):
    """The value as the device stores it: rounded to IEEE-754 single precision."""
    return struct.unpack("f", struct.pack("f", float(value)))[0]


def _as_bool(value):
    """Parse a portal boolean ('true'/'false', or '1'/'0'), or None."""
    if value is None:
        return None
    text = str(value).strip().lower()
    if text in ("true", "1"):
        return True
    if text in ("false", "0"):
        return False
    return None


def _as_int(value):
    """Parse a portal integer value, or None if absent/not an integer."""
    if value is None:
        return None
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return None


# --------------------------------------------------------------------------- #
# Coiote helpers
# --------------------------------------------------------------------------- #

def _nudge(dut):
    """Provoke a registration update so the queue-mode device checks in."""
    dut.write(b"app_module trigger_tx\r\n")


def _require_online(coiote, device):
    """Pre-flight: skip unless the device contacted Coiote recently."""
    last_contact = coiote.device_last_contact(device)
    if last_contact is None:
        pytest.skip(f"Device {device} not found in Coiote")
    age = datetime.datetime.now(datetime.timezone.utc) - last_contact
    if age > MAX_CONTACT_AGE:
        pytest.skip(f"Device {device} last contacted Coiote {age} ago (offline)")
    print(f"\nDevice {device} last contact {age} ago")
    return age


def _await_cloud_contact(coiote, dut, device, warmup_timeout=180):
    """Return a recent Coiote-contact age, nudging check-ins, or None if offline.

    A queue-mode (PSM/eDRX) device that has just rebooted has not necessarily
    re-registered yet, so give it a warm-up before calling it offline.
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
        _nudge(dut)
        time.sleep(20)


def _drive_task(coiote, dut, device, task_id, op_timeout):
    """Poll a queued task to a terminal report, nudging the device each poll.

    Unlike CoioteClient.wait_for_task this does NOT raise on an Error status —
    a rejected write is an expected outcome for one of the tests here. Returns
    the terminal report.

    The task is intentionally NOT deleted afterward: leaving it on Coiote keeps
    a record of what the test did, which is valuable when investigating runs.
    """
    deadline = time.monotonic() + op_timeout
    report = None
    while time.monotonic() < deadline:
        _nudge(dut)
        time.sleep(20)
        report = coiote.get_task_report(task_id, device)
        if report is not None and report.get("status") in (
                "Success", "Warning", "Error", "ErrorRetryable"):
            return report
    raise CoioteError(
        f"Task {task_id} did not finish within {op_timeout}s "
        f"(last report: {report})")


def _read_fresh(coiote, device, key, start_dt, timeout):
    """Read one cached key, waiting until its value is newer than ``start_dt``.

    The task report says only whether the Read operations succeeded; the values
    themselves are taken from the cached data model, which the Read updates.
    Requiring ``updateTime`` to be at/after the read task's ``startTime`` (minus
    a small skew) is what keeps a stale prior value out of the assertion.
    Returns the last value seen if the freshness deadline passes.
    """
    deadline = time.monotonic() + timeout
    last_value = None
    while True:
        value, update_dt = coiote.read_cached(device, key)
        last_value = value
        if start_dt is None or update_dt is None or update_dt >= start_dt:
            return value
        if time.monotonic() >= deadline:
            print(f"  [coiote] WARNING: {key} still stale after {timeout}s")
            return last_value
        time.sleep(3)


def _read_keys(coiote, dut, device, keys, op_timeout, name="hil-threshold-read"):
    """Run one Read task over ``keys`` and return {key: value-as-string}.

    Mirrors the relay/reclaim tests: the task drives the Reads, the values come
    back out of the cached data model, gated on the task's own start time.
    """
    task_id = coiote.configure_task(
        device, [{"read": {"key": key}} for key in keys], name=name)
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    assert status in ("Success", "Warning"), (
        f"Read task for {list(keys)} ended in {status!r}: {report.get('summary')}")

    start = report.get("startTime")
    start_dt = _parse_iso(start) - datetime.timedelta(seconds=2) if start else None

    values = {}
    for key in keys:
        value = _read_fresh(coiote, device, key, start_dt, min(op_timeout, 60))
        values[key] = None if value is None else str(value)
        print(f"  [coiote] {key} = {values[key]!r}")
    return values


def _write_ops(slot, config):
    """Write operations for one slot, Enabled last (spec section 8).

    Coiote v3 expects every write value as a string, booleans included.
    """
    return [{"write": {"key": _key(slot, res), "value": config[res]}}
            for res in CONFIG_RESOURCES]


def _assert_config(values, slot, config):
    """Assert a read-back {key: value} map matches the configuration written."""
    assert _as_bool(values[_key(slot, R_ENABLED)]) is _as_bool(config[R_ENABLED]), \
        f"slot {slot} Enabled read back as {values[_key(slot, R_ENABLED)]!r}"
    assert _as_int(values[_key(slot, R_VALUE_TYPE)]) == int(config[R_VALUE_TYPE]), \
        f"slot {slot} Value Type read back as {values[_key(slot, R_VALUE_TYPE)]!r}"
    assert _as_int(values[_key(slot, R_ALERT_TYPE)]) == int(config[R_ALERT_TYPE]), \
        f"slot {slot} Alert Type read back as {values[_key(slot, R_ALERT_TYPE)]!r}"
    _assert_threshold_value(values, slot, config[R_THRESHOLD_VALUE])


def _assert_threshold_value(values, slot, written):
    """Assert a slot's Threshold Value reads back as float32(``written``)."""
    got = _as_float(values[_key(slot, R_THRESHOLD_VALUE)])
    want = _as_float32(written)
    assert got is not None and abs(got - want) <= FLOAT_TOL, (
        f"slot {slot} Threshold Value read back as "
        f"{values[_key(slot, R_THRESHOLD_VALUE)]!r}, expected {want!r} "
        f"(float32 of the {written!r} written)")


def _config_keys(slot):
    return [_key(slot, res) for res in CONFIG_RESOURCES]


def _write_example_config(coiote, dut, device, op_timeout):
    """Write the two-slot Portal example configuration in a single task."""
    task_id = coiote.configure_task(
        device,
        _write_ops(SLOT_LOW, CONFIG_LOW) + _write_ops(SLOT_HIGH, CONFIG_HIGH),
        name="hil-threshold-write")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] write task status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Writing the threshold configuration ended in {status!r}. "
        f"Report: {report}")


def _write_threshold_value(coiote, dut, device, slot, value, op_timeout):
    """Write one slot's Threshold Value on its own."""
    task_id = coiote.configure_task(
        device,
        [{"write": {"key": _key(slot, R_THRESHOLD_VALUE), "value": value}}],
        name="hil-threshold-write-value")
    report = _drive_task(coiote, dut, device, task_id, op_timeout)
    status = report.get("status")
    print(f"  [coiote] write slot {slot} Threshold Value={value!r} -> "
          f"status={status!r}")
    assert status in ("Success", "Warning"), (
        f"Writing Threshold Value {value!r} to slot {slot} ended in {status!r}. "
        f"Report: {report}")


def _disable_slots(coiote, dut, device, slots, op_timeout):
    """Best-effort cleanup: disable the given slots so later runs start clean."""
    task_id = None
    try:
        task_id = coiote.configure_task(
            device,
            [{"write": {"key": _key(slot, R_ENABLED), "value": "false"}}
             for slot in slots],
            name="hil-threshold-cleanup")
        report = _drive_task(coiote, dut, device, task_id, op_timeout)
        print(f"  [cleanup] disable slots {list(slots)} -> "
              f"{report.get('status')!r}")
    except Exception as exc:  # cleanup must never fail the test
        print(f"  [cleanup] could not disable slots {list(slots)}: {exc}")
        # Do not leave the task queued to land on the device later, unseen.
        if task_id is not None:
            try:
                coiote.delete_task(task_id)
            except Exception as del_exc:
                print(f"  [cleanup] could not delete task {task_id}: {del_exc}")


# --------------------------------------------------------------------------- #
# Tests
# --------------------------------------------------------------------------- #

def test_threshold_config_write_read_back(coiote, dut, request):
    """Writing a slot's four configuration resources reads back unchanged.

    Covers an integral and a fractional Threshold Value (different CBOR
    encodings on the wire) plus one value that float32 must round.
    """
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    _require_online(coiote, device)

    try:
        _write_example_config(coiote, dut, device, op_timeout)

        values = _read_keys(coiote, dut, device,
                            _config_keys(SLOT_LOW) + _config_keys(SLOT_HIGH),
                            op_timeout)
        _assert_config(values, SLOT_LOW, CONFIG_LOW)
        _assert_config(values, SLOT_HIGH, CONFIG_HIGH)

        # Same slot again with a value float32 cannot represent exactly: the
        # read-back must land on its float32 neighbour, not drift further.
        _write_threshold_value(coiote, dut, device, SLOT_HIGH, ROUNDED_VALUE,
                               op_timeout)
        values = _read_keys(coiote, dut, device,
                            [_key(SLOT_HIGH, R_THRESHOLD_VALUE)], op_timeout,
                            name="hil-threshold-read-rounded")
        _assert_threshold_value(values, SLOT_HIGH, ROUNDED_VALUE)
    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch, "
                    f"DDF not uploaded to the tenant?): {exc}")
    finally:
        _disable_slots(coiote, dut, device, (SLOT_LOW, SLOT_HIGH), op_timeout)


def test_threshold_config_survives_reboot(coiote, dut, request):
    """RIDs 1-4 persist in NVS; the volatile RIDs 5/7 reset at boot."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    _require_online(coiote, device)

    try:
        _write_example_config(coiote, dut, device, op_timeout)

        # Cold boot with console-reader retries; returns once etc_settings
        # has loaded from NVS.
        dut.cold_boot()
        if _await_cloud_contact(coiote, dut, device) is None:
            pytest.skip(f"Device {device} did not contact Coiote after reboot")

        keys = _config_keys(SLOT_LOW) + _config_keys(SLOT_HIGH) + [
            _key(SLOT_LOW, R_ALERT), _key(SLOT_LOW, R_TRIGGER_COUNT),
            _key(SLOT_HIGH, R_ALERT), _key(SLOT_HIGH, R_TRIGGER_COUNT),
        ]
        values = _read_keys(coiote, dut, device, keys, op_timeout,
                            name="hil-threshold-read-after-reboot")

        _assert_config(values, SLOT_LOW, CONFIG_LOW)
        _assert_config(values, SLOT_HIGH, CONFIG_HIGH)

        # Volatile status resources are reset by the boot we just did.
        for slot in (SLOT_LOW, SLOT_HIGH):
            alert = _as_bool(values[_key(slot, R_ALERT)])
            assert alert is False, (
                f"slot {slot} Alert is {values[_key(slot, R_ALERT)]!r} on a "
                f"freshly booted device, expected false (volatile, RID 5)")
            count = _as_int(values[_key(slot, R_TRIGGER_COUNT)])
            assert count == 0, (
                f"slot {slot} Trigger Count is "
                f"{values[_key(slot, R_TRIGGER_COUNT)]!r} on a freshly booted "
                f"device, expected 0 (volatile, RID 7)")
    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch, "
                    f"DDF not uploaded to the tenant?): {exc}")
    finally:
        _disable_slots(coiote, dut, device, (SLOT_LOW, SLOT_HIGH), op_timeout)


@pytest.mark.parametrize("resource,bad_value", [
    (R_VALUE_TYPE, BAD_VALUE_TYPE),
    (R_ALERT_TYPE, BAD_ALERT_TYPE),
])
def test_threshold_out_of_range_write_rejected(coiote, dut, request,
                                               resource, bad_value):
    """An out-of-range write fails (CoAP 4.00) and changes nothing on device."""
    device = request.config.getoption("--coiote-device")
    op_timeout = request.config.getoption("--coiote-op-timeout")

    _require_online(coiote, device)

    key = _key(SLOT_BAD, resource)

    try:
        # Baseline first, so the test is self-contained: whatever the slot holds
        # now is what an invalid write must leave in place.
        before = _read_keys(coiote, dut, device, [key], op_timeout,
                            name="hil-threshold-bad-baseline")[key]

        # One write op only, so the task status reflects solely this write (a
        # trailing read could otherwise turn a rejection into a "Warning").
        try:
            task_id = coiote.configure_task(
                device, [{"write": {"key": key, "value": bad_value}}],
                name="hil-threshold-bad-write")
        except CoioteError as exc:
            pytest.skip(
                f"Coiote refused to queue the out-of-range write itself "
                f"({exc}) — the tenant validates the DDF RangeEnumeration "
                f"before the value reaches the device, so the on-device guard "
                f"cannot be exercised through named keys. Re-run against raw "
                f"LwM2M path keys to cover it.")

        report = _drive_task(coiote, dut, device, task_id, op_timeout)
        status = report.get("status")
        print(f"  [coiote] write {key}={bad_value!r} -> status={status!r}")
        assert status in ("Error", "ErrorRetryable"), (
            f"Writing {bad_value!r} to {key} is out of range and must be "
            f"rejected with 4.00 Bad Request, but the task ended in {status!r}. "
            f"Report: {report}")

        # The rejected write must leave the stored configuration untouched.
        after = _read_keys(coiote, dut, device, [key], op_timeout,
                           name="hil-threshold-bad-readback")[key]
        assert _as_int(after) == _as_int(before), (
            f"{key} changed from {before!r} to {after!r} after a rejected "
            f"write of {bad_value!r}; a rejected write must leave the stored "
            f"configuration unchanged")
        assert _as_int(after) != _as_int(bad_value), (
            f"{key} holds the rejected out-of-range value {after!r}")
    except CoioteError as exc:
        pytest.fail(f"Coiote E2E failed (device offline or key/dialect mismatch, "
                    f"DDF not uploaded to the tenant?): {exc}")
