# Hardware-in-the-Loop (HIL) Tests

These tests run against a physical ETC Flex relay connected via USB CDC ACM
serial. They use
[pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/)
to communicate with the Zephyr shell over a serial port.

## Prerequisites

- An ETC Flex relay flashed with a **debug** build (includes shell commands)
- The board connected to your PC via USB
- Python 3.10+ with the project venv activated

## Setup

Install the test dependencies (from the repo root):

```bash
pip install -r tests/hil/requirements.txt
```

## Running the tests

### All tests

```bash
pytest tests/hil/ -v -s --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_<ID>-if00
```

### A single test file

```bash
pytest tests/hil/test_reclaim_reliability.py -v -s --port /dev/ttyACM0
```

### A single test

```bash
pytest tests/hil/test_reclaim_reliability.py::test_reclaim_request_survives_reboot -v -s --port /dev/ttyACM0
```

### Soak test

The soak test continuously re-triggers Bug 1 (request cleared by out-of-window
LoRa traffic) and Bug 2 (request lost on reboot) in a loop. Each iteration
takes ~2 min. Duration is controlled by `--soak-hours` (default 24):

```bash
pytest tests/hil/soak_reclaim_reliability.py -v -s --port /dev/ttyACM0 --soak-hours 1
```

> **Note:** The soak test requires a live LoRa logger (10000597) transmitting
> once per minute. It is not suitable for CI.

## Test files

| File | Description |
|---|---|
| `conftest.py` | Shared fixtures (`reboot` helper, `--soak-hours` option) |
| `pytest.ini` | Default pytest-embedded configuration |
| `test_reclaim_reliability.py` | 6 quick HIL tests for retained RAM persistence, satisfaction clearing, and stale eviction |
| `soak_reclaim_reliability.py` | Long-running soak test that validates Bug 1 and Bug 2 fixes over hours of real LoRa traffic |
| `coiote_client.py` | Minimal AVSystem Coiote DM v3 REST client (auth, configure execute+read task, task polling, cached read, delete) used by the cloud E2E test |
| `test_relay_command_coiote.py` | Cloud E2E: executes the EXACT Relay Command (48935/0/3) `RECLAIM:*` subcommands via Coiote and asserts the reply read back from the Response resource (48935/0/4) |
| `test_magnet_swipe_settling.py` | FW-492: repeated magnet swipes must not re-power the analog rail while it is settling. Needs the `overlay-hil.conf` overlay (adds `magnet swipe` / `magnet status` shell commands) |
| `test_functional_test_no_battery_coiote.py` | FW-169: operator-assisted cloud check that the functional test reports Result (48937/0/1) `7` (battery not connected) with the battery removed. Run the functional test on the jig first; pass `--functional-test-expect 0` for the battery-installed positive case |
| `test_calibration.py` | FW-588/611/612/613/614: operator-attended calibration suite (LED vs result, portal 48939/48950 cross-checks, reboot persistence, sensor/calibration concurrency). Needs `overlay-hil.conf`; Coiote creds optional. Tests share their calibrations, so run the file in order and follow the five setup prompts |
| `test_splitter_detection.py` | FW-1071: operator-attended check that a port with no splitter reports no B branch, including after a splitter is removed without a reboot. Needs `overlay-hil.conf` (uses `magnet sample`); run the file in order and follow the three wiring prompts |
| `test_modem_power_state.py` | FW-1152: modem power state unification — power off/on idempotency, the bounded power-down window, and exactly one `Modem power ON -> OFF` per power off. Needs `overlay-hil.conf` (adds `modem_module power` / `modem_module state`). The AT+QPOWD-unavailable PWRKEY fallback is GDB-verified only; see the module docstring |
| `test_splitter_family.py` | FW-808: operator-attended check that one image detects and drives both a TMP1827 splitter (family `0x27`) and a TMP1826 one (`0x26`), branch switching included. Needs `overlay-hil.conf` and a TMP1827 splitter board (HW2-808); run the file in order and follow the three wiring prompts |
| `test_ble_boot_swipe.py` | FW-1194: a magnet swipe injected right after a cold boot, while the BLE stack is still enabling, must be latched and replayed instead of resetting the device. Needs `overlay-hil.conf` (uses `magnet swipe`); prints whether the swipe landed before or after `Bluetooth initialized` |
| `test_ble_adv_watchdog.py` | FW-1203: an advertising set stopped behind the app's back (`ble adv_kill`, as a connection attempt that fails to establish does) must be restarted by the advertising watchdog within one period, and a healthy advertiser must be left alone. Needs `overlay-hil.conf`; the on-air check uses `bleak` when installed |
| `test_rh_probe.py` | FW-1195: operator-attended check that an RH probe is read on port 1, after a same-port reconnect, on port 2, and again on port 1 after a cold boot; the last test cross-checks 48936/0 on Coiote when creds are given. Needs `overlay-hil.conf` (uses `magnet sample`) and one RH probe; run the file in order and follow the wiring prompts |
| `test_ble_ccc_reclaim.py` | FW-1200: one BLE session that unsubscribes the SENSOR characteristic (only sensor notifications may stop), runs a reclaim to completion on CONFIG while live readings are triggered, and checks every notified frame reassembles without interleaving. Finds the DUT by its advertisement layout (BlueZ drops the scan response carrying the name), forgets stale host bonds, and needs `overlay-hil.conf`, BlueZ + `bleak` + `dbus-fast`; allow ~6 min because the request first walks the whole record store |
| `test_threshold_coiote.py` | FW-1178: cloud checks for the EXACT Threshold object (48944): writing a slot's four configuration resources (Value Type / Alert Type / Threshold Value / Enabled) reads back unchanged for an integral, a fractional and a float32-rounded Threshold Value, RIDs 1-4 survive a cold reboot while Alert (RID 5) and Trigger Count (RID 7) reset at boot, and an out-of-range Value Type / Alert Type write is rejected and leaves the stored configuration untouched. Needs Coiote creds and the 48944 DDF uploaded to the tenant; queue-mode latency means ~20 min for the file |
| `test_threshold_trigger_coiote.py` | FW-1179: cloud checks that a reading crossing an immediate report threshold transmits ahead of schedule. Two slots are held at fixed values (Exceeds 25 °C, Drops Below 40 %RH) and the readings are driven across them with `sensor_sim` (FW-1234), so no probe is needed: a baseline reading stays quiet; a 30 °C reading crosses, dispatches an upload and reaches Coiote with `/48944/0/5` and `/48933/0/7` true while a device Read shows the alert cleared; a second crossing inside the 60 s hold-off (FW-1225, `/48931/0/16`) is flagged only and rides the next ordinary report; one past it transmits again; humidity falling to 30 %RH crosses the Drops Below slot; a reading that stays beyond does not fire again and rewriting the Threshold Value re-arms it; an unplugged probe (`nc`) re-arms the port without the reading returning below the threshold; a crossing on an RTC log wake defers its dispatch by the per-device transmit delay; and no later report carries Priority again. Needs `overlay-hil.conf`, Coiote creds and the 48944 DDF; run the file in order — budget ~25 min |
| `acquisition.py` | Shared helpers for the splitter and RH-probe suites: triggers one acquisition with `magnet sample` (anchored on the command echo) and parses the `detect: splitter`, `detect: parts`, `ADC[n]` and `digital: humid` log lines |

