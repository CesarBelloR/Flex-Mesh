import logging
import base64
import cbor2
import crc16
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

        calc_crc = int.to_bytes(crc16.crc16xmodem(smp_msg, 0), 2, "big")
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
        cbormsg = cbor2.dumps(data)
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
        generated_crc = int.to_bytes(crc16.crc16xmodem(smp_msg), 2, "big")
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

    def get_image_state(self):
        smp_msg = self.get_json_msg("image_mgmt", "state", "get_request", "images")
        logger.debug(f"Current image state {smp_msg}")
        return smp_msg

    def set_image_state(self, hash_string, confirm=False):
        json_msg = {"hash": hash_string, "confirm": confirm}
        self.send_generic_smp_smg("image_mgmt", "state", "set_request", json_msg)
        smp_response = self.read_smp_data()[0]
        return smp_response

    def get_status(self):
        self.send_generic_smp_smg("etc", "status", "request")
        smp_response = self.read_smp_data()[0]
        return smp_response

    def get_record(self, offset, length):
        json_msg = {"off": offset, "length": length}
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
