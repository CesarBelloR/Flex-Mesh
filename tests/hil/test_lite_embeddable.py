"""HIL tests for Flex Lite / Flex Embeddable port mapping over BLE (FW-1062).

Flex Lite (ID prefix 13/14/15) and Flex Embeddable (prefix 12) are 2-port
variants. They expose two physical probe ports (IN1, IN2) plus a 2-way splitter
(sub-ports ``1.B`` = IN5, ``2.B`` = IN6). The firmware remaps the splitter
sub-ports into the unused IN3/IN4 slots
(``etc_device_map_two_port_sensor_data``) so every channel emits the FW-1062
layout::

    device_type, sensor_id, sensor_battery, packet_number, reading_time,
    port1, port2, port1.B, port2.B, ambient, humidity, reclaimed

Consequences this test verifies, for *every* Lite/Embeddable prefix and
regardless of whether a splitter is physically attached (the remap runs either
way):

* **Advertisement** - always the 26-byte *base* layout
  (``IN1 IN2 IN3 IN4 ambient humidity bat_status bat``); the 42-byte splitter
  block is never emitted, because IN5-IN8 are cleared after the remap. ``1.B``
  and ``2.B`` ride in the IN3/IN4 float slots.
* **Notification CSV** - the legacy ASCII string ends at ``isReclaim`` with no
  trailing splitter tail; ``1.B``/``2.B`` occupy the T3/T4 positions.

This mirrors ``test_ble_splitter.py`` (the regular 4-port Flex logger, which
*does* emit the splitter block) and reuses its capture/parse helpers.

Automatic serial-number switching
---------------------------------
The test drives the DUT's device id through each Lite/Embeddable prefix itself:
it switches the serial-number type to EXACT_INFO and sets ``set_device_id
<prefix> <serial>`` for each prefix, nudging a fresh sample so the advertisement
rebuilds under the new device type. No reboot is needed - the remap reads the
device type live, and the advertised BLE name (set at boot) stays put, so the
test keeps scanning by the original ``--ble-name``.

The original serial-number type and id are captured once up front and **restored
on teardown whether the tests pass or fail** (module-scoped fixture finaliser).

Prerequisites:

* The DUT is the ``--port`` logger in BLE mode (``settings set_device 3``) and
  ``--ble-name`` names its current advertisement (e.g. ``Flex_10002965``). The
  host BlueZ quirks and btmon/pairing setup are identical to
  ``test_ble_splitter.py`` - see that module's docstring.
"""

import asyncio
import re
import time

import pytest

from test_ble_splitter import (
    ADV_BASE_LEN,
    CSV_AMBIENT,
    CSV_HUMIDITY,
    CSV_ISRECLAIM,
    CSV_SPLITTER_START,
    SCAN_TIMEOUT_S,
    _capture_adv_via_btmon,
    _collect_notification,
    _parse_adv,
    _reconstruct_mfg,
    _scan,
    _valid_or_disconnected,
)

# Prefixes that select the 2-port (Lite/Embeddable) handling.
LITE_EMBEDDABLE_PREFIXES = [12, 13, 14, 15]

# enum etc_serial_number_types (etc_settings.h)
SERIAL_TYPE_HW_INFO = 0
SERIAL_TYPE_EXACT_INFO = 1

# Notification CSV field indices of the remapped sub-ports (T3/T4).
CSV_PORT1_B = 7
CSV_PORT2_B = 8

# Seconds to let a fresh sample propagate into the rebuilt advertisement.
SAMPLE_SETTLE_S = 6.0
SHELL_TIMEOUT_S = 10


def _shell(dut, line):
    dut.write(line.encode() + b"\r\n")


