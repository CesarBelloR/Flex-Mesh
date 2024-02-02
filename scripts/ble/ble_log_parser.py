import os
from util import EtcUtil

class BleLogParser:
    def __init__ (self, filename):
        self.filename = filename
        self.parse_file()
        file_path = os.path.splitext(self.filename)[0] + "_out.txt"
        EtcUtil.write_to_file(file_path, self.packet_data)
        
    def get_data(self):
        return self.packet_data
    
    def parse_file(self):
        all_data = []
        
        with open(self.filename, "r") as f:
            for line in f:
                data = self.parse_line(line)
                if data is not None:
                    all_data.append(data)
            self.packet_data = self.parse_data(all_data)
            
    def parse_data(self, data):
        message_id = -1
        frame_id_old = -1
        packet = bytearray()
        all_packets = []
        
        for data_entry in data:
            if message_id != int(data_entry[0]):
                message_id = int(data_entry[0])
                packet = bytearray()
                frame_id_old = -1
                
            frame_id = int(data_entry[1])
            if frame_id != (frame_id_old + 1):
                continue
            
            # Skip message ID, frame ID and payload length on first frame (4 bytes total)
            if frame_id == 0:
                packet.extend(data_entry[4:])
            # Skip message and frame ID on frames after first frame
            elif frame_id > 0:
                packet.extend(data_entry[4:])
            
            frame_id_old = frame_id
            all_packets.append(packet)
        return all_packets
    
    # Example line:
    # 2024-02-01T17:07:24.677Z INFO Attribute value changed, handle: 0x1D, value (0x): 23-02-00-00-C2-...
    def parse_line(self, line: str):
        if len(line) == 0:
            return None
        if "Attribute value changed, handle: 0x1D, value (0x):" in line:
            entry = ''.join(''.join(line.split(' ')[1:]).split(':')[2:])
            return self.parse_entry(entry)
        
        return None
    
    def parse_entry(self, entry: str):
        data = bytearray()
        for byte_str in entry.split('-'):
            data.extend(bytearray.fromhex(byte_str))
        return data
    

        
if __name__ == "__main__":
    parser = BleLogParser("data.log")