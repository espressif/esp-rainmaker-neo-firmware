#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Generate default Kconfig configuration.

Usage: gen_default_config.py <kconfig_file> <output_config_file> [defaults_file1] [defaults_file2] ...

This script loads a Kconfig file and saves the default configuration
to the specified output file. Multiple defaults files can be provided,
and each subsequent file can override settings from previous files.
"""

import sys
import os


def main():
    if len(sys.argv) < 3:
        print(
            "Usage: gen_default_config.py <kconfig_file> <output_config_file> [defaults_file1] [defaults_file2] ..."
        )
        sys.exit(1)

    kconfig_path = sys.argv[1]
    output_path = sys.argv[2]
    defaults_paths = sys.argv[3:] if len(sys.argv) > 3 else []

    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    try:
        # Check if output file already exists and has content
        if os.path.exists(output_path) and os.path.getsize(output_path) > 0:
            print(
                f"Config file {output_path} already exists and has content, skipping default generation"
            )
            return

        from kconfiglib import Kconfig

        print(f"Loading Kconfig from: {kconfig_path}")
        kconf = Kconfig(kconfig_path)

        # Load defaults files if provided
        if defaults_paths:
            first_file = True
            for defaults_path in defaults_paths:
                if os.path.exists(defaults_path):
                    print(f"Loading defaults from: {defaults_path}")
                    # Use replace=False for subsequent files to merge instead of replace
                    kconf.load_config(defaults_path, replace=first_file)
                    first_file = False
                else:
                    print(f"Warning: defaults file not found: {defaults_path}")
        else:
            # No defaults files provided, use Kconfig defaults
            pass

        print(f"Saving final configuration to: {output_path}")

        # Save the final configuration (this will include all defaults or loaded config)
        kconf.write_config(output_path)

        print("Default configuration saved successfully")

    except Exception as e:
        print(f"Error generating default config: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
