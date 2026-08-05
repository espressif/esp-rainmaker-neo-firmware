#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Generate a global sdkconfig.cmake file from a config file.

Usage: gen_sdkconfig_cmake.py <config_file> <output_file>

This script:
1. Loads the config file
2. Extracts all CONFIG_ symbols
3. Generates a global sdkconfig.cmake file with all CONFIG_ symbols
"""

import sys
import os


def get_config_values(config_path) -> dict[str, str]:
    """Get the config values from a config file."""
    config_values = {}
    with open(config_path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("CONFIG_"):
                key, value = line.split("=")
                config_values[key] = value
    return config_values


def cmd_erase_all_cached_values() -> str:
    """Erase all cached CONFIG_ values."""
    return """message(STATUS "Erasing all cached CONFIG_ values")
get_cmake_property(_cache_values CACHE_VARIABLES)
foreach(_cache_value IN LISTS _cache_values)
    if(_cache_value MATCHES "^CONFIG_.*")
        unset(${_cache_value} CACHE)
    endif()
endforeach()

"""


def infer_cache_type(value) -> str:
    """Infer the cache type from a value."""
    if value in ("y", "n", "m"):
        return "BOOL"
    return "STRING"


def write_sdkconfig_cmake(output_path, config_values):
    """Write a sdkconfig.cmake file with the config values."""
    with open(output_path, "w") as f:
        f.write("# Auto-generated sdkconfig.cmake\n")
        f.write("# DO NOT EDIT - Generated from component configs\n")
        f.write(cmd_erase_all_cached_values())
        for key, value in config_values.items():
            f.write(f"set({key} {value} CACHE {infer_cache_type(value)} INTERNAL)\n")


def main():
    if len(sys.argv) != 3:
        print("Usage: gen_sdkconfig_cmake.py <config_file> <output_file>")
        sys.exit(1)

    config_path = sys.argv[1]
    output_path = sys.argv[2]

    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    # Load the config file and get all symbols
    config_values = get_config_values(config_path)

    # Generate cmake file
    write_sdkconfig_cmake(output_path, config_values)
    print(f"Generated sdkconfig.cmake: {output_path}")


if __name__ == "__main__":
    main()
