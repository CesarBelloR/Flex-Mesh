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
    parser.addoption("--functional-test-expect", type=int, default=7,
                     help="Expected functional-test Result code (48937/0/1) for the "
                          "no-battery HIL test (default 7 = battery not connected; "
                          "use 0 for the battery-installed positive case)")
    parser.addoption("--ble-name", type=str, default=None,
                     help="BLE advertised name of the DUT (e.g. 'Flex_10000199') for the "
                          "BLE splitter test. Defaults to the first device advertising with "
                          "the 'Flex_' prefix.")


# Default config path: the etc-tools coiote_api repo sitting next to this
# workspace (monitor2.0/etc-firmware/tests/hil -> .../exact/etc-tools/...).
_DEFAULT_COIOTE_CONFIG = os.path.abspath(
    os.path.join(os.path.dirname(__file__),
                 "../../../../etc-tools/coiote_api/config.json"))


# The device is only FULLY booted once etc_settings has loaded its NVS-backed
# settings (main.c runs etc_settings_init() before starting the application
# threads). Waiting on the shell prompt alone races ahead of this, so a test that
# reboots and immediately drives the device can act before it is ready.
_BOOTED_SENTINEL = "Load settings successfully"

# Time to let the CDC-ACM console settle after it re-enumerates. Reopening while
# enumeration is still in flight leaves a reader that never delivers anything.
_USB_SETTLE_S = 3.0


def _wait_serial_port(port, present, timeout, poll=0.05):
    """Block until os.path.exists(port) == present (or timeout). Returns success."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(port) == present:
            return True
        time.sleep(poll)
    return False


@pytest.fixture(autouse=True)
def add_dut_methods(dut):
    import serial as pyserial

    def reboot():
        """Cold-reboot the device and block until it is FULLY booted.

        A cold reboot re-enumerates the USB CDC-ACM console, so the open serial fd
        goes stale and further reads/writes raise [Errno 5]. Reopen the port once
        it re-enumerates, then wait for the settings/NVS load marker rather than
        the shell prompt (the prompt appears before etc_settings has loaded)."""
        ser = dut.serial
        port = ser.port

        # The write can race the USB drop; a failure here just means the reset
        # already took the console down.
        try:
            ser.proc.write(b"kernel reboot cold\r\n")
        except Exception:
            pass

        # Stop reading the stale fd, then follow the console down and back up on
        # the same (by-id) path.
        ser.stop_redirect_thread()
        try:
            ser.proc.close()
        except Exception:
            pass
        _wait_serial_port(port, present=False, timeout=12)
        if not _wait_serial_port(port, present=True, timeout=45, poll=0.1):
            raise RuntimeError(f"serial port {port} did not re-enumerate after reboot")
        time.sleep(1.5)  # let the tty settle before reopening

        # Reopen on the same port and resume the redirect reader.
        port_config = dict(ser.DEFAULT_PORT_CONFIG)
        port_config["baudrate"] = ser.baud
        last_err = None
        for _ in range(20):
            try:
                ser.proc = pyserial.serial_for_url(port, **port_config)
                break
            except Exception as e:  # transient failure while the tty enumerates
                last_err = e
                time.sleep(0.5)
        else:
            raise RuntimeError(f"could not reopen {port} after reboot: {last_err}")
        ser.start_redirect_thread()

        # Fully booted only once settings/NVS have loaded.
        dut.expect(_BOOTED_SENTINEL, timeout=45)

    def _reopen_console():
        """Drop and re-establish the serial reader on the same by-id path."""
        ser = dut.serial
        port = ser.port

        for stop in (ser.stop_redirect_thread, ser.proc.close):
            try:
                stop()
            except Exception:
                pass

        if not _wait_serial_port(port, present=True, timeout=45, poll=0.1):
            raise RuntimeError(f"serial port {port} did not re-enumerate")
        time.sleep(_USB_SETTLE_S)

        port_config = dict(ser.DEFAULT_PORT_CONFIG)
        port_config["baudrate"] = ser.baud
        last_err = None
        for _ in range(20):
            try:
                ser.proc = pyserial.serial_for_url(port, **port_config)
                break
            except Exception as e:  # transient while the tty enumerates
                last_err = e
                time.sleep(0.5)
        else:
            raise RuntimeError(f"could not reopen {port}: {last_err}")
        ser.start_redirect_thread()

    def cold_boot(attempts=3):
        """Cold-boot and return with a console reader that is known to be alive.

        `reboot()` reopens the port as soon as it re-enumerates, and the reader
        does not always survive that — leaving a live device looking silent,
        which surfaces as a spurious firmware failure. This waits longer for USB
        to settle and retries the whole cycle until the boot marker is seen.
        """
        for _ in range(attempts):
            # The write can race the USB drop; a failure here just means the
            # reset already took the console down.
            try:
                dut.write(b"kernel reboot cold\r\n")
            except Exception:
                pass

            try:
                _reopen_console()
                dut.expect(_BOOTED_SENTINEL, timeout=90)
                return
            except Exception:
                continue

        raise RuntimeError("console did not recover after reboot")

    setattr(dut, "reboot", reboot)
    setattr(dut, "cold_boot", cold_boot)
    setattr(dut, "reopen_console", _reopen_console)


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


@pytest.fixture
def coiote_optional(request):
    """Like `coiote`, but returns None instead of skipping when no creds/config
    are available. Lets a test still run its device-side (shell/LED/log)
    assertions and only skip the portal cross-checks."""
    from coiote_client import CoioteClient

    path = (request.config.getoption("--coiote-config")
            or os.environ.get("COIOTE_CONFIG")
            or _DEFAULT_COIOTE_CONFIG)
    if not path or not os.path.isfile(path):
        return None
    with open(path) as f:
        cfg = json.load(f)
    if not cfg.get("username") or not cfg.get("password"):
        return None
    client = CoioteClient(
        api_host=cfg.get("api_host", "https://us.iot.avsystem.cloud:8087/api"),
        username=cfg["username"],
        password=cfg["password"],
    )
    client.authenticate()
    return client


@pytest.fixture
def operator(request):
    """Prompt a human operator to perform a physical action (attach/orient the
    calibrator, observe the LED, ...) and read their response.

    Output capture is suspended around the prompt so it is visible and stdin
    works; run pytest with `-s` for the smoothest experience. Returns an object
    with `prompt()`, `ask()` and `confirm()` helpers."""
    capman = request.config.pluginmanager.getplugin("capturemanager")

    class _Operator:
        def _io(self, render):
            if capman is not None:
                with capman.global_and_fixture_disabled():
                    return render()
            return render()

        def prompt(self, message):
            """Show an instruction; return the operator's typed reply (stripped,
            lowercased). An empty reply means 'done'."""
            def render():
                print("\n" + "=" * 72)
                print("OPERATOR ACTION REQUIRED")
                for line in message.strip().splitlines():
                    print("  " + line.rstrip())
                return input("  >>> press ENTER when ready (or type 'skip'): ")
            return self._io(render).strip().lower()

        def ask(self, question):
            """Ask a free-form question; return the reply (stripped, lowercased)."""
            def render():
                print("\n" + "-" * 72)
                return input(f"  ?? {question}\n  >>> ")
            return self._io(render).strip().lower()

        def confirm(self, question):
            """Ask a yes/no question; return True only on an explicit yes."""
            def render():
                print("\n" + "-" * 72)
                return input(f"  ?? {question} [y/N]: ")
            return self._io(render).strip().lower().startswith("y")

    return _Operator()
