"""HIL test for FW-1153/FW-1159: the APN comes from the SIM and is written once.

The driver configures a single PDP context, whose APN it picks from the service
provider name stored on the SIM. Contexts 2 and 3 are kept unusable because some
networks reject the attach request when several contexts are defined. All three
contexts are read back before anything is written, so only a modem that is not
already configured correctly gets written to.

The driver logs a sentinel for each step:

  SIM SPN: "<name>"                       - name read off the SIM
  SIM SPN: unavailable                    - SIM stores none, or the read failed
  APN select: "<apn>" source=spn|default  - option the name selected
  APN already correct: "<apn>"            - nothing to write
  APN applied: "<apn>"                    - context 1 was written
  APN wiped: cid2 / cid3                  - a spare context was cleared

The test is event-driven: every wait returns as soon as its sentinel appears.

Build without rtt.conf, otherwise CONFIG_SHELL_LOG_BACKEND=n hides the lines
this test reads:

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf"
"""

import re

# Match applications/etc-app/boards/etc_flex_nrf52840.conf and the Kconfig
# default the board conf does not override.
DEFAULT_APN = "iot.1nce.net"
APN_OPTIONS = {"Hologram": "hologram"}

# The modem needs to boot and the SIM to initialise before the SPN is read.
BOOT_TIMEOUT_S = 180

# A first boot writes the contexts and cycles CFUN, so registration restarts.
CONNECT_TIMEOUT_S = 600

SPN_RE = r'SIM SPN: (?:"([^"]*)"|unavailable)'
SETTLED_RE = r'APN (?:applied|already correct): "([^"]+)"'

def _decode(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def _reboot_and_read_boot_log(dut):
    """Cold-boot and return everything this boot logged before it came up.

    The driver reports its APN within the first ten seconds, well before
    etc_settings loads, so the sentinels cannot be waited for after the boot is
    complete — by then they have already gone past. Reading what preceded the
    boot marker gets them without racing the console.
    """
    dut.cold_boot()
    return _decode(dut.pexpect_proc.before)


def _settled_apn(text):
    match = re.findall(SETTLED_RE, text)
    assert match, f"No APN sentinel in output.\nOutput: {text}"
    return match[-1]


def test_apn_matches_the_sim_spn(dut):
    """The configured APN must be the one the inserted SIM selects."""
    text = _reboot_and_read_boot_log(dut)

    spn_match = re.search(SPN_RE, text)
    assert spn_match, f"Driver logged no SPN read.\nOutput: {text}"

    spn = spn_match.group(1) or ""
    expected = DEFAULT_APN
    for option_spn, option_apn in APN_OPTIONS.items():
        if spn.casefold() == option_spn.casefold():
            expected = option_apn

    assert _settled_apn(text) == expected, (
        f'SIM reports SPN "{spn}", so the APN should be "{expected}".\nOutput: {text}'
    )


def test_second_boot_writes_nothing(dut):
    """The read-before-write guard must skip every write once the modem is correct."""
    _reboot_and_read_boot_log(dut)
    text = _reboot_and_read_boot_log(dut)

    assert "APN already correct" in text, (
        "Second boot reconfigured the PDP contexts; the read-before-write guard "
        f"is not working.\nOutput: {text}"
    )
    assert "APN applied" not in text, f"Second boot rewrote context 1.\nOutput: {text}"
    assert "APN wiped" not in text, f"Second boot rewrote a spare context.\nOutput: {text}"


def test_selected_apn_connects(dut):
    """The single configured context must still bring the device online."""
    text = _reboot_and_read_boot_log(dut)

    # Registration sometimes completes before the device finishes booting, in
    # which case the sentinel is already in what the boot logged.
    if "Network connected" not in text:
        dut.expect(r"Network connected", timeout=CONNECT_TIMEOUT_S)
