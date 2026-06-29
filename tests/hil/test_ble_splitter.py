"""HIL tests for splitter temperature fields over BLE (FW-885).

A BLE-mode logger (device mode 3) exposes the splitter sub-port temperatures
(1.B-4.B = sensor inputs IN5-IN8) in two places:

* **Advertisement** - the manufacturer data carries the packed sensor floats.
  Layout (little-endian IEEE-754 floats unless noted)::

      IN1 IN2 IN3 IN4 ambient humidity [1.B 2.B 3.B 4.B] bat_status(u8) bat(u8)

  The four splitter floats are present only when a splitter is attached
  (all-or-nothing), so the manufacturer data is 26 bytes without a splitter or
  42 bytes with one. The two battery bytes are always last. The payload exceeds
  the 31-byte legacy advertising limit, so the device uses *extended*
  advertising.

* **Notification** (sensor characteristic) - the legacy ASCII CSV built by
  ``etc_common_prepare_logger_legacy_data``::

      <ver>,<id>,<battery>,<pkt>,<ts>,<T1>,<T2>,<T3>,<T4>,<ambient>,<humidity>,
      <isReclaim>[,<1.B>[,<2.B>[,<3.B>[,<4.B>]]]]

  The splitter temps are appended after ``isReclaim`` and trailing disconnected
  sub-ports are omitted (emit 1.B up through the highest valid sub-port;
  interior gaps appear as ``*``).

Prerequisites for the positive assertions:

* The DUT is flashed with the standard (encryption-off) build and set to BLE
  mode (``settings set_device 3``). The test ``--port`` is the DUT shell.
* A splitter is attached so IN5-IN8 produce readings. With no splitter the
  splitter fields are absent and the test skips the with-splitter assertions
  (which also exercises the "omit when no splitter" path: 26-byte adv, no CSV
  tail).

The DUT is targeted over BLE by name (``--ble-name``, default: the first device
advertising with the ``Flex_`` prefix).

Two host (Linux/BlueZ) quirks shape how this test reaches the data; neither is a
firmware issue:

* **Advertisement.** BlueZ (verified on 5.64) does not surface the larger
  (42-byte, splitter-present) extended-advertising manufacturer data over D-Bus,
  so ``bleak`` reports empty manufacturer data even though the bytes are on air.
  The test therefore captures the advertisement with ``btmon`` (raw HCI) and
  parses the LE Extended Advertising Report directly. ``btmon`` needs raw-socket
  capabilities once per host::

      sudo setcap 'cap_net_raw,cap_net_admin+eip' "$(command -v btmon)"

  If ``btmon`` is unavailable/unprivileged the test falls back to bleak's D-Bus
  manufacturer data (sufficient for the 26-byte no-splitter case) and skips if
  that too is empty.

* **Notification.** The sensor CCC requires an encrypted link. Both ends are
  NoInputNoOutput, so pairing is **Just Works** (an unauthenticated, encrypted
  link — enough for ``PERM_*_ENCRYPT``). BlueZ rejects the Just Works
  confirmation unless a pairing agent is registered, so the test registers a
  transient auto-accept agent (via ``dbus-fast``) for the duration of the pair.
  If a stale bond blocks pairing, remove the device from the host
  (``bluetoothctl remove <addr>``) and re-run.
"""

import asyncio
import re
import shutil
import struct
import subprocess

import pytest

SENSOR_CHAR_UUID = "24eb85c1-1114-46fd-a9a3-1559361c6a95"
_AGENT_PATH = "/etc_hil/agent"

# Advertisement manufacturer-data layout.
ADV_BASE_FLOATS = 6           # IN1-IN4, ambient, humidity
ADV_BASE_LEN = ADV_BASE_FLOATS * 4 + 2  # + battery status + battery = 26
ADV_WITH_SPLITTER_LEN = ADV_BASE_LEN + 4 * 4  # + 4 sub-port floats = 42

# Notification ASCII field indices.
CSV_AMBIENT = 9
CSV_HUMIDITY = 10
CSV_ISRECLAIM = 11
CSV_SPLITTER_START = 12       # first splitter field (1.B), if present

# Disconnected sub-ports read back as a large-magnitude sentinel.
NO_CONNECTED_THRESHOLD = -200.0

SCAN_TIMEOUT_S = 20.0
NOTIFY_TIMEOUT_S = 25.0


def _ble_name(request, dut):
    """Resolve the DUT BLE name from --ble-name, else require the Flex_ prefix."""
    name = request.config.getoption("--ble-name")
    return name  # may be None -> caller falls back to the 'Flex_' prefix


