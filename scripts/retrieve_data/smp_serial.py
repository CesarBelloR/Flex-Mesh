from smp import SimpleMgmtProtocol, MSG_START_BYTE, MSG_CONTINUE_BYTE, MSG_END_BYTE
import serial
import logging
import time

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Max data write is 127 - 2 (end bytes)
MAX_DATA_WRITE = 125


class SimpleMgmtSerial(SimpleMgmtProtocol):
    def __init__(self, conn_string, serial_handler=None):
        self.sequence_num = 0
        self.serial_handler = serial_handler
        # only grab the device part of the string
        self.conn_string = conn_string.split(",")[0]
        self.timeout = 60

    def set_port(self, port):
        self.conn_string = port
        self.serial_handler = None

    def get_serial_handler(self):
        if not self.serial_handler:
            self.serial_handler = serial.Serial(self.conn_string, 115200, timeout=10)
        return self.serial_handler

    def send_smp_data(self, data):
        """
        Send SMP Data with our serial handler. Handles sending start and end bytes

        :data: b64 Encoded SMP Data
        :return: True if able to write to serial
        """
        ser = self.get_serial_handler()
        if not ser.writable():
            return False

        start_msg = True
        logger.debug(f"Sending packet data {data}")
        remaining_data = data
        while remaining_data:
            if len(remaining_data) > MAX_DATA_WRITE:
                data_to_send = remaining_data[: MAX_DATA_WRITE - 1]
                remaining_data = remaining_data[MAX_DATA_WRITE - 1 :]
            else:
                data_to_send = remaining_data
                remaining_data = None

            logger.debug(f"Current set of data to send {data_to_send}")
            logger.debug(f"Length of data to send data {len(data_to_send)}")
            if start_msg:
                ser.write(MSG_START_BYTE)
                start_msg = False
            else:
                ser.write(MSG_CONTINUE_BYTE)

            ser.write(data_to_send)
            ser.write(MSG_END_BYTE)

        return True

    def read_smp_data(self, timeout=60):
        """
        Returns JSON of received CBOR msg
        """
        ser = self.get_serial_handler()
        if not ser.readable():
            return None

        # read until we see a msg start byte
        ser.read_until(MSG_START_BYTE)

        smp_msg_info = None
        msg_to_decode = b""
        # set timeout value <timeout> seconds from now
        timeout_value = time.time() + timeout
        while (not smp_msg_info) and time.time() < timeout_value:
            info = ser.read_until(MSG_END_BYTE)
            logger.debug("Received serial data: ")
            logger.debug(info)
            msg_to_decode += info[:-1]
            smp_msg_info = self.decode_smp_data(msg_to_decode)
            logger.debug(f"Current receive status: {smp_msg_info}")
            if not smp_msg_info:
                dbg_printout = ser.read_until(MSG_CONTINUE_BYTE)
                logger.debug("Received serial data: ")
                logger.debug(dbg_printout)

        return smp_msg_info

    def clear_serial_read(self):
        ser = self.get_serial_handler()
        if not ser.readable():
            return None

        while ser.readline():
            pass
