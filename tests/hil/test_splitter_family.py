"""Operator-attended HIL test for TMP1826 and TMP1827 splitters on real hardware.

Covers FW-808: splitter boards are moving to the cheaper TMP1827(N), which is
register-, command- and timing-compatible with the TMP1826 and differs only in
its 1-Wire ROM family code (0x27 vs 0x26). Both parts will be in the field, so
one image has to detect and drive either, including a mix across ports.

The point of running this on hardware is that the unit tests stub the 1-Wire
bus: only a real TMP1827 proves the ROM search, the branch-select GPIO write and
the analog switch behind it all work against the actual part.

Both parts are wired at the same time rather than swapped onto one port, so a
single acquisition exercises the mix. Which port carries which part does not
matter - the tests read that back from the device.

Stale-state-after-removal is not retested here: the reported family lives in the
same per-port array as the splitter-present flag, which
`test_splitter_detection.py` already covers.

Run with `-s` so the operator prompts are visible and stdin works:

    pytest tests/hil/test_splitter_family.py -v -s

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

The tests run in definition order and share the wiring set up by the first
prompt.
"""

import pytest

from acquisition import (
    FAMILY_NONE,
    FAMILY_TMP1826,
    FAMILY_TMP1827,
    acquire_parts,
    assert_present,
)

WIRING = """
    Wire the device as follows, then power-cycle it:
      - one port: TMP1826 splitter
      - another port: TMP1827 splitter (HW2-808 board), both branches populated,
        with the two probes at clearly different temperatures (e.g. hold one in
        your hand)
      - remaining ports: anything, including nothing

    Which port carries which splitter does not matter.
"""


def _ports_with(parts, family):
    """0-based indices of the ports reporting `family`."""
    return [i for i, f in enumerate(parts) if f == family]


def _require_port(parts, family, part_name):
    ports = _ports_with(parts, family)
    if not ports:
        pytest.fail(f"no {part_name} splitter found (family codes {'/'.join(parts)}); "
                    f"is one wired and did the device see it?")
    return ports[0]


def test_both_splitter_families_are_detected(dut, operator):
    """One image must recognise a TMP1826 and a TMP1827 on the same bus scan.
    Before FW-808 the firmware compared the family code against the single
    devicetree value, so the TMP1827 was silently ignored."""
    operator.prompt(WIRING)

    splitters, types, adc, parts = acquire_parts(dut)

    _require_port(parts, FAMILY_TMP1826, "TMP1826")
    _require_port(parts, FAMILY_TMP1827, "TMP1827")

    # The two log lines are produced from one array, so they must never disagree
    # about which ports have a splitter.
    for port, family in enumerate(parts):
        expected = "0" if family == FAMILY_NONE else "1"
        assert splitters[port] == expected, (
            f"port {port + 1} reports family {family} but splitter flag "
            f"{splitters[port]} (mask {splitters}, parts {'/'.join(parts)})")


def test_tmp1827_switches_between_its_branches(dut, operator):
    """Detection alone is not enough: the TMP1827's GPIOs drive the analog switch
    that selects the A or B branch, so both branches must read independently."""
    splitters, types, adc, parts = acquire_parts(dut)

    port = _require_port(parts, FAMILY_TMP1827, "TMP1827")
    input_a = port + 1
    input_b = port + 5

    assert_present(types, adc, input_a)
    assert_present(types, adc, input_b)
    assert adc[input_a - 1] != adc[input_b - 1], (
        f"port {port + 1}.A and {port + 1}.B both read {adc[input_a - 1]}; the "
        f"branch switch is not taking effect on the TMP1827, so the same probe "
        f"is being measured twice")
