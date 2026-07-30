"""Operator-attended HIL test for splitter detection on real hardware.

Covers FW-1071: a port with no splitter must not publish a B branch. The bug was
that the B branch was classified once and never cleared, so a port kept
reporting inputs 5..8 after its splitter was removed - and because the branch
switch failed silently, the value it reported was the A branch probe measured a
second time.

Firmware maps splitter A branches to inputs 1..4 and B branches to inputs 5..8,
so input 7 is what the portal shows as port 3.B.

Run with `-s` so the operator prompts are visible and stdin works:

    pytest tests/hil/test_splitter_detection.py -v -s

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend this test reads):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

**The order of the tests in this file is load-bearing.** pytest collects in
definition order, and each test leaves the hardware in the state the next one
starts from. The regression in particular depends on the device NOT being
rebooted between the splitter being present and being removed: a reboot clears
the stale state and hides the bug, which is why the field only saw it after a
probe swap.
"""

from acquisition import (
    acquire as _acquire,
    assert_absent as _assert_absent,
    assert_present as _assert_present,
)


def test_probes_without_splitters_have_no_b_branch(dut, operator):
    """The FW-1071 configuration: splitters on ports 1 and 2, plain probes wired
    directly to ports 3 and 4. Ports 3 and 4 have no B branch to report."""
    operator.prompt("""
        Wire the device as follows, then power-cycle it:
          - port 1: splitter, both branches populated
          - port 2: splitter, both branches populated
          - port 3: plain probe, no splitter
          - port 4: plain probe, no splitter
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters == "1100", (
        f"splitters detected on {splitters}, expected 1100 (ports 1 and 2 only)")
    for input_no in (1, 2, 3, 4, 5, 6):
        _assert_present(types, adc, input_no)
    _assert_absent(types, adc, 7)
    _assert_absent(types, adc, 8)


def test_splitter_on_port_three_reports_a_distinct_b_branch(dut, operator):
    """Port 3 gains a splitter. Its B branch must appear, and must be its own
    measurement rather than a repeat of the A branch."""
    operator.prompt("""
        Move a splitter onto port 3, both branches populated, and put probes at
        clearly different temperatures on its two branches (e.g. hold one in
        your hand). Leave ports 1, 2 and 4 as they are.

        Do NOT power-cycle or reset the device.
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters[2] == "1", f"no splitter detected on port 3 (mask {splitters})"
    _assert_present(types, adc, 3)
    _assert_present(types, adc, 7)
    assert adc[2] != adc[6], (
        f"port 3.A and 3.B both read {adc[2]}; the branch switch is not taking "
        f"effect and the same probe is being measured twice")


def test_removing_a_splitter_clears_its_b_branch(dut, operator):
    """The regression. Before the fix, port 3 kept reporting a B branch after
    its splitter was removed, carrying a duplicate of the port 3 probe."""
    operator.prompt("""
        Remove the splitter from port 3 and wire a plain probe there instead.
        Leave ports 1, 2 and 4 as they are.

        Do NOT power-cycle or reset the device - a reboot clears the stale state
        this test is looking for.
    """)

    splitters, types, adc = _acquire(dut)

    assert splitters[2] == "0", (
        f"port 3 still reports a splitter (mask {splitters}) after it was removed")
    _assert_present(types, adc, 3)
    _assert_absent(types, adc, 7)