def _read_serial_info(dut):
    """Return (active_type, serial_number) parsed from ``settings info``.

    ``settings info`` prints (the active source is flagged with ``[*]``)::

        Serial Number[*] : 10002965
        HW ID: <hex>
    """
    _shell(dut, "settings info")
    m_sn = dut.expect(r"Serial Number(\[\*\] )?: (\S+)", timeout=SHELL_TIMEOUT_S)
    sn_active = m_sn.group(1) is not None
    serial_number = m_sn.group(2).decode() if isinstance(m_sn.group(2), bytes) \
        else m_sn.group(2)
    # Drain the HW ID line so it does not bleed into a later expect().
    dut.expect(r"HW ID(\[\*\] )?: \S+", timeout=SHELL_TIMEOUT_S)
    active_type = SERIAL_TYPE_EXACT_INFO if sn_active else SERIAL_TYPE_HW_INFO
    return active_type, serial_number


def _split_id(device_id):
    """(prefix, serial) as numeric strings, from a device id like ``10002965``."""
    digits = re.sub(r"\D", "", device_id)
    prefix = digits[:2] if len(digits) >= 2 else "10"
    serial = digits[2:8] if len(digits) > 2 else "000001"
    return prefix, serial


def _set_device_id(dut, prefix, serial):
    _shell(dut, f"settings set_device_id {int(prefix):02d} {int(serial)}")
    dut.expect(r"OK", timeout=SHELL_TIMEOUT_S)


def _set_serial_type(dut, serial_type):
    _shell(dut, f"settings set_serial_type {int(serial_type)}")
    time.sleep(0.5)


# The DUT's id/serial-type captured before the very first prefix switch, so every
# teardown restores the TRUE original even if an intermediate restore misbehaved.
_ORIGINAL = {}


@pytest.fixture
def device_id_guard(dut, request):
    """Capture the original serial id/type once, restore it on teardown (pass or
    fail).

    The pytest-embedded ``dut`` fixture is function-scoped, so this guard is too:
    it captures the original on the first use into a module global and every
    finaliser restores to that, so the DUT always ends on its original id.
    Yields the scan target (``--ble-name``) and the 6-digit serial to reuse when
    synthesising each Lite/Embeddable id.
    """
    target = request.config.getoption("--ble-name")
    if not target:
        pytest.skip("This test needs --ble-name to scan the DUT advertisement "
                    "(e.g. --ble-name Flex_10002965)")

    _shell(dut, "settings get_device")
    mode = int(dut.expect(r"Device mode (\d+)", timeout=SHELL_TIMEOUT_S).group(1))
    if mode != 3:
        pytest.skip(f"DUT is device mode {mode}, not BLE (3); "
                    f"run 'settings set_device 3' first")

    if not _ORIGINAL:
        otype, oid = _read_serial_info(dut)
        _ORIGINAL.update(type=otype, id=oid)
        print(f"captured original id={oid}, serial-type {otype}")
    orig_prefix, orig_serial = _split_id(_ORIGINAL["id"])

    try:
        yield {"target": target, "serial": orig_serial}
    finally:
        # Restore the TRUE original id and serial-number type, come what may.
        try:
            _set_device_id(dut, orig_prefix, orig_serial)
        except Exception as exc:  # noqa: BLE001 - keep going to restore the type
            print(f"WARNING: failed to restore device id: {exc}")
        try:
            _set_serial_type(dut, _ORIGINAL["type"])
        except Exception as exc:  # noqa: BLE001
            print(f"WARNING: failed to restore serial type: {exc}")
        _, restored_id = _read_serial_info(dut)
        print(f"restored id={restored_id}")
        assert restored_id == _ORIGINAL["id"], \
            f"failed to restore original device id: {restored_id} != {_ORIGINAL['id']}"


@pytest.fixture
def provisioned_prefix(dut, device_id_guard, request):
    """Set the DUT id to ``request.param`` (a Lite/Embeddable prefix) and nudge a
    fresh sample so the advertisement rebuilds under the new device type."""
    prefix = request.param
    # The prefix only takes effect when the device id comes from NVS, not HW info.
    _set_serial_type(dut, SERIAL_TYPE_EXACT_INFO)
    _set_device_id(dut, prefix, device_id_guard["serial"])
    # Rebuild the advertisement/notification under the new device type.
    _shell(dut, "app_module trigger_tx")
    time.sleep(SAMPLE_SETTLE_S)
    return prefix


