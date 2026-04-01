import pytest
import time


def pytest_addoption(parser):
    parser.addoption("--soak-hours", type=float, default=24.0,
                     help="How long to run the soak test (hours, default 24)")
    parser.addoption("--e2e-timeout", type=int, default=30,
                     help="Max wait time in minutes for e2e reclaim test (default 30)")


@pytest.fixture(autouse=True)
def add_dut_methods(dut):
    def reboot():
        """Reboot the device and wait for it to come back."""
        dut.write(b"kernel reboot\r\n")
        time.sleep(5)
        dut.write(b"\r\n")
        dut.expect(r"uart:~\$", timeout=20)

    setattr(dut, "reboot", reboot)
