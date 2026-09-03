"""HIL tests for FW-1200: BLE subscription bookkeeping and notification framing.

Two defects in etc_ble.c:

* Unsubscribing from the SENSOR characteristic cleared the wrong subscription
  bit, so the periodic sensor notification kept firing into a characteristic
  nobody listened to (the host rejects it, the device logs
  ``Failed to notify current characteristic``).
* ``etc_ble_notify()`` builds every frame in one shared buffer and is called
  from three threads (BLE module, data module, Bluetooth RX via the CONFIG
  write handler) with an unbounded wait for the TX-complete. Concurrent callers
  could interleave the chunks of two messages.

Both are exercised over a real link with ``bleak``. Frames carry the 4-byte
``flex_ble_frame`` header (msg_id, frame_id, frame_len on frame 0 only); every
message the host receives must reassemble to exactly ``frame_len`` bytes with
contiguous frame ids.

Prerequisites: the DUT flashed with the standard (encryption-off) build plus
``overlay-hil.conf`` (no rtt.conf), and either in BLE mode
(``settings set_device 3``) or in any other mode, in which case the test injects
a magnet swipe to start advertising. Host side: BlueZ, ``bleak`` and
``dbus-fast`` (see test_ble_splitter.py for the Just Works pairing agent).

Run:

    pytest tests/hil/test_ble_ccc_reclaim.py -v -s
"""

import asyncio
import json
import re
import struct
import subprocess
import time

import pytest

from test_ble_splitter import SCAN_TIMEOUT_S, _AutoAcceptAgent, _ble_name

SENSOR_CHAR_UUID = "24eb85c1-1114-46fd-a9a3-1559361c6a95"
CONFIG_CHAR_UUID = "24eb85c3-1114-46fd-a9a3-1559361c6a95"

# Manufacturer data lengths as bleak reports them (the firmware emits no
# company id, so bleak eats the first two bytes as one): 26 - 2 without a
# splitter block, 42 - 2 with one. Used to recognise the DUT when BlueZ does
# not deliver the scan response that carries its name.
FLEX_MFG_LENS = (24, 40)

# The sensor notification is scheduled 5 s after the CCC subscribe.
SENSOR_WORK_DELAY_S = 5.0
# The request first walks the whole record store (tens of milliseconds per
# record with debug logging), then streams every un-acked record before the
# window (FW-954), so allow minutes on a well-used bench device.
RECLAIM_TIMEOUT_S = 720.0
# Recent window, so only a handful of records are inside it.
RECLAIM_WINDOW_S = 2 * 3600

NOTIFY_REJECTED = "Failed to notify current characteristic"


def _text(value):
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def _drain(dut, seconds):
    deadline = time.time() + seconds
    buf = ""
    while time.time() < deadline:
        try:
            buf += _text(dut.expect(r".+", timeout=0.5).group(0)) + "\n"
        except Exception:
            pass
    return buf


async def _scan(target_name, timeout):
    """Find the DUT by name, or by its advertisement layout when the name
    (carried in the scan response) never reaches the host."""
    from bleak import BleakScanner

    found = {}
    stop = asyncio.Event()

    def _cb(device, adv):
        name = adv.local_name or device.name or ""
        by_name = name == target_name if target_name else name.startswith("Flex_")
        by_layout = (not target_name and len(adv.manufacturer_data) == 1
                     and len(next(iter(adv.manufacturer_data.values()))) in FLEX_MFG_LENS)
        if (by_name or by_layout) and "dev" not in found:
            found["dev"], found["name"] = device, name or device.address
            stop.set()

    async with BleakScanner(_cb):
        try:
            await asyncio.wait_for(stop.wait(), timeout)
        except asyncio.TimeoutError:
            pass
    return found.get("dev"), None, found.get("name")


def _ensure_advertising(dut):
    dut.write(b"settings get_device\r\n")
    mode = int(dut.expect(r"Device mode (\d+)", timeout=10).group(1))
    if mode != 3:
        # A magnet swipe advertises for CONFIG_ETC_BLE_ADV_MAGNET_TIMEOUT_SEC.
        dut.write(b"magnet swipe\r\n")
        time.sleep(1.0)


