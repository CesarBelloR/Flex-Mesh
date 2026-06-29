"""HIL tests for Flex Lite / Flex Embeddable port mapping over LoRa (FW-1062).

Companion to ``test_lite_embeddable.py`` (BLE) and ``test_lora_splitter.py`` (the
regular 4-port logger). Same harness as the LoRa splitter test: the relay is the
primary ``--port`` DUT (device mode 0) and the LoRa logger console is
``--logger-port`` (device mode 1).

A Lite/Embeddable logger remaps its 2-way splitter sub-ports (1.B=IN5, 2.B=IN6)
into IN3/IN4 and clears IN5-IN8 (``etc_device_map_two_port_sensor_data``), so its
LoRa CSV is the regular layout with **no trailing splitter block** - even when a
4-way splitter is physically attached::

    S, parent, version, ID, battery, pkt_num, timestamp,
    T1(port1), T2(port2), T3(port1.B), T4(port2.B), ambient, humidity

That is the distinguishing assertion: a regular logger (prefix 10) with a
splitter attached emits 17 fields (T1.B-T4.B appended); a 2-port device emits at
most 13 and never the splitter block.

Automatic serial-number switching
---------------------------------
The test drives the *logger's* device id through each Lite/Embeddable prefix
(set serial-number type to EXACT_INFO + ``set_device_id <prefix> <serial>``). No
reboot is needed: the remap reads the device type live per record. The original
serial-number type and id are captured once and **restored on teardown whether
the tests pass or fail**. Device modes are a prerequisite (set once on the
hardware), not changed by the test, so they need no restore.
"""

import time

import pytest

from test_lora_splitter import (  # shared LoRa harness helpers
    LOGGER_FIELDS_WITH_SPLITTER,
    MAX_ATTEMPTS,
    MIN_LOGGER_FIELDS,
    RELAY_SPLITTER,
    TEMP_TOLERANCE_C,
    _clean,
    _relay_cmd,
    _valid_temp_or_disconnected,
    logger,  # noqa: F401 - re-exported so pytest resolves the `logger` fixture
)
from test_lite_embeddable import _split_id

LITE_EMBEDDABLE_PREFIXES = [12, 13, 14, 15]
SERIAL_TYPE_EXACT_INFO = 1

# Logger CSV positions of the remapped sub-ports (T3/T4) and the relay's parsed
# sensor indices for the main ports IN3/IN4.
CSV_PORT1_B = 9
CSV_PORT2_B = 10
RELAY_IN3 = 2
RELAY_IN4 = 3

# Captured once before the first prefix switch so teardown restores the TRUE
# original even if an intermediate restore misbehaved.
_LORA_ORIGINAL = {}


def _logger_serial_info(logger):
    """Return (active_type, serial_number) from the logger's ``settings info``.

    Only the Serial Number line is matched: a second expect for the trailing HW
    ID line would hang, since SerialConsole.expect only scans the buffer after a
    fresh read and the device goes idle once ``settings info`` has printed.
    """
    logger.drain()
    logger.send("settings info")
    m = logger.expect(r"Serial Number(\[\*\] )?: (\S+)", 10)
    active = m.group(1) is not None
    serial_number = _clean(m.group(2)).strip()
    return (SERIAL_TYPE_EXACT_INFO if active else 0), serial_number


def _require_lora_roles(dut, logger):
    """Skip unless the relay is device mode 0 and the logger device mode 1."""
    dut.write(b"settings get_device\r\n")
    if int(dut.expect(r"Device mode (\d+)", timeout=10).group(1)) != 0:
        pytest.skip("primary --port DUT is not the relay (set 'settings set_device 0' "
                    "and reboot)")
    logger.send("settings get_device")
    if int(logger.expect(r"Device mode (\d+)", 10).group(1)) != 1:
        pytest.skip("--logger-port DUT is not the LoRa logger (set "
                    "'settings set_device 1' and reboot)")


