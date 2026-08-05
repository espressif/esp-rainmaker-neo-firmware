# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""
CMake-invoked CLI: emit the CRC32 for the POSIX embedded app descriptor.

Layout, magic, and field sizes live in `app_desc.py` (which mirrors the C
header). This file is a thin wrapper so the build system can capture the CRC
as a hex string in `execute_process(... OUTPUT_VARIABLE ...)`.

Usage: app_desc_crc.py <project_ver> <project_name>
"""

import sys

import app_desc


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: app_desc_crc.py <project_ver> <project_name>\n")
        return 2
    crc = app_desc.compute_crc32(sys.argv[1], sys.argv[2])
    sys.stdout.write("%08x" % crc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