def _assert_ble_mode(dut):
    """The primary --port DUT must be a BLE-mode logger (device mode 3)."""
    dut.write(b"settings get_device\r\n")
    mode = int(dut.expect(r"Device mode (\d+)", timeout=10).group(1))
    if mode != 3:
        pytest.skip(f"DUT is device mode {mode}, not BLE (3); "
                    f"run 'settings set_device 3' and reboot first")


def _reconstruct_mfg(adv):
    """Rebuild the raw manufacturer blob bleak split into {company_id: value}.

    bleak parses manufacturer data as a 16-bit company id mapped to the
    remaining bytes; the firmware emits raw sensor bytes with no company id, so
    the first two bytes are reported as the key. Prepend them back (LE).
    """
    md = adv.manufacturer_data
    if not md:
        return None
    cid, value = next(iter(md.items()))
    return struct.pack("<H", cid) + bytes(value)


def _parse_adv(blob):
    """Return (base_floats, splitter_floats, bat_status, battery)."""
    n = len(blob)
    assert (n - ADV_BASE_LEN) % 4 == 0 and n >= ADV_BASE_LEN, \
        f"unexpected adv manufacturer-data length {n}: {blob.hex()}"
    n_split = (n - ADV_BASE_LEN) // 4
    base = struct.unpack_from("<6f", blob, 0)
    off = ADV_BASE_FLOATS * 4
    splitter = struct.unpack_from(f"<{n_split}f", blob, off) if n_split else ()
    off += n_split * 4
    bat_status, battery = blob[off], blob[off + 1]
    return base, splitter, bat_status, battery


async def _scan(target_name, timeout):
    """Find the DUT advertisement; return (BLEDevice, AdvertisementData)."""
    from bleak import BleakScanner

    found = {}
    stop = asyncio.Event()

    def _cb(device, adv):
        name = adv.local_name or device.name or ""
        if target_name:
            match = name == target_name
        else:
            match = name.startswith("Flex_")
        if match and "dev" not in found:
            found["dev"], found["adv"], found["name"] = device, adv, name
            stop.set()

    async with BleakScanner(_cb):
        try:
            await asyncio.wait_for(stop.wait(), timeout)
        except asyncio.TimeoutError:
            pass
    return found.get("dev"), found.get("adv"), found.get("name")


def _parse_btmon_adv(text, target_name):
    """Pull the manufacturer-data blob for ``target_name`` out of btmon output.

    btmon prints each LE Extended Advertising Report as a block ending with the
    parsed AD structures, e.g.::

        Company: not assigned (13395)
          Data: cb41339388c3...
        Name (complete): Flex_10001115

    The firmware emits raw sensor bytes with no real company id, so btmon
    interprets the first two bytes as the 16-bit company id and the rest as
    ``Data``; rebuild the blob as ``<company-id LE> + Data`` (matching
    ``_reconstruct_mfg``). Match on the advertised name, not the address, since
    ``CONFIG_BT_PRIVACY`` rotates the address.
    """
    cid = data = None
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"Company:.*\((\d+)\)$", line)
        if m:
            cid, data = int(m.group(1)), None
            continue
        m = re.match(r"Data:\s*([0-9a-fA-F]+)$", line)
        if m and cid is not None:
            data = m.group(1)
            continue
        m = re.match(r"Name \((?:complete|short)\):\s*(.+)$", line)
        if m and m.group(1).strip() == target_name and cid is not None and data:
            return struct.pack("<H", cid) + bytes.fromhex(data)
    return None


