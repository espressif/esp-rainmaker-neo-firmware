# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Kconfig utility functions.
"""

import tempfile


def gen_sdkconfig_defaults(config: dict[str, any]) -> str:
    """
    Generate a temporary SDK config defaults file. Must be deleted after use.
    """
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".defaults", delete=False
    ) as txt_file:
        for k, v in config.items():
            # add CONFIG_ prefix if not present
            if not k.startswith("CONFIG_"):
                k = f"CONFIG_{k}"
            # add quotes if the value is a string and not y/n/m
            if isinstance(v, str) and v not in ["y", "n", "m"]:
                v = f'"{v}"'
            txt_file.write(f"{k}={v}\n")
        return txt_file.name
