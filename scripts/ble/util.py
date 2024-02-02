import os

class EtcUtil:
    def write_to_file(file_path, byte_data):
        with open(file_path, 'w') as file:
            for byte_array in byte_data:
                hex_string = ' '.join(format(byte, '02X') for byte in byte_array)
                file.write(hex_string + '\n')