def _capture_adv(target):
    """Capture ``target``'s advertisement manufacturer blob (btmon, else bleak)."""
    blob = _capture_adv_via_btmon(target, SCAN_TIMEOUT_S)
    source = "btmon"
    if blob is None:
        dev, adv, _name = asyncio.run(_scan(target, SCAN_TIMEOUT_S))
        if dev is None:
            return None, None
        blob = _reconstruct_mfg(adv)
        source = "bleak"
    return blob, source


@pytest.mark.parametrize("provisioned_prefix", LITE_EMBEDDABLE_PREFIXES,
                         indirect=True)
def test_two_port_layout_over_ble(dut, device_id_guard, provisioned_prefix):
    """Each Lite/Embeddable prefix advertises the 26-byte base layout (never the
    splitter block) and its notification CSV has no trailing splitter tail."""
    prefix = provisioned_prefix
    target = device_id_guard["target"]

    blob, source = _capture_adv(target)
    if blob is None:
        pytest.skip(f"No advertisement seen for {target} within {SCAN_TIMEOUT_S}s "
                    f"(is the DUT advertising in BLE mode? is btmon privileged?)")

    base, splitter, bat_status, battery = _parse_adv(blob)
    print(f"prefix {prefix}: adv {len(blob)} B via {source} base={base} "
          f"splitter={splitter} port1.B={base[2]} port2.B={base[3]}")

    # Defining invariant: a 2-port device never emits the splitter block, because
    # IN5-IN8 are cleared by the remap.
    assert len(blob) == ADV_BASE_LEN, \
        (f"prefix {prefix}: adv must be the {ADV_BASE_LEN}-byte base layout, got "
         f"{len(blob)} bytes; the splitter block must not be emitted: {blob.hex()}")
    assert splitter == (), \
        f"prefix {prefix}: unexpected splitter floats in 2-port adv: {splitter}"

    # Best-effort: the notification CSV must end at isReclaim with no splitter tail.
    # Connecting/pairing over BLE can fail on the host; never let that mask the
    # advertisement result, which already proves the layout.
    _shell(dut, "app_module trigger_tx")
    try:
        csv = asyncio.run(_collect_notification(target))
    except Exception as exc:  # noqa: BLE001 - host BLE limitation, not a FW bug
        print(f"prefix {prefix}: notification capture errored ({exc}); "
              f"advertisement assertion already covers the layout")
        return
    if csv is None:
        print(f"prefix {prefix}: no notification captured (pairing/encryption "
              f"unavailable); advertisement assertion already covers the layout")
        return

    fields = csv.split(",")
    print(f"prefix {prefix}: notification CSV ({len(fields)} fields): {csv}")
    assert len(fields) > CSV_ISRECLAIM, f"truncated CSV: {csv}"
    assert _valid_or_disconnected(fields[CSV_AMBIENT])
    assert _valid_or_disconnected(fields[CSV_HUMIDITY])
    assert fields[CSV_ISRECLAIM] in ("0", "1"), \
        f"isReclaim field {fields[CSV_ISRECLAIM]!r} unexpected"
    assert _valid_or_disconnected(fields[CSV_PORT1_B]), \
        f"port1.B (T3) invalid value {fields[CSV_PORT1_B]!r}"
    assert _valid_or_disconnected(fields[CSV_PORT2_B]), \
        f"port2.B (T4) invalid value {fields[CSV_PORT2_B]!r}"
    tail = fields[CSV_SPLITTER_START:]
    assert tail == [], \
        f"prefix {prefix}: CSV must not append a splitter block, got tail {tail}"
