# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: E402 -- sys.path prelude must run before the imports below

# Add the RainMaker Neo backend root folder to sys.path
import os
import sys

_RMNG_ROOT = os.environ.get("RMNG_BACKEND_DIR")
if not _RMNG_ROOT or not os.path.exists(_RMNG_ROOT):
    raise FileNotFoundError(
        f"RainMaker Neo backend directory not found at {_RMNG_ROOT}. Set RMNG_BACKEND_DIR environment variable to the root of the RainMaker Neo backend repository."
    )
if _RMNG_ROOT not in sys.path:
    sys.path.insert(0, _RMNG_ROOT)

from py_sdk.test_user import User as _User
from py_sdk.test_group import Group as _Group
from py_sdk.test_device import Device as _Device
from test.itest.email_utils import generate_random_email as _generate_random_email

DRX_PATH = os.path.abspath(os.path.join(_RMNG_ROOT, "tools", "drx.py"))

__all__ = [
    "User",
    "Group",
    "Device",
    "generate_random_email",
    "DRX_PATH",
]

User = _User
Group = _Group
Device = _Device
generate_random_email = _generate_random_email
