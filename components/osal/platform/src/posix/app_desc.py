# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""
Shared helpers for the POSIX embedded app descriptor.

Source of truth for the descriptor layout, magic, and field sizes is the C
header `osal_app_desc_posix.h`. Everything in this module is derived
from that header so the build-time CRC generator and the runtime test
tooling stay in lockstep with the C definition.
"""

from __future__ import annotations

import os
import re
import struct
import zlib

# Path to the C header that defines the descriptor format.
_HERE = os.path.dirname(os.path.abspath(__file__))
HEADER_PATH = os.path.abspath(
    os.path.join(_HERE, "..", "..", "include", "osal_app_desc_posix.h")
)


def parse_c_int_define(header_path: str, name: str) -> int:
    """
    Parse a `#define NAME <integer-literal>` from a C header. Accepts decimal,
    hex (0x...), and any combination of trailing C integer suffixes (U, L, LL,
    ULL, etc., case-insensitive).
    """
    pattern = re.compile(
        r"^\s*#define\s+"
        + re.escape(name)
        + r"\s+([0-9A-Fa-fxX]+)[uUlL]*\s*(?://|/\*|$)"
    )
    with open(header_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.match(line)
            if m:
                return int(m.group(1), 0)
    raise RuntimeError(f"Could not find #define {name} in {header_path}")


# Constants pulled from the header. Resolved eagerly so any drift between
# Python and C surfaces at import time rather than at first use.
MAGIC: int = parse_c_int_define(HEADER_PATH, "OSAL_APP_DESC_MAGIC")
STR_LEN: int = parse_c_int_define(HEADER_PATH, "OSAL_APP_DESC_STR_LEN")
SHA_LEN: int = parse_c_int_define(HEADER_PATH, "OSAL_APP_DESC_SHA256_LEN")

# Layout: uint64 magic | uint32 secure_version | char[STR_LEN] version |
#         char[STR_LEN] project_name | uint8[SHA_LEN] app_elf_sha256 | uint32 crc32
SIZE: int = 8 + 4 + STR_LEN + STR_LEN + SHA_LEN + 4
CRC_PAYLOAD_LEN: int = SIZE - 4


def _pad(value: str, length: int) -> bytes:
    """Encode value as UTF-8, truncate to leave room for a NUL terminator, NUL-pad to length."""
    encoded = value.encode("utf-8")[: length - 1]
    return encoded.ljust(length, b"\x00")


def build_payload(
    project_ver: str, project_name: str, secure_version: int = 0
) -> bytes:
    """
    Produce the exact bytes the C compiler will lay out for the descriptor's
    CRC payload (i.e. every field except the trailing crc32 itself).
    """
    return (
        struct.pack("<QI", MAGIC, secure_version)
        + _pad(project_ver, STR_LEN)
        + _pad(project_name, STR_LEN)
        + b"\x00" * SHA_LEN
    )


def compute_crc32(project_ver: str, project_name: str, secure_version: int = 0) -> int:
    """Compute the CRC32 value that the C build expects to find in the descriptor."""
    return (
        zlib.crc32(build_payload(project_ver, project_name, secure_version))
        & 0xFFFFFFFF
    )
