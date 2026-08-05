#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Generate a global sdkconfig.h file from a config file.

Usage: gen_sdkconfig_h.py <config_file> <output_file>

This script:
1. Loads the config file
2. Extracts all CONFIG_ symbols
3. Generates a global sdkconfig.h file with all CONFIG_ symbols
"""

import sys
import os


def get_config_values(config_path) -> dict[str, str]:
    """Get the config values from a config file."""
    config_values = {}
    with open(config_path, "r") as f:
        for line in f:
            if line.startswith("CONFIG_"):
                key, value = line.strip().split("=")
                config_values[key] = value
    return config_values


def write_sdkconfig_h(output_path, config_values):
    """Write a sdkconfig.h file with the config values."""
    with open(output_path, "w") as f:
        f.write("/* Auto-generated sdkconfig.h */\n")
        f.write("/* DO NOT EDIT - Generated from component configs */\n")
        f.write("#ifndef SDKCONFIG_H\n")
        f.write("#define SDKCONFIG_H\n\n")
        for key, value in config_values.items():
            if value in ("y", "n", "m"):
                # Bool/tristate value
                value = 1 if value == "y" else 0
            f.write(f"#define {key} {value}\n")
        f.write("\n#endif /* SDKCONFIG_H */\n")


def main():
    if len(sys.argv) != 3:
        print("Usage: gen_sdkconfig_h.py <config_file> <output_file>")
        sys.exit(1)

    config_path = sys.argv[1]
    output_path = sys.argv[2]

    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    # Load the config file and get all symbols
    config_values = get_config_values(config_path)

    # Generate header file
    write_sdkconfig_h(output_path, config_values)
    print(f"Generated sdkconfig.h: {output_path}")


if __name__ == "__main__":
    main()