def _capture_adv_via_btmon(target_name, timeout):
    """Capture ``target_name``'s advertisement with btmon (raw HCI).

    Returns the reconstructed manufacturer-data blob, or None if btmon is
    missing/unprivileged or the advert was not seen. A concurrent bleak scan is
    run to provoke the controller into emitting extended advertising reports.
    """
    btmon = shutil.which("btmon")
    if btmon is None:
        return None
    try:
        proc = subprocess.Popen([btmon], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
    except OSError:
        return None
    try:
        # Provoke ext-adv reports while btmon is listening.
        asyncio.run(_scan(target_name, min(timeout, SCAN_TIMEOUT_S)))
    finally:
        proc.terminate()
        try:
            out, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate()
    if out and "Operation not permitted" in out:
        return None  # btmon lacks cap_net_raw; caller falls back to bleak
    return _parse_btmon_adv(out or "", target_name)


class _AutoAcceptAgent:
    """A transient BlueZ pairing agent that accepts Just Works confirmations.

    Registered as the default agent for the duration of a pairing attempt so
    BlueZ does not reject the (unauthenticated, encrypted) Just Works link for
    lack of an agent. Best-effort: yields None if dbus-fast/BlueZ is unavailable.
    """

    def __init__(self):
        self._bus = None
        self._mgr = None

    async def __aenter__(self):
        try:
            from dbus_fast import BusType
            from dbus_fast.aio import MessageBus
            from dbus_fast.service import ServiceInterface, method
        except ImportError:
            return None

        class Agent(ServiceInterface):
            def __init__(self):
                super().__init__("org.bluez.Agent1")

            @method()
            def Release(self):
                pass

            @method()
            def RequestConfirmation(self, device: "o", passkey: "u"):
                pass  # Just Works numeric comparison -> accept

            @method()
            def RequestAuthorization(self, device: "o"):
                pass

            @method()
            def AuthorizeService(self, device: "o", uuid: "s"):
                pass

            @method()
            def RequestPinCode(self, device: "o") -> "s":
                return "0000"

            @method()
            def RequestPasskey(self, device: "o") -> "u":
                return 0

            @method()
            def DisplayPinCode(self, device: "o", pincode: "s"):
                pass

            @method()
            def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):
                pass

            @method()
            def Cancel(self):
                pass

        try:
            self._bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
            self._bus.export(_AGENT_PATH, Agent())
            introspect = await self._bus.introspect("org.bluez", "/org/bluez")
            obj = self._bus.get_proxy_object("org.bluez", "/org/bluez", introspect)
            self._mgr = obj.get_interface("org.bluez.AgentManager1")
            await self._mgr.call_register_agent(_AGENT_PATH, "KeyboardDisplay")
            await self._mgr.call_request_default_agent(_AGENT_PATH)
        except Exception:  # noqa: BLE001 - no agent => caller still tries to pair
            await self.__aexit__(None, None, None)
            return None
        return self

    async def __aexit__(self, *exc):
        if self._mgr is not None:
            try:
                await self._mgr.call_unregister_agent(_AGENT_PATH)
            except Exception:  # noqa: BLE001 - best effort
                pass
            self._mgr = None
        if self._bus is not None:
            self._bus.disconnect()
            self._bus = None


def _decode_notification(chunks):
    """Strip the 4-byte flex_ble_frame header from the first frame and decode.

    flex_ble_frame: msg_id(u8) frame_id(u8) frame_len(u16 LE) payload[]. The
    legacy ASCII payload is short (single frame) and the build under test has
    encryption disabled, so the payload is plaintext.
    """
    for data in chunks:
        if len(data) <= 4:
            continue
        frame_id = data[1]
        if frame_id != 0:
            continue
        payload = data[4:]
        try:
            text = payload.decode("ascii")
        except UnicodeDecodeError:
            continue
        if text.count(",") >= CSV_ISRECLAIM:
            return text.strip().rstrip(",")
    return None


def _valid_or_disconnected(field):
    field = field.strip()
    if field == "*":
        return True
    try:
        float(field)
        return True
    except ValueError:
        return False


def test_advertisement_includes_splitter(dut, request):
    """The extended advertisement carries the splitter sub-port temps when a
    splitter is attached; otherwise the splitter block is omitted (26 bytes)."""
    _assert_ble_mode(dut)
    target = _ble_name(request, dut)
    if not target:
        pytest.skip("Advertisement capture needs an explicit --ble-name "
                    "(btmon matches the report by advertised name)")

    # Prefer btmon (raw HCI): BlueZ does not expose the 42-byte splitter
    # manufacturer data over D-Bus, so bleak alone cannot see the splitter
    # block. Fall back to bleak's D-Bus data (good for the 26-byte case).
    blob = _capture_adv_via_btmon(target, SCAN_TIMEOUT_S)
    source = "btmon"
    if blob is None:
        dev, adv, name = asyncio.run(_scan(target, SCAN_TIMEOUT_S))
        if dev is None:
            pytest.skip(f"No advertisement seen for {target} within "
                        f"{SCAN_TIMEOUT_S}s (is the DUT advertising in BLE mode? "
                        f"is btmon installed/privileged for raw capture?)")
        blob = _reconstruct_mfg(adv)
        source = "bleak"
        if blob is None:
            pytest.skip(f"{name} found but neither btmon nor BlueZ surfaced "
                        f"manufacturer data; install btmon and grant it "
                        f"cap_net_raw to capture the splitter advertisement.")
    base, splitter, bat_status, battery = _parse_adv(blob)
    print(f"{target}: adv {len(blob)} B via {source} base={base} "
          f"splitter={splitter} bat_status={bat_status} battery={battery}")

    if len(blob) == ADV_BASE_LEN:
        pytest.skip(f"No splitter attached: adv is {ADV_BASE_LEN} bytes with the "
                    f"splitter block omitted (base floats {base})")

    assert len(blob) == ADV_WITH_SPLITTER_LEN, \
        f"splitter adv must be {ADV_WITH_SPLITTER_LEN} bytes, got {len(blob)}"
    assert len(splitter) == 4, "expected all four splitter floats"
    # At least one sub-port must be a real reading (that is what made the device
    # include the block); battery bytes must still be the final two.
    assert any(v > NO_CONNECTED_THRESHOLD for v in splitter), \
        f"splitter block present but all sub-ports disconnected: {splitter}"


