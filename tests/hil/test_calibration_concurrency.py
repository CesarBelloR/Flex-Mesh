"""Operator-attended HIL tests for the calibration/sensor concurrency fix (FW-588).

Reproduces, on real hardware with a physical calibrator, the hang seen when a
scheduled sensor acquisition collided with a calibration run:

  * The sensor thread queued on the calibration front-end lock, won it in the gap
    between the measurement finishing and the hardware being powered down, and
    sampled a still-live calibrator. The calibrator pulls the ports low, so the
    ports were misdetected as 1-wire and the acquisition wedged on a 1-wire
    transfer that never completed - still holding the lock.
  * The data module thread then blocked forever on its next status read, so the
    calibration result was never sent and the device dropped off the network.

The signature in the original capture was:

    sensor_module: APP_EVT_DATA_GET
    app_event_manager: SENSOR_EVT_ENVIRONMENTAL_AQUIRING
    etc_sensor: ADC[0] 2985
    etc_sensor: ADC[3] 2985        <- identical values: the calibrator's network
    <silence>
    cloud: CLOUD_WRAP_EVT_RX_OFF
    modem_module: MODEM_EVT_LTE_DISCONNECTED

The calibrator is physical hardware, so these prompt an `operator`. Run with `-s`
so the prompts are visible and stdin works:

    pytest tests/hil/test_calibration_concurrency.py -v -s

The waits are driven by the device's log output (see `_wait_for`), not fixed
wall-clock windows, so a passing run finishes in seconds; the timeouts only bound
a regression so it fails fast instead of hanging the suite. The collision is made
deterministic by holding calibration at the post-`init` ownership point (the
front-end is owned for up to CONFIG_CALIBRATION_TIMEOUT, 30 s) rather than racing
the brief `calibration run` measurement.

Firmware build (HIL shell hooks; NO rtt.conf, which would disable the shell log
backend these tests read):

    west build -b etc_flex/nrf52840 -s applications/etc-app -p -- \
      -DEXTRA_CONF_FILE="debug.conf;overlay-memfault.conf;overlay-hil.conf"

Raise the etc_sensor log level to DBG when investigating a failure: at the
default INF, `ADC[%d]` is the only visible log on the acquisition path, which is
why the original capture was so sparse.
"""

import re
import time

import pytest

# Serial-log fragments. Sources are named in comments so a log-text change is
# traceable back to the code that emits it.
ACQUIRING_LOG = "SENSOR_EVT_ENVIRONMENTAL_AQUIRING"  # sensor_module.c
DATA_READY_LOG = "SENSOR_EVT_ENVIRONMENTAL_DATA_READY"  # a completed acquisition
ADC_LOG = r"ADC\[\d\]"                               # etc_sensor.c read_analog_sample()
SKIP_LOG = "skipping acquisition"                    # etc_sensor.c run_acquisition()
RX_OFF_LOG = "CLOUD_EVT_RX_OFF"                      # cloud module
LTE_DISCONNECTED_LOG = "MODEM_EVT_LTE_DISCONNECTED"  # modem_module.c
WDT_ACKED_LOG = "All modules have ACKed the wdt feed request"  # util_module.c
NO_CALIBRATOR_LOG = "No calibration tool is here"    # etc_calibration.c

# Bounded fallbacks. A skip/acquire decision lands in well under a second; these
# only cap how long we wait before declaring a regression, so the suite fails
# fast instead of hanging.
DECISION_TIMEOUT_S = 15
SETTLE_TIMEOUT_S = 12
# The module-feed watchdog request is periodic (~60 s); allow a little over one
# full period to observe a cycle.
WDT_CYCLE_TIMEOUT_S = 75


def _shell(dut, cmd):
	dut.write(f"{cmd}\r\n".encode())