class FrameCollector:
    """Reassembles flex_ble_frame chunks per characteristic and checks them."""

    def __init__(self):
        self.messages = {SENSOR_CHAR_UUID: [], CONFIG_CHAR_UUID: []}
        self._open = {}
        self.errors = []
        # Filled in by the session.
        self.sensor_after_toggle = None
        self.notify_rejected = None
        self.reclaim = []

    def on_notify(self, uuid):
        def _cb(_char, data):
            self._feed(uuid, bytes(data))
        return _cb

    def _feed(self, uuid, data):
        if len(data) < 4:
            self.errors.append(f"{uuid}: runt frame {data.hex()}")
            return
        msg_id, frame_id, frame_len = data[0], data[1], struct.unpack_from("<H", data, 2)[0]
        payload = data[4:]
        key = (uuid, msg_id)
        if frame_id == 0:
            if key in self._open:
                self.errors.append(f"{uuid}: message {msg_id} restarted mid-way")
            self._open[key] = {"expected": frame_len, "next": 1, "payload": bytearray(payload)}
        else:
            msg = self._open.get(key)
            if msg is None or msg["next"] != frame_id:
                self.errors.append(f"{uuid}: message {msg_id} frame {frame_id} out of order")
                return
            msg["next"] += 1
            msg["payload"] += payload
        msg = self._open[key]
        if len(msg["payload"]) >= msg["expected"]:
            if len(msg["payload"]) != msg["expected"]:
                self.errors.append(f"{uuid}: message {msg_id} length "
                                   f"{len(msg['payload'])} != header {msg['expected']}")
            self.messages[uuid].append(bytes(msg["payload"]))
            del self._open[key]

    def unfinished(self):
        return [f"{uuid}: message {msg_id} incomplete" for (uuid, msg_id) in self._open]


def _log_line_appears(dut, text, timeout):
    """True if `text` is logged within `timeout` seconds, anchored at now."""
    _drain(dut, 0.5)
    try:
        dut.expect(re.escape(text), timeout=timeout)
        return True
    except Exception:
        return False


def _reclaim_request():
    now = int(time.time())
    return json.dumps({"request": "reclaim", "start": now - RECLAIM_WINDOW_S,
                       "end": now + 60}).encode()


def _forget_flex_bonds():
    """Drop stale host bonds: after a reflash the DUT's keys no longer match
    what BlueZ remembers and pairing fails with AuthenticationFailed."""
    try:
        out = subprocess.run(["bluetoothctl", "devices"], capture_output=True,
                             text=True, timeout=10).stdout
    except Exception:  # noqa: BLE001 - bluetoothctl missing: nothing to clean
        return
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2].startswith("Flex_"):
            subprocess.run(["bluetoothctl", "remove", parts[1]], capture_output=True,
                           text=True, timeout=10)


def _reclaim_responses(frames):
    out = []
    for payload in frames.messages[CONFIG_CHAR_UUID]:
        try:
            msg = json.loads(payload.decode("ascii"))
        except (UnicodeDecodeError, ValueError):
            continue
        if msg.get("response") == "reclaim":
            out.append(msg)
    return out