## Cloud E2E test (Coiote)

`test_relay_command_coiote.py` drives the production cloud path end to end. It
needs the device **LTE/cloud-connected to Coiote** plus Coiote credentials, in
addition to the serial `--port` (used for one independent on-device cross-check).

Provide credentials with `--coiote-config` (a JSON file with `username`,
`password`, and optionally `api_host`) or `$COIOTE_CONFIG`; if neither is given
and the sibling `etc-tools/coiote_api/config.json` is absent, the test **skips**.

```bash
pytest tests/hil/test_relay_command_coiote.py -v -s \
  --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
  --coiote-config /path/to/etc-tools/coiote_api/config.json
```

Options: `--coiote-device` (default `urn:dev:mac:0A978428BAEE9D23`),
`--coiote-op-timeout` (per-operation poll seconds, default 240 — queue mode means
minutes of latency). The test deletes every task it creates.

> **Addressing note:** the test addresses the object by named data-model keys
> (`EXACT Relay.0.Command` / `EXACT Relay.0.Response`, derived from the DDF). If
> your Coiote tenant expects raw LwM2M paths instead, change `COMMAND_KEY` /
> `RESPONSE_KEY` at the top of the test to `/48935/0/3` / `/48935/0/4`.

## Threshold object test (Coiote)