def _wait_for(dut, pattern, timeout):
	"""Read serial output until `pattern` (a regex) appears, then return at once.

	These tests are driven by the device's response, not a fixed wall-clock wait:
	a passing check costs the few hundred ms until the log line lands, and
	`timeout` only bounds a regression so it fails instead of hanging.

	Returns (matched, text) where `text` is everything read so far, so a caller
	can still make "must NOT contain" assertions over the same window."""
	deadline = time.time() + timeout
	rx = re.compile(pattern)
	buf = ""
	while time.time() < deadline:
		try:
			m = dut.expect(r".+", timeout=0.5)
			s = m.group(0)
			if isinstance(s, bytes):
				s = s.decode("utf-8", "replace")
			buf += s + "\n"
			if rx.search(buf):
				return True, buf
		except Exception:
			pass
	return False, buf


def _drain(dut, seconds=1):
	"""Discard whatever the device emits for `seconds`, to clear shell echo and
	backlog before an assertion window."""
	deadline = time.time() + seconds
	while time.time() < deadline:
		try:
			dut.expect(r".+", timeout=0.3)
		except Exception:
			pass


def _hold_calibration(dut):
	"""Drive the device into a state where calibration deterministically owns the
	analog front-end, and hold it there.

	`calibration init` enters calibration, powers the rail and claims HW ownership
	synchronously, then waits up to CONFIG_CALIBRATION_TIMEOUT (30 s) for
	`calibration run`. That gap is a stable window in which every sensor
	acquisition must be skipped - far more deterministic than racing a sample
	against the brief `calibration run` measurement, which often lands in the
	post-run rail-settling window instead. Returns the init output so the caller
	can detect a 'No calibration tool is here' scan failure."""
	_shell(dut, "calibration init")
	# Normally the scan-failure line never comes, so this reads the init output
	# for a beat and then returns; if the calibrator is missing it returns early.
	_, out = _wait_for(dut, NO_CALIBRATOR_LOG, 2)
	return out


def _release_calibration(dut):
	"""Complete the held calibration so it tears down and releases the front-end,
	leaving the device clean for the next test."""
	_shell(dut, "calibration run")
	_drain(dut, 2)


def test_sample_during_calibration_is_skipped_not_hung(dut, operator):
	"""A sample requested while calibration owns the front-end must be skipped,
	and both threads must stay alive.

	This is the direct reproducer. Pre-fix, the acquisition ran against the live
	calibrator and wedged; post-fix the ownership gate makes it return -EBUSY.
	"""
	operator.prompt("Attach the calibrator to a sensor port, then press Enter")
	_drain(dut, 1)

	init_out = _hold_calibration(dut)
	if NO_CALIBRATOR_LOG in init_out:
		pytest.skip("calibrator not detected by 'calibration init'; check the "
			    "connection and re-run")

	# Calibration now owns the front-end. A sample must be skipped, not run
	# against the calibrator. Retry: a sample can also be dropped by the FW-492
	# rail-settling gate before it reaches the ownership check; ownership is held
	# ~30 s, so re-fire until the ownership skip is actually observed.
	matched = False
	log = ""
	deadline = time.time() + DECISION_TIMEOUT_S
	while time.time() < deadline and not matched:
		_shell(dut, "magnet sample")
		matched, chunk = _wait_for(dut, SKIP_LOG, 5)
		log += chunk

	# Both threads must still be alive afterwards.
	_shell(dut, "magnet status")
	alive, status = _wait_for(dut, r"settling=", DECISION_TIMEOUT_S)

	_release_calibration(dut)

	assert matched, (
		"the sample was never skipped for calibration ownership (expected "
		f"'{SKIP_LOG}'); it may be blocked before the acquisition starts:\n{log}")
	# The acquiring event is emitted just before the ownership gate rejects it.
	assert ACQUIRING_LOG in log, (
		f"sensor module never emitted the acquiring event:\n{log}")
	# Load-bearing: the sample must not have read the calibrator. Pre-fix this is
	# where ADC[0]/ADC[3] appeared with identical values.
	assert not re.search(ADC_LOG, log), (
		"the sensor sampled the analog front-end while calibration owned it; "
		f"the reading is the calibrator's reference network, not the probes:\n{log}")
	assert alive, f"sensor/app path is unresponsive after the collision:\n{status}"


