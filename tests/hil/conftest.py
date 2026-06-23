import json
import os
import time

import pytest


def pytest_addoption(parser):
    parser.addoption("--soak-hours", type=float, default=24.0,
                     help="How long to run the soak test (hours, default 24)")
    parser.addoption("--e2e-timeout", type=int, default=30,
                     help="Max wait time in minutes for e2e reclaim test (default 30)")
    parser.addoption("--coiote-config", type=str, default=None,
                     help="Path to a JSON file with Coiote username/password/api_host. "
                          "Defaults to $COIOTE_CONFIG, else the sibling "
                          "etc-tools/coiote_api/config.json if present.")
    parser.addoption("--coiote-device", type=str,
                     default="urn:dev:mac:0A978428BAEE9D23",
                     help="Coiote device endpoint id for the relay-command E2E test")
    parser.addoption("--coiote-op-timeout", type=int, default=240,
                     help="Per-operation Coiote task poll timeout in seconds (default 240)")
    parser.addoption("--logger-port", type=str, default=None,
                     help="Serial port of the LoRa logger console, for the two-device "
                          "end-to-end reclaim test (relay is the primary --port DUT).")
    parser.addoption("--logger-id", type=str, default=None,
                     help="LoRa logger device id used in the end-to-end reclaim test.")


# Default config path: the etc-tools coiote_api repo sitting next to this
# workspace (monitor2.0/etc-firmware/tests/hil -> .../exact/etc-tools/...).
_DEFAULT_COIOTE_CONFIG = os.path.abspath(
    os.path.join(os.path.dirname(__file__),
                 "../../../../etc-tools/coiote_api/config.json"))


@pytest.fixture(autouse=True)
def add_dut_methods(dut):
    def reboot():
        """Reboot the device and wait for it to come back."""
        dut.write(b"kernel reboot\r\n")
        time.sleep(5)
        dut.write(b"\r\n")
        dut.expect(r"uart:~\$", timeout=20)

    setattr(dut, "reboot", reboot)


@pytest.fixture
def coiote(request):
    """A ready, authenticated CoioteClient, or skip if no creds are available."""
    from coiote_client import CoioteClient

    path = (request.config.getoption("--coiote-config")
            or os.environ.get("COIOTE_CONFIG")
            or _DEFAULT_COIOTE_CONFIG)
    if not path or not os.path.isfile(path):
        pytest.skip("No Coiote config (use --coiote-config or $COIOTE_CONFIG)")

    with open(path) as f:
        cfg = json.load(f)
    if not cfg.get("username") or not cfg.get("password"):
        pytest.skip(f"Coiote config {path} missing username/password")

    client = CoioteClient(
        api_host=cfg.get("api_host", "https://us.iot.avsystem.cloud:8087/api"),
        username=cfg["username"],
        password=cfg["password"],
    )
    client.authenticate()
    return client