`test_threshold_coiote.py` (FW-1178) exercises the EXACT Threshold object
(48944) over the same cloud path. It needs the device **LTE/cloud-connected**,
Coiote credentials, and `--port` (the persistence test cold-reboots the DUT over
the serial console). Flash a debug build **without** `rtt.conf`: `dut.reboot()`
waits for the `Load settings successfully` console line, which an rtt build
never prints to the serial console.

```bash
pytest tests/hil/test_threshold_coiote.py -v -s \
  --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
  --coiote-config /path/to/etc-tools/coiote_api/config.json
```

Tests: configuration write + read-back of slots 0 and 1 — slot 0 with an
integral Threshold Value (10.0, which Coiote sends as a CBOR integer) and slot 1
with a fractional one (20.25), followed by a re-write of 21.1 to check the
float32 rounding — persistence of RIDs 1-4 across `dut.reboot()` together with
Alert (RID 5) = false and Trigger Count (RID 7) = 0 on the freshly booted
device, and rejection of out-of-range Value Type / Alert Type writes with the
stored configuration left unchanged. The test leaves slots 0 and 1 disabled on
the way out (best effort).

> **Addressing note:** the object is addressed by named data-model keys
> (`EXACT Threshold.0.Value Type`, ... , derived from the DDF), so the 48944 DDF
> must be uploaded to the Coiote tenant data model. Without the DDF, fall back to
> raw keys by setting `OBJECT = "48944"` and the resource names to their RIDs,
> giving the **dot-separated** form `48944.<slot>.<rid>`, e.g. `48944.0.2` for
> Value Type. The slashed spellings (`/48944/0/2`, `48944/0/2`) are rejected by
> the tenant dialect and do not work.

## Threshold trigger test (Coiote)

`test_threshold_trigger_coiote.py` (FW-1179) is the trigger half of the same
object. It keeps two slots at fixed values — slot 0 Exceeds 25.0 °C on
temperature, slot 1 Drops Below 40.0 %RH on humidity — and drives the
**readings** across them with `sensor_sim` (FW-1234), so no probe, thermal
stimulus or water bath is needed. It needs the device **LTE/cloud-connected**,
Coiote credentials and `--port`.

```bash
pytest tests/hil/test_threshold_trigger_coiote.py -v -s \
  --port /dev/serial/by-id/usb-ZEPHYR_USB-DEV_0A978428BAEE9D23-if00 \
  --coiote-config /path/to/etc-tools/coiote_api/config.json
```

**The order of the tests is load-bearing**: the first arms both slots and leaves
them enabled for the rest, and the last compares the Priority flag against the
one the Exceeds test recorded. The hold-off is shortened to 60 s from Coiote
(FW-1225, `EXACT Configuration.0.THRESHOLD_REPORT_INTERVAL`, i.e. `/48931/0/16`) for
the whole file and restored to 900 s on the way
out, along with disabling both slots — by a fixture after the last test, or as a
queued Coiote task if the run stops early. The DUT is never rebooted (FW-1221,
and the trigger state is RAM-only). Budget ~25 min.

Three device log lines carry the assertions, so build **without** `rtt.conf` and
with debug logs on: `Sensor sim active mask 0x<mask>` (the injected readings
reached the sample), `Threshold crossed: slots 0x<mask> upload <0|1>`, and
`Threshold upload deferred by <n> ms`.

### `sensor_sim`: driving readings from the shell

`CONFIG_ETC_SENSOR_SIM_SHELL` (on in `overlay-hil.conf`, off everywhere else)
adds a `sensor_sim` command group. A simulated value replaces the physical one
in the sample after the probes are read and before the sensor event is
submitted, so flash records, threshold evaluation, LwM2M, LoRa, BLE and the
functional test all treat it as a real reading.

| Command | Effect |
| --- | --- |
| `sensor_sim set <in1..in8\|ambient\|humid> <milli-units>` | Drive one input. Integer milli-degrees C or milli-%RH, e.g. `sensor_sim set in1 30000` for 30.0 °C |
| `sensor_sim set <input> nc` | Drive one input to its "no probe" sentinel, so the reading is invalid |
| `sensor_sim clear [input]` | Stop simulating one input, or all of them with no argument |
| `sensor_sim status` | The active mask and one line per simulated input, in milli-units |

Setting a value does not take a sample: the next `magnet sample` or scheduled
acquisition picks it up. The state is RAM only and never written to NVS, so a
reboot clears it.

The probe-connected LED state is derived inside `etc_sensor_run_acquisition()`,
upstream of the substitution, so a port with no probe still shows as
disconnected on the UI even while it reports a simulated reading.
