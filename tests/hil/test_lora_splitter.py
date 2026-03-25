"""HIL tests for LoRa splitter temperature fields (FW-674, FW-886).

Runs on the pytest-embedded harness shared with the reclaim e2e tests: the
relay is the primary ``--port`` DUT, and the LoRa logger console is a secondary
serial passed via ``--logger-port``. The relay must be device mode 0 and the
logger device mode 1.

Each rendezvous opens the relay's LoRa RX window (``app_module trigger_rx`` on
the relay) and then triggers a sample + LoRa TX on the logger
(``app_module trigger_tx``), so the relay reliably receives the packet. LoRa is
lossy in the lab, so the relay-side test re-arms the rendezvous a few times.

Logger LoRa CSV (18 fields when a splitter is connected)::

    S, parent, version, ID, battery, pkt_num, timestamp,
    T1, T2, T3, T4, ambient, humidity, error, T1.B, T2.B, T3.B, T4.B

The error field (a single error-code slot, sent as ``*`` here) and the four
sub-port temps (T1.B-T4.B) are only present when at least one sub-port has a
valid reading; otherwise the logger omits them and the message is shorter.
"""

import os
import re
import time

import pytest
import serial

# Fields up to and including humidity (no splitter data).
MIN_LOGGER_FIELDS = 13
# Fields when the error slot + 4 splitter sub-port temps are present.
LOGGER_FIELDS_WITH_SPLITTER = 18
# Splitter sub-port temps occupy logger CSV indices 14..17 (T1.B..T4.B); index
# 13 is the error-code field the splitter path reserves with '*'.
SPLITTER_FIELDS = slice(14, 18)
# In the relay's parsed sensor array the sub-ports IN5..IN8 are indices 4..7.
RELAY_SPLITTER = slice(4, 8)
# Sensor values the relay reports once splitter data is parsed (IN1..IN8 +
# ambient + humidity).
RELAY_SENSOR_COUNT = 10
# Tolerance (deg C) when comparing the logger's transmitted temps to the values
# the relay parsed and re-formatted.
TEMP_TOLERANCE_C = 0.6
# LoRa is lossy in the lab; re-arm the rendezvous this many times.
MAX_ATTEMPTS = 6

_LOG_PATH = os.path.join(os.path.dirname(__file__), '..', '..', '.log',
                         'lora-splitter-logger-console.log')

# The shell colorizes log lines, so captured lines carry ANSI escapes (e.g. a
# trailing ``\x1b[0m`` before the newline). Strip them before parsing.
_ANSI = re.compile(r'\x1b\[[0-9;]*[a-zA-Z]')


def _clean(captured):
    """Decode a pexpect/SerialConsole capture (bytes or str) and strip ANSI.

    pytest-embedded's ``dut.expect`` returns bytes groups; ``SerialConsole``
    returns str. Normalize both to a clean str.
    """
    if isinstance(captured, (bytes, bytearray)):
        captured = captured.decode('utf-8', errors='replace')
    return _ANSI.sub('', captured)


class SerialConsole:
    """Minimal pyserial console with a pexpect-style search-with-timeout that
    mirrors everything read to a log file."""

    def __init__(self, port, baud=115200, logfile=None):
        self._ser = serial.Serial(port, baud, timeout=0.1)
        self._buf = ''
        self._log = open(logfile, 'w') if logfile else None

    def send(self, cmd):
        self._ser.write(f'{cmd}\r\n'.encode())

    def _read(self):
        data = self._ser.read(4096)
        if data and self._log:
            self._log.write(data.decode('utf-8', errors='replace'))
            self._log.flush()
        return data

    def expect(self, pattern, timeout):
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self._read()
            if data:
                self._buf += data.decode('utf-8', errors='replace')
                m = rx.search(self._buf)
                if m:
                    self._buf = self._buf[m.end():]
                    return m
        raise TimeoutError(f'Pattern {pattern!r} not seen within {timeout}s')

    def drain(self):
        time.sleep(0.3)
        self._read()
        self._ser.reset_input_buffer()
        self._buf = ''

    def close(self):
        self._ser.close()
        if self._log:
            self._log.close()


@pytest.fixture
def logger(request):
    port = request.config.getoption('--logger-port')
    if not port:
        pytest.skip('No --logger-port given; the splitter test needs the logger console')
    con = SerialConsole(port, logfile=os.path.abspath(_LOG_PATH))
    try:
        yield con
    finally:
        con.close()


def _relay_cmd(dut, cmd, expect, timeout=15):
    """Send a shell command to the relay DUT and wait for its response."""
    dut.write(f'{cmd}\r\n'.encode())
    return dut.expect(expect, timeout=timeout)


def _assert_roles(dut, logger):
    """The primary --port DUT must be the relay (0); --logger-port the logger (1)."""
    dut.write(b'settings get_device\r\n')
    assert int(dut.expect(r'Device mode (\d+)', timeout=10).group(1)) == 0, \
        'primary --port DUT is not the relay (expected device mode 0)'
    logger.send('settings get_device')
    assert int(logger.expect(r'Device mode (\d+)', 10).group(1)) == 1, \
        '--logger-port DUT is not the LoRa logger (expected device mode 1)'


def _valid_temp_or_disconnected(value):
    """True if *value* is a float string or the ``*`` disconnected marker."""
    value = value.strip()
    if value == '*':
        return True
    try:
        float(value)
        return True
    except ValueError:
        return False