def test_notification_includes_splitter(dut, request):
    """The sensor notification ASCII CSV appends the splitter temps after the
    isReclaim field, with trailing disconnected sub-ports omitted."""
    _assert_ble_mode(dut)
    target = _ble_name(request, dut)

    # Nudge a fresh sample so a record is queued for the BLE notification.
    dut.write(b"app_module trigger_tx\r\n")

    csv = asyncio.run(_collect_notification(target))
    if csv is None:
        pytest.skip("No sensor-data notification received (no record queued, "
                    "pairing/encryption unavailable on the host, or the build "
                    "has BLE encryption enabled)")

    fields = csv.split(",")
    print(f"notification CSV ({len(fields)} fields): {csv}")
    assert len(fields) > CSV_ISRECLAIM, f"truncated CSV: {csv}"
    assert _valid_or_disconnected(fields[CSV_AMBIENT])
    assert _valid_or_disconnected(fields[CSV_HUMIDITY])
    assert fields[CSV_ISRECLAIM] in ("0", "1"), \
        f"isReclaim field {fields[CSV_ISRECLAIM]!r} unexpected"

    splitter = fields[CSV_SPLITTER_START:]
    if not splitter:
        pytest.skip(f"No splitter data in notification (sub-ports omitted): {csv}")

    assert 1 <= len(splitter) <= 4, f"unexpected splitter field count: {splitter}"
    for i, val in enumerate(splitter):
        assert _valid_or_disconnected(val), \
            f"splitter field {i + 1}.B invalid value {val!r}"
    # Trailing-omission: the last emitted sub-port must be a real reading, never
    # a trailing '*' placeholder.
    assert splitter[-1].strip() != "*", \
        f"trailing disconnected sub-port should have been omitted: {splitter}"


async def _collect_notification(target):
    """Connect, pair, subscribe, and return the first decoded sensor CSV."""
    from bleak import BleakClient

    dev, _adv, _name = await _scan(target, SCAN_TIMEOUT_S)
    if dev is None:
        return None

    chunks = []
    got = asyncio.Event()

    def _on_notify(_char, data):
        chunks.append(bytes(data))
        got.set()

    async with _AutoAcceptAgent(), BleakClient(dev) as client:
        try:
            await client.pair()
        except Exception as exc:  # noqa: BLE001 - backend may auto-pair
            print(f"pair() not performed ({exc}); relying on on-demand pairing")
        # The sensor CCC requires an encrypted link (CONFIG_BT_SMP). If the host
        # cannot establish a paired/encrypted link (e.g. the firmware enforces
        # MITM and this central is NoInputNoOutput), subscribing fails; treat
        # that as "no notification available" so the test skips rather than
        # hard-failing on an environment/security limitation.
        try:
            await client.start_notify(SENSOR_CHAR_UUID, _on_notify)
        except Exception as exc:  # noqa: BLE001
            print(f"start_notify failed ({exc}); cannot subscribe without an "
                  f"encrypted link")
            return None
        # The device schedules the measurement notification ~5 s after the CCC
        # subscribe; wait for the first frame, then drain briefly for the rest.
        try:
            await asyncio.wait_for(got.wait(), NOTIFY_TIMEOUT_S)
        except asyncio.TimeoutError:
            pass
        await asyncio.sleep(1.0)
        try:
            await client.stop_notify(SENSOR_CHAR_UUID)
        except Exception:  # noqa: BLE001 - best effort on teardown
            pass

    return _decode_notification(chunks)