def test_data_module_survives_concurrent_sample(dut, operator):
	"""The data module must not deadlock behind the sensor thread.

	Pre-fix, its status read blocked on the same mutex the wedged sensor thread
	held, so the send never happened and the device dropped off the network
	(CLOUD_EVT_RX_OFF then MODEM_EVT_LTE_DISCONNECTED). Instead of watching for
	that slow signature over tens of seconds, this drives the collision and then
	probes liveness directly: a wedged data/sensor path cannot answer an accessor.
	"""
	operator.prompt("Attach the calibrator to a sensor port, then press Enter")
	_drain(dut, 1)

	init_out = _hold_calibration(dut)
	if NO_CALIBRATOR_LOG in init_out:
		pytest.skip("calibrator not detected; check the connection and re-run")

	_shell(dut, "magnet sample")
	_, log = _wait_for(dut, SKIP_LOG, DECISION_TIMEOUT_S)

	# The calibration state must still be readable. Pre-fix, every accessor queued
	# behind the wedged sensor thread on the same mutex, so this would time out.
	_shell(dut, "calibration report")
	readable, report = _wait_for(dut, r"Ref", DECISION_TIMEOUT_S)

	_release_calibration(dut)

	assert not (RX_OFF_LOG in log and LTE_DISCONNECTED_LOG in log), (
		"observed the RX_OFF + LTE_DISCONNECTED signature of a wedged data "
		f"module:\n{log}")
	assert readable, (
		"calibration state unreadable after the collision - the data/sensor path "
		f"may be wedged on the front-end mutex:\n{report}")


def test_watchdog_feed_cycle_completes(dut):
	"""The module-feed watchdog must keep feeding: a full request -> ack -> feed
	cycle must complete within one period.

	The per-module ack set is intentionally not asserted: only debug_module
	participates in this watchdog today. A wedged sensor/data thread is caught
	instead by the ownership/deadlock tests above (a hung thread fails their fast
	liveness readbacks). The feed request is periodic, so this waits (event-driven)
	for one full cycle rather than a blind fixed window.
	"""
	ok, log = _wait_for(dut, WDT_ACKED_LOG, WDT_CYCLE_TIMEOUT_S)
	assert ok, (
		f"no watchdog feed cycle completed within {WDT_CYCLE_TIMEOUT_S}s "
		f"(expected '{WDT_ACKED_LOG}'); the module-feed watchdog may be stuck:\n{log}")


def test_sample_after_calibration_reads_real_probes(dut, operator):
	"""Once calibration releases the hardware, sampling must work normally.

	Confirms the ownership gate reopens: a permanently-stuck flag would silently
	stop all sensor readings, which is worse than the original hang.
	"""
	operator.prompt("Attach the calibrator, then press Enter")
	_drain(dut, 1)

	_shell(dut, "calibration init")
	_drain(dut, 2)
	_shell(dut, "calibration run")
	# The run completes in a few seconds and releases the front-end; the operator
	# step below is a natural barrier, so no explicit completion wait is needed.

	operator.prompt("Remove the calibrator and attach a normal probe, then press Enter")
	_drain(dut, 1)

	# The rail must settle (FW-492, ~2 s) and ownership must be released. Retry
	# the sample until it actually acquires. Wait for the acquisition's *outcome*
	# (a completed read vs. a skip), not just the acquiring event, so a skip in
	# progress is not mistaken for a success.
	acquired = False
	log = ""
	deadline = time.time() + 30
	while time.time() < deadline and not acquired:
		_shell(dut, "magnet sample")
		_, chunk = _wait_for(dut, f"{DATA_READY_LOG}|{SKIP_LOG}", SETTLE_TIMEOUT_S)
		log += chunk
		if DATA_READY_LOG in chunk and SKIP_LOG not in chunk:
			acquired = True

	assert acquired, (
		f"no normal acquisition after calibration released the hardware:\n{log}")
