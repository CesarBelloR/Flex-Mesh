import logging
import base64
import cbor2
import time
from smp_spec import SMP_DICT
import os
import hashlib


logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# SMP_CONSTANTS
MSG_START_BYTE = b"\x06\x09"
MSG_CONTINUE_BYTE = b"\x04\x14"
MSG_END_BYTE = b"\x0a"

CRC16_XMODEM_TABLE = [
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
        0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
        0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
        0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
        0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
        0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
        0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
        0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
        0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
        0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
        0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
        0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
        0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
        0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
        0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
        0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
        0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
        0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
        0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
        0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
        0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
        0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
        ]

def _crc16(data, crc, table):
    for byte in data:
        crc = ((crc<<8)&0xff00) ^ table[((crc>>8)&0xff)^byte]
    return crc & 0xffff


def crc16xmodem(data, crc=0):
    return _crc16(data, crc, CRC16_XMODEM_TABLE)

class SimpleMgmtProtocol:
    def __init__(self):
        self.sequence_num = 0

    def send_smp_data(self, data):
        logger.error(f"Override send_smp_data(self, data) to send on desired interface")
        return False

    def read_smp_data(self, timeout=60):
        logger.error(
            f"Override read_smp_data(self, timeout=60) to read from desired interface"
        )
        return dict()

    def decode_smp_data(self, bytes):
        logger.debug(f"Bytes to decode {bytes}")
        try:
            decoded_msg = base64.b64decode(bytes)
        except Exception as err:
            logger.warning("Unable to decode message")
            logger.warning(err)
            return None

        logger.debug(f"Decoded msg: {decoded_msg}")

        # Verify we've received the whole message
        recv_dec_msg_len = int.from_bytes(decoded_msg[:2], "big")
        actual_dec_msg_len = len(decoded_msg[2:])
        logger.debug(f"Received Message length: {recv_dec_msg_len}")
        logger.debug(f"Actual message length: {actual_dec_msg_len}")
        if actual_dec_msg_len < recv_dec_msg_len:
            logger.debug(f"All information not received, not decoding")
            return None

        # Verify CRC
        recv_crc = decoded_msg[-2:]
        smp_msg = decoded_msg[2:-2]

        calc_crc = int.to_bytes(crc16xmodem(smp_msg, 0), 2, "big")
        if calc_crc != recv_crc:
            logger.debug(f"Caclulated crc {calc_crc}")
            logger.debug(f"Received CRC {recv_crc}")
            logger.error("Calculated CRC does not equal received")
            return None
        return self.decode_smp_msg(smp_msg)

    def decode_smp_msg(self, msg):
        logger.debug(f"SMP Msg to decode: {msg}")

        op_code = int.from_bytes(msg[0:1], "big")
        flags = int.from_bytes(msg[1:2], "big")
        data_length = int.from_bytes(msg[2:4], "big")
        group_id = int.from_bytes(msg[4:5], "big")
        # TODO check seq num
        seq_num = msg[5:7]
        logger.debug(f"Seq Num: {seq_num}")
        cmd_id = int.from_bytes(msg[7:8], "big")
        data = msg[8:]
        if len(data) != data_length:
            logger.error(
                f"Expected Data length {data_length} does not match actual {len(data)}, some decoding issues may occur"
            )
        decoded_cbor = cbor2.loads(data)
        logger.debug(f"Decoded CBOR: {decoded_cbor}")
        return decoded_cbor, op_code, group_id, cmd_id, flags

    def encode_smp_msg(self, opcode, flags, group_id, cmd_id, seq_num, data):
        """ """
        msg_byte_array = bytearray()
        # Generate cbor that will be used
        cbormsg = cbor2.dumps(data, canonical=True)
        cbormsg_length = len(cbormsg)
        logger.debug("SMP Message info")
        logger.debug(
            f"Grp Id: {group_id} Cmd Id: {cmd_id} OpCode: {opcode}, CBOR: {data}"
        )

        # Create message
        msg_byte_array.append(opcode)
        msg_byte_array.append(flags)
        msg_byte_array.extend(int.to_bytes(cbormsg_length, 2, "big"))
        msg_byte_array.extend(int.to_bytes(group_id, 2, "big"))
        msg_byte_array.extend(int.to_bytes(seq_num, 1, "big"))
        msg_byte_array.append(cmd_id)
        msg_byte_array.extend(cbormsg)

        encoded_msg = bytes(msg_byte_array)
        logger.debug(f"SMP Mesage Bytes: {encoded_msg}")
        return encoded_msg

    def encode_smp_data(self, smp_msg):
        byte_msg_array = bytearray()

        # smp msg length plus CRC
        encoded_msg_len = int.to_bytes(len(smp_msg) + 2, 2, "big")
        generated_crc = int.to_bytes(crc16xmodem(smp_msg), 2, "big")
        encoded_smp_msg = base64.b64encode(encoded_msg_len + smp_msg + generated_crc)
        byte_msg_array.extend(encoded_smp_msg)

        encoded_smp_data = bytes(byte_msg_array)
        return encoded_smp_data

    def send_generic_smp_smg(self, group_key, cmd_key, request, json_msg=None, flags=0):
        group_id = SMP_DICT[group_key]["group_id"]
        op_code = SMP_DICT[group_key][cmd_key][request]["op_code"]
        cmd_id = SMP_DICT[group_key][cmd_key]["cmd_id"]
        if json_msg is not None:
            json_out = json_msg
        else:
            json_out = SMP_DICT[group_key][cmd_key][request]["json"]

        encoded_msg = self.encode_smp_msg(
            op_code, flags, group_id, cmd_id, self.get_seq_num(), json_out
        )
        smp_msg_data = self.encode_smp_data(encoded_msg)
        self.send_smp_data(smp_msg_data)

    def send_echo_msg(self, string):
        default_key = "default"
        echo_key = "echo"
        req_key = "request"
        read_string = "r"
        echo_msg = SMP_DICT[default_key][echo_key][req_key]["json"]
        echo_msg["d"] = string
        return self.get_json_msg(default_key, echo_key, req_key, read_string, echo_msg)

    def send_reset_command(self):
        self.send_generic_smp_smg("default", "reset", "request")

    def get_json_msg(self, group_key, cmd_key, req_key, json_key, json_msg=None):
        json_data = {}
        retry = 0
        while json_key not in json_data:
            time.sleep(0.01)
            if json_msg:
                self.send_generic_smp_smg(
                    group_key, cmd_key, req_key, json_msg=json_msg
                )
            else:
                self.send_generic_smp_smg(group_key, cmd_key, req_key)
            smp_msg = self.read_smp_data()

            if smp_msg is not None:
                json_data = smp_msg[0]
            else:
                json_data = {}
            retry += 1
            if retry > 10:
                break

        if json_key not in json_data:
            logger.error(f"Unable to find key {json_key}")
            return ""
        return json_data[json_key]

    def encode_hex_string(self, hex_string):
        bytes_format = bytes(bytearray.fromhex(hex_string))
        encoded_string = base64.b64encode(bytes.fromhex(bytes_format))
        return encoded_string

    def get_seq_num(self):
        return self.sequence_num
    
    def get_report(self, status):
        if status == 0:
            return "No error."
        elif status == 1:
            return "Unknown error."
        elif status == 2:
            return "Insufficient memory."
        elif status == 3:
            return "Error in input value."
        elif status == 4:
            return "Operation timed out."
        elif status == 5:
            return "No such file/entry."
        elif status == 6:
            return "Current state disallows command."
        elif status == 7:
            return "Response too large."
        elif status == 8:
            return "Command not supported."
        elif status == 9:
            return "Corrupt."
        elif status == 10:
            return "Command blocked by processing of other command."
        elif status == 11:
            return "Access to specific function, command or resource denied."
        elif status == 12:
            return "User errors defined from 256 onwards"
    
    def get_image_state(self):
        smp_msg = self.get_json_msg("image_mgmt", "state", "get_request", "images")
        logger.debug(f"Current image state {smp_msg}")
        return smp_msg

    def set_image_state(self, hash_string, confirm=False):
        json_msg = {"hash": hash_string, "confirm": confirm}
        self.send_generic_smp_smg("image_mgmt", "state", "set_request", json_msg)
        smp_response = self.read_smp_data()[0]
        return smp_response

    def get_shell(self, command):
        json_msg = {"argv": command}
        self.send_generic_smp_smg("shell_mgmt", "shell", "request", json_msg)
        smp_response = self.read_smp_data()[0]
        return smp_response

    def get_status(self):
        self.send_generic_smp_smg("etc", "status", "request")
        smp_response = self.read_smp_data()[0]
        return smp_response

    def get_clean(self):
        self.send_generic_smp_smg("etc", "clean", "request")
        smp_response = self.read_smp_data()[0]
        return smp_response
    
    def get_record(self, offset, length, element, sector, num_element):
        json_msg = {"off": offset, "length": length, "element": element, "sector": sector, "num_element": num_element}
        self.send_generic_smp_smg("etc", "record", "request", json_msg)
        smp_response = self.read_smp_data()[0]
        return smp_response

    def upload_image_chunk(
        self, first_chunk, data_byte_str, offset, image_num=None, len=None, sha=None
    ):
        """
        upload_image chunk sends an image chunk to the device. If first_chunk is set, offset is forced to
        zero and required info is filled out

        ## CAUTION: THIS IS UNTESTED CODE, MAY NOT WORK AS EXPECTED

        :param first_chunk:     (bool)  True if this is the first chunk of an image upload
        :param data_byte_str:   (bytes) data byte string to send in chunk
        :param offset:          (uint)  offset of image chunk
        :param image_num:       (uint)  the image number to write to
        :param len:             (uint)  The length of the total image to upload
        :param sha:             (str)   string identifying update session
        :return: json response of image upload
        """
        if first_chunk:
            assert image_num is not None and len is not None and sha is not None
            json_msg = {
                "image": image_num,
                "len": len,
                "off": 0,
                "sha": sha,
                "data": data_byte_str,
            }
        else:
            json_msg = {"off": offset, "data": data_byte_str}

        self.send_generic_smp_smg("image_mgmt", "upload", "request", json_msg)

        smp_response = self.read_smp_data()[0]
        return smp_response

    def erase_image(self, image_slot_num):
        json_msg = {"slot": image_slot_num}
        self.send_generic_smp_smg("image_mgmt", "erase", "request", json_msg)
        smp_response = self.read_smp_data()[0]
        return smp_response
