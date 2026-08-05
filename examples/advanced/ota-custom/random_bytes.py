# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: CC0-1.0

"""
Usage: python random_bytes.py [size] [filename]
Generates a random binary file.
- size: int, default INPUT_SIZE_MAX (clamped between INPUT_SIZE_MIN and INPUT_SIZE_MAX)
- filename: str, default 'random_bytes.bin'
"""

import os
import sys

INPUT_SIZE_MAX = 4096
INPUT_SIZE_MIN = 256

try:
    input_size = int(sys.argv[1]) if len(sys.argv) > 1 else INPUT_SIZE_MAX
except ValueError:
    input_size = INPUT_SIZE_MAX

filename = sys.argv[2] if len(sys.argv) > 2 else "random_bytes.bin"

fileSizeInBytes = max(INPUT_SIZE_MIN, min(input_size, INPUT_SIZE_MAX))

print(f"Generating {fileSizeInBytes} byte(s) of random data to {filename}")
buffer = os.urandom(fileSizeInBytes)
xor_value = buffer[0]
for i in range(1, fileSizeInBytes):
    xor_value ^= buffer[i]
print(f"==> XOR byte-wise = 0x{xor_value:02X}")

with open(filename, "wb") as fout:
    fout.write(buffer)