async def _wait_for_reclaim_end(frames, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        responses = _reclaim_responses(frames)
        if responses and responses[-1].get("status") == 0:
            return responses
        await asyncio.sleep(0.5)
    return _reclaim_responses(frames)


async def _connect(target, attempts=3):
    """Scan and connect. With a rotating (private) address BlueZ may drop the
    device object between the end of the scan and the connect, so rescan and
    retry rather than fail on the first miss."""
    from bleak import BleakClient, BleakError

    last = None
    for _ in range(attempts):
        dev, _adv, _name = await _scan(target, SCAN_TIMEOUT_S)
        if dev is None:
            continue
        client = BleakClient(dev)
        try:
            await client.connect()
            return client
        except BleakError as exc:
            last = exc
            await asyncio.sleep(1.0)
    pytest.skip(f"DUT not found advertising or not connectable ({last})")


async def _subscribe(client, frames):
    await client.start_notify(CONFIG_CHAR_UUID, frames.on_notify(CONFIG_CHAR_UUID))
    await client.start_notify(SENSOR_CHAR_UUID, frames.on_notify(SENSOR_CHAR_UUID))


async def _session(target, dut):
    """Connect, pair, subscribe, toggle the SENSOR CCC, run one reclaim while
    live readings are triggered, return the FrameCollector."""
    frames = FrameCollector()
    _forget_flex_bonds()
    async with _AutoAcceptAgent():
        client = await _connect(target)
        for attempt in range(2):
            try:
                await client.pair()
            except Exception as exc:  # noqa: BLE001 - backend may auto-pair
                print(f"pair() not performed ({exc}); relying on on-demand pairing")
            try:
                await _subscribe(client, frames)
                break
            except Exception as exc:  # noqa: BLE001
                # After a reflash the device still holds the old bond for this
                # host and rejects the first pairing; it unpairs on that failure,
                # so a fresh connection succeeds.
                print(f"subscribe failed ({exc}); reconnecting once")
                await client.disconnect()
                if attempt:
                    pytest.skip(f"cannot subscribe without an encrypted link ({exc})")
                await asyncio.sleep(2.0)
                client = await _connect(target)

        # Unsubscribe SENSOR, then watch the device log across the 5 s sensor
        # work delay (plus slack) for a rejected notify attempt.
        await asyncio.sleep(1.0)
        await client.stop_notify(SENSOR_CHAR_UUID)
        frames.notify_rejected = await asyncio.to_thread(
            _log_line_appears, dut, NOTIFY_REJECTED, SENSOR_WORK_DELAY_S + 3.0)
        frames.sensor_after_toggle = len(frames.messages[SENSOR_CHAR_UUID])
        await client.start_notify(SENSOR_CHAR_UUID, frames.on_notify(SENSOR_CHAR_UUID))

        # Live readings from the shell around the request, so the data module
        # and the BT RX thread notify concurrently.
        dut.write(b"app_module trigger_tx\r\n")
        await client.write_gatt_char(CONFIG_CHAR_UUID, _reclaim_request(), response=True)
        await asyncio.sleep(0.5)
        dut.write(b"app_module trigger_tx\r\n")
        frames.reclaim = await _wait_for_reclaim_end(frames, RECLAIM_TIMEOUT_S)

        for uuid in (CONFIG_CHAR_UUID, SENSOR_CHAR_UUID):
            try:
                await client.stop_notify(uuid)
            except Exception:  # noqa: BLE001 - best effort on teardown
                pass
        await client.disconnect()
    return frames


def test_ccc_toggle_and_concurrent_notifications(dut, request):
    """Toggling the SENSOR CCC stops only sensor notifications, the reclaim
    still completes on CONFIG, and concurrently notified frames reassemble.

    One session covers both FW-1200 defects because every reclaim request
    first walks the whole record store, which takes minutes on a well-used
    bench device."""
    _ensure_advertising(dut)
    dut.write(b"app_module trigger_tx\r\n")
    _drain(dut, 3.0)

    frames = asyncio.run(_session(_ble_name(request, dut), dut))

    assert frames.notify_rejected is False, (
        "device tried to notify the unsubscribed sensor characteristic")
    assert frames.sensor_after_toggle == 0, (
        "sensor notification delivered after the host unsubscribed")
    assert frames.reclaim, "no reclaim response on the CONFIG characteristic"
    if frames.reclaim[0].get("status") == 0 and not frames.messages[SENSOR_CHAR_UUID]:
        pytest.skip("no records in the reclaim window; nothing was streamed")
    assert frames.reclaim[-1]["status"] == 0, (
        f"reclaim did not complete: {frames.reclaim}")
    assert not frames.errors and not frames.unfinished(), frames.errors + frames.unfinished()
    # Both notifiers must have taken part for the interleaving check to mean anything.
    assert frames.messages[SENSOR_CHAR_UUID], "no record streamed during the reclaim"
    assert len(frames.messages[CONFIG_CHAR_UUID]) >= 2, "expected the reclaim count and completion"
    for payload in frames.messages[SENSOR_CHAR_UUID]:
        text = payload.decode("ascii", "replace")
        assert text.count(",") >= 11, f"malformed sensor CSV: {text!r}"
    for payload in frames.messages[CONFIG_CHAR_UUID]:
        json.loads(payload.decode("ascii"))
