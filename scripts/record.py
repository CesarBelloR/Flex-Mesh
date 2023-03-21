import argparse
import logging
import struct
import time
import csv
from mcumgr import McuMgrExecutor
from smp_serial import SimpleMgmtSerial

logging.basicConfig(
    level=logging.DEBUG, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

DATA_RECORD_CHUNK_SIZE = 256
SECTOR_SIZE = 4096
import struct


class record:
    def __init__(self, battery=0.0, sensor=[0.0] * 6, timestamp=0, flag=0):
        self.battery = battery
        self.sensor = sensor
        self.timestamp = timestamp
        self.flag = flag

    def pack(self):
        return struct.pack(
            "<7fII", self.battery, *self.sensor, self.timestamp, self.flag
        )

    def unpack(self, packed_data):
        # buffer_dump(packed_data)
        unpacked_data = struct.unpack("<7fII", packed_data)
        self.battery = unpacked_data[0]
        self.sensor = list(unpacked_data[1:7])
        self.timestamp = unpacked_data[7]
        self.flag = unpacked_data[8]

def buffer_to_structs(buffer):
    struct_size = struct.calcsize("<7fII")
    num_records = len(buffer) // struct_size
    struct_list = []
    for i in range(num_records):
        start = i * struct_size
        end = start + struct_size
        struct_bytes = buffer[start:end]
        my_struct = record()
        my_struct.unpack(struct_bytes)
        struct_list.append(my_struct)
    remain_data = bytearray(buffer[len(struct_list) * struct_size:])
    return struct_list, remain_data


def buffer_dump(buffer):
    hex_string = " ".join(f"{byte:02x}" for byte in buffer)
    logging.info(f"Buffer contents: {hex_string}")

def record_dump(record):
    logger.info(
            f"Battery: {round(record.battery, 2)} - Sensor: {round(record.sensor[0], 2)} {round(record.sensor[1], 2)} {round(record.sensor[2], 2)} {round(record.sensor[3], 2)} {round(record.sensor[4], 2)} {round(record.sensor[5], 2)} - Timestamp {record.timestamp}"
        )
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    # Add the argument for connection string (COM port)
    parser.add_argument("--conn", help="Serial connection for Monitor Devices")
    parser.add_argument("--cmd", help="Command to execute over MCUMGR")

    args = parser.parse_args()
    conn_port = args.conn
    if conn_port is None:
        logger.error("No connection string")
    else:
        mgr = SimpleMgmtSerial(conn_port)
        if args.cmd == "retrieve":
            status = mgr.get_status()
            logger.info(status)
            current_record = status["record"]
            offset_max_addr = status["info"]["max_sector"] * SECTOR_SIZE
            offset_first_addr = current_record["first_record"]["sector"] * SECTOR_SIZE + current_record["first_record"]["index"] * status["info"]["element_size"]
            offset_last_addr = current_record["last_record"]["sector"] * SECTOR_SIZE + (current_record["last_record"]["index"] + 1) * status["info"]["element_size"]
            total_size = status["info"]["element_size"] * status["info"]["total"]
            start_time = time.time()
            current_size = 0
            struct_list = []
            remain_data = []
            data = bytearray()
            offset = offset_first_addr
            logger.info(f"Total size {total_size}")
            while current_size < total_size:
                offset_need_reset = False
                length = (total_size - current_size) / DATA_RECORD_CHUNK_SIZE
                # Find the length for request
                if length > 1:
                    length = DATA_RECORD_CHUNK_SIZE
                else:
                    length = total_size - current_size
                    
                if offset + length >= offset_max_addr:
                    offset_need_reset = True
                    length = offset + length - offset_max_addr
                
                record_data = mgr.get_record(offset, length)
                data += record_data["data"]
                current_size += length
                if offset_need_reset:
                    offset = 0
                else:
                    offset += length
                # logger.info( f"Length {length} - Size {current_size}")
                # buffer_dump(data)
                list_record, remain_data = buffer_to_structs(data)
                data = bytearray()
                data += remain_data
                struct_list.extend(list_record)
                
            
            logger.info(struct_list[0])
            for element in struct_list:
                record_dump(element) 
            # record_len = data["len"]
            # logger.info(f"Offset {offset}")
            # struct_list = []
            # offset = 0
            # while offset < 2048:
            #     data = mgr.get_record(offset, DATA_RECORD_CHUNK_SIZE)
            #     logger.info(data["data"])
            #     struct_list.extend(buffer_to_structs(data["data"]))
            #     offset += DATA_RECORD_CHUNK_SIZE
            #     # logger.info(f"Offset {offset}")

            # end_time = time.time()
            # elapsed_time = end_time - start_time
            # logger.info(f"Process time {elapsed_time}")
        else:
            logger.warning(f"No support command {args.cmd}")