@pytest.fixture
def lora_id_guard(logger):
    """Capture the logger's original serial id/type once; restore on teardown."""
    if not _LORA_ORIGINAL:
        otype, oid = _logger_serial_info(logger)
        _LORA_ORIGINAL.update(type=otype, id=oid)
        print(f"captured original logger id={oid}, serial-type {otype}")
    orig_prefix, orig_serial = _split_id(_LORA_ORIGINAL["id"])

    try:
        yield orig_serial
    finally:
        try:
            logger.send(f"settings set_device_id {int(orig_prefix):02d} {int(orig_serial)}")
            logger.expect(r"OK", 10)
        except Exception as exc:  # noqa: BLE001 - keep going to restore the type
            print(f"WARNING: failed to restore logger device id: {exc}")
        try:
            logger.send(f"settings set_serial_type {_LORA_ORIGINAL['type']}")
            time.sleep(0.5)
        except Exception as exc:  # noqa: BLE001
            print(f"WARNING: failed to restore logger serial type: {exc}")
        _, restored_id = _logger_serial_info(logger)
        print(f"restored logger id={restored_id}")
        assert restored_id == _LORA_ORIGINAL["id"], \
            f"failed to restore original logger id: {restored_id} != {_LORA_ORIGINAL['id']}"


def _tx_fields_for_prefix(logger, prefix, tx_attempts=4, per_msg_timeout=20):
    """Trigger TX and return the first ``Msg`` whose id field carries ``prefix``.

    The logger also transmits autonomously on its schedule, so a freshly issued
    ``set_device_id`` can race a queued/scheduled TX still carrying the old id;
    and a periodic log wakeup can occasionally swallow a manual trigger. Filter
    by the expected id prefix and re-trigger so neither makes the test flaky.
    """
    prefix_str = f"{int(prefix):02d}"
    for _ in range(tx_attempts):
        logger.drain()
        logger.send("app_module trigger_tx")
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                m = logger.expect(r"Msg (S,[^\r\n]+)\r?\n", timeout=per_msg_timeout)
            except TimeoutError:
                break
            fields = _clean(m.group(1)).strip().rstrip(",").split(",")
            if len(fields) > 3 and fields[3].startswith(prefix_str):
                return fields
            print(f"  (skipping stale TX id={fields[3] if len(fields) > 3 else '?'})")
    raise AssertionError(f"no logger TX with id prefix {prefix_str} captured")


@pytest.fixture
def lora_prefix(logger, lora_id_guard, request):
    """Set the logger id to ``request.param`` (a Lite/Embeddable prefix)."""
    prefix = request.param
    logger.send(f"settings set_serial_type {SERIAL_TYPE_EXACT_INFO}")
    time.sleep(0.5)
    logger.send(f"settings set_device_id {prefix} {int(lora_id_guard)}")
    logger.expect(r"OK", 10)
    return prefix


@pytest.mark.parametrize("lora_prefix", LITE_EMBEDDABLE_PREFIXES, indirect=True)
def test_logger_tx_two_port_layout(dut, logger, lora_prefix):
    """A Lite/Embeddable logger transmits the regular layout with no splitter
    block over LoRa, carrying 1.B/2.B in the T3/T4 positions."""
    _require_lora_roles(dut, logger)
    fields = _tx_fields_for_prefix(logger, lora_prefix)
    print(f"prefix {lora_prefix}: logger TX {len(fields)} fields: {fields}")

    assert fields[0] == "S", f"first field should be 'S', got {fields[0]!r}"
    # The defining invariant: no splitter block, even with a 4-way splitter wired.
    assert len(fields) < LOGGER_FIELDS_WITH_SPLITTER, \
        (f"prefix {lora_prefix}: 2-port logger must omit the splitter block, got "
         f"{len(fields)} fields: {fields}")
    assert len(fields) >= MIN_LOGGER_FIELDS - 1, \
        f"prefix {lora_prefix}: truncated logger CSV: {fields}"

    # T1-T4 (port1, port2, port1.B, port2.B), ambient, humidity.
    for idx in range(7, min(len(fields), 13)):
        assert _valid_temp_or_disconnected(fields[idx]), \
            f"field {idx} has invalid value {fields[idx]!r}"


