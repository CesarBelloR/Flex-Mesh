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
| `acquisition.py` | Shared helpers for the two splitter suites: triggers one acquisition with `magnet sample` and parses the `detect: splitter`, `detect: parts` and `ADC[n]` log lines |

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
