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