def _splitter_decoded_ok(relay_vals, logger_vals):
    """True if the relay's parsed IN5..IN8 match the logger's T1.B..T4.B.

    A ``*`` (disconnected) sub-port on the logger must read back as the
    NO_CONNECTED sentinel (a large negative) on the relay; otherwise the values
    must agree within TEMP_TOLERANCE_C.
    """
    if len(relay_vals) < 4:
        return False
    for logv, relayv in zip(logger_vals, relay_vals):
        relayv = float(relayv)
        if logv == '*':
            if relayv > -200:
                return False
        elif abs(relayv - float(logv)) > TEMP_TOLERANCE_C:
            return False
    return True


def _logger_tx_fields(logger):
    """Trigger a logger sample + LoRa TX; return the transmitted CSV fields.

    Anchors on the trailing newline so the full message line (and therefore the
    trailing splitter fields) is captured, not a partial read.
    """
    logger.drain()
    logger.send('app_module trigger_tx')
    m = logger.expect(r'Msg (S,[^\r\n]+)\r?\n', timeout=30)
    return _clean(m.group(1)).strip().rstrip(',').split(',')


def test_logger_tx_includes_splitter_fields(dut, logger):
    """Logger LoRa TX carries the splitter sub-port temps (T1.B-T4.B)."""
    _assert_roles(dut, logger)
    fields = _logger_tx_fields(logger)

    assert fields[0] == 'S', f"first field should be 'S', got {fields[0]!r}"
    assert len(fields) >= MIN_LOGGER_FIELDS, \
        f'expected >= {MIN_LOGGER_FIELDS} logger fields, got {len(fields)}: {fields}'

    # Base temperature fields T1-T4 (7-10), ambient (11), humidity (12).
    for idx in range(7, 13):
        assert _valid_temp_or_disconnected(fields[idx]), \
            f'field {idx} has invalid value {fields[idx]!r}'

    if len(fields) < LOGGER_FIELDS_WITH_SPLITTER:
        pytest.skip(f'No splitter connected to the logger (only {len(fields)} '
                    f'fields; sub-port temps omitted): {fields}')

    for i, val in enumerate(fields[SPLITTER_FIELDS]):
        assert _valid_temp_or_disconnected(val), \
            f'splitter field T{i + 1}.B has invalid value {val!r}'


def test_relay_receives_and_forwards_splitter(dut, logger):
    """Relay receives the splitter packet over LoRa, decodes the sub-port temps
    into IN5..IN8, and forwards them in the relay->cloud legacy CSV.

    The relay's ``Sensor`` line is logged immediately on decode, so it is the
    authoritative per-reception signal. The ``Relay message`` (cloud-encode) is
    decoupled and encodes whatever records are buffered, so the relay record
    buffer is erased each attempt to make that encode unambiguously the packet
    we just sent. LoRa is lossy, so the rendezvous is re-armed up to
    MAX_ATTEMPTS times.
    """
    _assert_roles(dut, logger)

    last = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        # Erase the relay record buffer so the next cloud-encode reflects only
        # the packet received in this attempt (no backlog). The flash erase of
        # the record partition takes ~18 s, so allow generous time.
        _relay_cmd(dut, 'record erase', r'Erase done', timeout=30)
        # Open the relay's LoRa RX window, then transmit from the logger.
        # Anchoring on the command's own response also discards stale relay
        # output, so the Sensor / Relay message lines we read next are fresh.
        _relay_cmd(dut, 'app_module trigger_rx', r'Triggering relay LoRa RX')
        time.sleep(2)
        fields = _logger_tx_fields(logger)
        last = fields

        if len(fields) < LOGGER_FIELDS_WITH_SPLITTER:
            pytest.skip(f'No splitter connected to the logger (only {len(fields)} '
                        f'fields; sub-port temps omitted): {fields}')

        splitter = [f.strip() for f in fields[SPLITTER_FIELDS]]
        splitter_seq = ','.join(splitter)

        try:
            sensors_raw = dut.expect(r'Sensor ([^\r\n]+)\r?\n', timeout=30).group(1)
            cloud_raw = dut.expect(r'Relay message ([^\r\n]+)\r?\n', timeout=20).group(1)
        except Exception:
            print(f'attempt {attempt}: relay did not receive the packet, retrying')
            continue

        sensors = _clean(sensors_raw).split()
        cloud_csv = _clean(cloud_raw).strip()

        # 1) Decode: the relay's parsed IN5..IN8 must match the transmitted
        #    splitter temps (proves logger encode + LoRa + relay decode).
        decoded_ok = (len(sensors) >= RELAY_SENSOR_COUNT and
                      _splitter_decoded_ok(sensors[RELAY_SPLITTER], splitter))
        # 2) Forward: the splitter temps appear contiguously in the cloud CSV
        #    (an exact substring also verifies their position right after
        #    isParent,isReclaimed and their formatting).
        forwarded_ok = splitter_seq in cloud_csv

        if decoded_ok and forwarded_ok:
            print(f'attempt {attempt}: relay parsed {len(sensors)} sensors '
                  f'(IN5-8={sensors[RELAY_SPLITTER]}); splitter {splitter_seq!r} '
                  f'forwarded.\n  cloud CSV: {cloud_csv}')
            return

        print(f'attempt {attempt}: decoded_ok={decoded_ok} forwarded_ok={forwarded_ok} '
              f'(logger splitter {splitter_seq!r}, relay sensors {sensors}, '
              f'cloud {cloud_csv!r}), retrying')

    pytest.fail(f'relay never cleanly received/forwarded the splitter packet '
                f'after {MAX_ATTEMPTS} attempts; last logger TX: {last}')
