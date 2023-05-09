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
    def __init__(self, battery=0.0, sensor=[0.0] * 6, timestamp=0, flag=0, ack = 0):
        self.battery = battery
        self.sensor = sensor
        self.timestamp = timestamp
        self.flag = flag
        self.ack = ack
    def pack(self):
        return struct.pack(
            "<7fIIc", self.battery, *self.sensor, self.timestamp, self.flag, self.ack
        )

    def unpack(self, packed_data, status):
        # buffer_dump(packed_data)
        unpacked_data = struct.unpack("<7fII", packed_data)
        self.battery = unpacked_data[0]
        self.sensor = list(unpacked_data[1:7])
        self.timestamp = unpacked_data[7]
        self.flag = unpacked_data[8]
        self.ack = status

def buffer_to_structs(buffer, status):
    struct_size = struct.calcsize("<7fII")
    num_records = len(buffer) // struct_size
    struct_list = []
    for i in range(num_records):
        start = i * struct_size
        end = start + struct_size
        struct_bytes = buffer[start:end]
        my_struct = record()
        my_struct.unpack(struct_bytes, status[i])
        struct_list.append(my_struct)
    return struct_list


def buffer_dump(buffer):
    hex_string = " ".join(f"{byte:02x}" for byte in buffer)
    logging.info(f"Buffer contents: {hex_string}")

def record_dump(record):
    logger.info(
            f"Battery: {round(record.battery, 2)} - Sensor: {round(record.sensor[0], 2)} {round(record.sensor[1], 2)} {round(record.sensor[2], 2)} {round(record.sensor[3], 2)} {round(record.sensor[4], 2)} {round(record.sensor[5], 2)} - Timestamp {record.timestamp} - Status {record.ack}"
        )
     
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    # Add the argument for connection string (COM port)
    parser.add_argument("--conn", help="Serial connection for Monitor Devices")
    parser.add_argument("--cmd", help="Command to execute over MCUMGR")
    parser.add_argument("--file", help="Save record to file")
    parser.add_argument("--arg", help="Argument parameter for shell interface")
    args = parser.parse_args()
    conn_port = args.conn
    if conn_port is None:
        logger.error("No connection string")
    else:
        mgr = SimpleMgmtSerial(conn_port)
        if args.cmd == "retrieve":
            status = mgr.get_status()
            # Logger the status to know about the record information
            logger.info(status)
            response_status = status["rc"]
            if response_status != 0:
                logger.error(mgr.get_report(response_status))
                exit(0)
            current_record = status["record"]
            # Get maximum element in sector, maximum sector for record and maximum range of records
            max_element_in_sector = status["info"]["max_index"]
            max_sector = status["info"]["max_sector"]
            max_offset = max_element_in_sector * max_sector
            element_in_byte = status["info"]["element_size"]
            max_byte_record_per_request = DATA_RECORD_CHUNK_SIZE - (DATA_RECORD_CHUNK_SIZE % element_in_byte)
            max_record_per_request = int(max_byte_record_per_request / element_in_byte)
            total_record = status["info"]["total"]
            total_size = element_in_byte * total_record
            offset_first_record_pos = current_record["first_record"]["index"] + current_record["first_record"]["sector"] * max_element_in_sector
            offset_last_record_pos = current_record["last_record"]["index"] + current_record["last_record"]["sector"] * max_element_in_sector
            logger.info( f"{max_element_in_sector} {max_sector} {max_record_per_request} {offset_first_record_pos} {offset_last_record_pos}")
            struct_list = []
            offset = offset_first_record_pos
            offset_total = offset_first_record_pos + total_record
            logger.info(f"Total size {total_size}")
            offset_need_update = False
            while offset <= offset_total:
                if offset > max_offset:
                    offset = 0
                    offset_total = offset_total - max_offset
                sector = int(offset / max_element_in_sector)
                if offset_need_update == True:
                    offset = (sector  * max_element_in_sector)
                    offset_need_update = False
                element = offset - (sector * max_element_in_sector)
                offset_addr = sector * SECTOR_SIZE + element * element_in_byte
                if element + max_record_per_request > max_element_in_sector:
                    length = (max_element_in_sector - element) * element_in_byte
                    offset_need_update = True
                elif offset + max_record_per_request >= offset_total:
                    length = (offset_total - offset) * element_in_byte
                else:
                    length  = max_record_per_request * element_in_byte 
                logger.info( f"{offset} - {element, sector} {offset_addr}, {length} {int(length/element_in_byte)}")
                if length != 0:
                    record_data = mgr.get_record(offset_addr, length, element, sector, int(length/element_in_byte))
                    print(record_data)
                    list_record = buffer_to_structs(record_data["data"], record_data["status"])
                    struct_list.extend(list_record)
                    offset = offset + max_record_per_request
                else:
                    break
            if args.file is not None:
                with open(args.file, 'w', newline='') as csvfile:
                    csv_writer = csv.writer(csvfile)
                    csv_writer.writerow(["Timestamp", "Battery", "Sensor 1", "Sensor 2", " Sensor 3", "Sensor 4", " Sensor 5", "Sensor 6", "Status"])
                    
                    for element in struct_list:
                        csv_writer.writerow([element.timestamp, round(element.battery, 2), round(element.sensor[0], 2), round(element.sensor[1], 2), round(element.sensor[2], 2), round(element.sensor[3], 2), round(element.sensor[4], 2), round(element.sensor[5], 2), element.ack])
        elif args.cmd == "shell":
            shell_args = args.arg
            logger.info(shell_args)
            status = mgr.get_shell(shell_args)
            response_status = status["rc"]
            if response_status != 0:
                logger.error(mgr.get_report(response_status))
                exit(0)
            logger.info(status)
        else:
            logger.warning(f"No support command {args.cmd}")