@pytest.mark.parametrize("lora_prefix", [13], indirect=True)
def test_relay_decodes_two_port_layout(dut, logger, lora_prefix):
    """The relay receives a Lite logger packet over LoRa and decodes it with no
    splitter sub-ports: IN5-IN8 read back disconnected and 1.B/2.B arrive in the
    IN3/IN4 slots.

    Asserts on the relay's decoded ``Sensor`` line (logged on reception). The
    relay->cloud forward is intentionally not checked: that path needs a modem
    the bench relay lacks (it logs "Can't prepare packet for relay"), and the
    forwarding itself is unchanged by FW-1062 - the port mapping happens on the
    logger before TX.
    """
    _require_lora_roles(dut, logger)

    last = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        _relay_cmd(dut, "app_module trigger_rx", r"Triggering relay LoRa RX")
        time.sleep(1)
        fields = _tx_fields_for_prefix(logger, lora_prefix)
        last = fields

        assert len(fields) < LOGGER_FIELDS_WITH_SPLITTER, \
            f"prefix {lora_prefix}: 2-port logger emitted a splitter block: {fields}"
        port1_b, port2_b = fields[CSV_PORT1_B], fields[CSV_PORT2_B]

        # The relay logs the decoded per-port temps on reception. Match the id so
        # a stale prefix-10 packet from before the switch is not mistaken for ours.
        try:
            relay_id = dut.expect(r"Logger ID (\d+)", timeout=30).group(1)
            sensors_raw = dut.expect(r"Sensor ([0-9.\- ]+)", timeout=10).group(1)
        except Exception:  # noqa: BLE001 - lossy LoRa, re-arm
            print(f"attempt {attempt}: relay did not receive the packet, retrying")
            continue

        relay_id = _clean(relay_id).strip()
        sensors = _clean(sensors_raw).split()
        if not relay_id.startswith(f"{lora_prefix}"):
            print(f"attempt {attempt}: relay got stale id {relay_id}, retrying")
            continue
        if len(sensors) < 8:
            print(f"attempt {attempt}: short relay sensor line {sensors}, retrying")
            continue

        # No splitter sub-ports: IN5-IN8 must all be disconnected (large negative).
        in5_8 = [float(v) for v in sensors[RELAY_SPLITTER]]
        no_splitter = all(v < -200 for v in in5_8)
        # 1.B/2.B mapped into IN3/IN4 must match what the logger transmitted in
        # the T3/T4 positions (within tolerance; '*' <-> disconnected).
        mapped_ok = (_relay_matches(sensors[RELAY_IN3], port1_b) and
                     _relay_matches(sensors[RELAY_IN4], port2_b))

        if no_splitter and mapped_ok:
            print(f"attempt {attempt}: relay (id {relay_id}) parsed "
                  f"IN3/IN4={sensors[RELAY_IN3]},{sensors[RELAY_IN4]} "
                  f"(logger T3/T4={port1_b},{port2_b}); IN5-8 disconnected={in5_8}")
            return

        print(f"attempt {attempt}: no_splitter={no_splitter} mapped_ok={mapped_ok} "
              f"(sensors={sensors}, logger T3/T4={port1_b},{port2_b}), retrying")

    pytest.fail(f"relay never cleanly received/decoded the 2-port packet after "
                f"{MAX_ATTEMPTS} attempts; last logger TX: {last}")


def _relay_matches(relay_val, logger_val):
    """Relay sensor value matches the logger field (disconnected <-> '*')."""
    relay_val = float(relay_val)
    logger_val = logger_val.strip()
    if logger_val == "*":
        return relay_val < -200
    return abs(relay_val - float(logger_val)) <= TEMP_TOLERANCE_C
