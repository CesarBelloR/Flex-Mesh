from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

from ble_log_parser import BleLogParser
from util import EtcUtil as Util


class FlexDataDecrypt:
    def __init__ (self, key):
        # Ensure the key is exactly 16 bytes (128 bits)
        key = key[:16]
        if len(key) < 16:
            key = key + (b'\x00' * (16 - len(key)))

        # Create the vector for CBC method
        iv = bytes(16)
        # Create an AES-128 cipher object with the provided key and ECB mode
        self.cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())

    def decrypt_aes_128(self, data):        
        # Create a decryptor object
        decryptor = self.cipher.decryptor()
        # Decrypt the data
        decrypted_data = decryptor.update(data) + decryptor.finalize()

        return decrypted_data
    
if __name__ == "__main__":
    parser = BleLogParser("encrypted_sample.log")
    decrypt = FlexDataDecrypt(bytes.fromhex("70475440693636213646256D7744"))
    decrypted_data = []
    for data_entry in parser.get_data():
        decrypted_data.append(decrypt.decrypt_aes_128(data_entry))
    Util.write_bin_to_file("decrypted_data.bin", decrypted_data)
    Util.write_to_file("decrypted_data.txt", decrypted_data)
    print(decrypted_data)