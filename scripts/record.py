import argparse
import logging
import struct
import time
from mcumgr import McuMgrExecutor
from smp_serial import SimpleMgmtSerial
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

DATA_RECORD_CHUNK_SIZE=256

import struct

class record:
    def __init__(self, battery=0.0, sensor=[0.0]*6, timestamp=0, flag=0):
        self.battery = battery
        self.sensor = sensor
        self.timestamp = timestamp
        self.flag = flag
        
    def pack(self):
        return struct.pack('<7fII', self.battery, *self.sensor, self.timestamp, self.flag)
        
    def unpack(self, packed_data):
        unpacked_data = struct.unpack('<7fII', packed_data)
        self.battery = unpacked_data[0]
        self.sensor = list(unpacked_data[1:7])
        self.timestamp = unpacked_data[7]
        self.flag = unpacked_data[8]

def buffer_to_structs(buffer):
    struct_size = struct.calcsize('<7fII')
    num_records = len(buffer) // struct_size
    struct_list = []
    for i in range(num_records):
        start = i * struct_size
        end = start + struct_size
        struct_bytes = buffer[start:end]
        my_struct = record()
        my_struct.unpack(struct_bytes)
        logger.info( f"Battery: {round(my_struct.battery, 2)} - Sensor: {round(my_struct.sensor[0], 2)} {round(my_struct.sensor[1], 2)} {round(my_struct.sensor[2], 2)} {round(my_struct.sensor[3], 2)} {round(my_struct.sensor[4], 2)} {round(my_struct.sensor[5], 2)} - Timestamp {my_struct.timestamp}")
        struct_list.append(my_struct)
    return struct_list

def buffer_dump(buffer):
    # convert the buffer to a string of hex values
    hex_string = ' '.join(f'{byte:02x}' for byte in buffer)
    # log the hex string
    logging.info(f'Buffer contents: {hex_string}')
      
if __name__ == '__main__':
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
        if args.cmd == "retrieve" :
                status = mgr.get_status()
                logger.info(status)
                start_time = time.time()
                offset = 0
                data = mgr.get_record(offset, DATA_RECORD_CHUNK_SIZE)
                offset += DATA_RECORD_CHUNK_SIZE
                logger.info(data["data"])
                struct_list = buffer_to_structs(data["data"])
                logger.info(struct_list)
                # record_len = data["len"]
                # logger.info(f"Offset {offset}")
                # while offset < record_len:
                #     data = mgr.get_record(offset)
                #     # logger.info(data["data"])
                #     offset += DATA_RECORD_CHUNK_SIZE
                #     logger.info(f"Offset {offset}")
                    
                # end_time = time.time()
                # elapsed_time = end_time - start_time
                # logger.info(f"Process time {elapsed_time}")
        else:
            logger.warning( f"No support command {args.cmd}")
