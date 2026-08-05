#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
from pathlib import Path

from esptool.cmds import attach_flash, detect_chip, reset_chip, write_flash

_COMMON_ROOT = Path(__file__).resolve().parents[1] / "common"
if str(_COMMON_ROOT) not in sys.path:
    sys.path.insert(0, str(_COMMON_ROOT))

from util.esp_helpers import (  # noqa: E402
    ESP_THREAD_ACTIVE_DATASET,
    ESP_WIFI_PASSWORD,
    ESP_WIFI_SSID,
    NetworkCredentials,
    NetworkCredentialsThread,
    NetworkCredentialsWifi,
)


def _parse_int(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"Invalid integer value: {value}") from exc


def _build_credentials(args: argparse.Namespace) -> NetworkCredentials:
    wifi_ssid = args.wifi_ssid or ESP_WIFI_SSID
    wifi_password = args.wifi_password or ESP_WIFI_PASSWORD
    thread_dataset = args.thread_active_dataset or ESP_THREAD_ACTIVE_DATASET

    if args.network_type == "wifi":
        if not wifi_ssid or not wifi_password:
            raise ValueError("Wi-Fi provisioning requires both SSID and password")
        return NetworkCredentialsWifi(wifi_ssid, wifi_password)

    if args.network_type == "thread":
        if not thread_dataset:
            raise ValueError("Thread provisioning requires an active dataset")
        return NetworkCredentialsThread(thread_dataset)

    # auto mode: prefer Wi-Fi when both are available
    if wifi_ssid and wifi_password:
        print("Using Wi-Fi credentials")
        return NetworkCredentialsWifi(wifi_ssid, wifi_password)
    if thread_dataset:
        print("Using Thread active dataset")
        return NetworkCredentialsThread(thread_dataset)

    raise ValueError(
        "No network credentials found. Set Wi-Fi or Thread env vars in .env, "
        "or pass --wifi-ssid/--wifi-password or --thread-active-dataset."
    )


def _flash_nvs_partition(port: str, baud: int, offset: int, nvs_bin_path: Path) -> None:
    with detect_chip(port=port, connect_attempts=3) as esp:
        esp.change_baud(baud)
        attach_flash(esp)
        with open(nvs_bin_path, "rb") as bin_file:
            write_flash(esp, [(offset, bin_file)], compress=True)
        reset_chip(esp, reset_mode="hard-reset")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate network provisioning NVS and flash it to an ESP chip."
    )
    parser.add_argument(
        "--port",
        required=True,
        help="Serial port of the target chip (example: /dev/cu.usbserial-XXXX).",
    )
    parser.add_argument(
        "--offset",
        required=True,
        type=_parse_int,
        help="NVS partition offset (hex or decimal, example: 0x9000).",
    )
    parser.add_argument(
        "--size",
        required=True,
        type=_parse_int,
        help="NVS partition size in bytes (hex or decimal, example: 0x6000).",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=460800,
        help="Baud rate for flashing (default: 460800).",
    )
    parser.add_argument(
        "--network-type",
        choices=["auto", "wifi", "thread"],
        default="auto",
        help="Network credentials source. auto prefers Wi-Fi when both are present.",
    )
    parser.add_argument("--wifi-ssid", help="Wi-Fi SSID override.")
    parser.add_argument("--wifi-password", help="Wi-Fi password override.")
    parser.add_argument(
        "--thread-active-dataset", help="Thread active dataset override."
    )
    args = parser.parse_args()

    credentials = None
    try:
        credentials = _build_credentials(args)
        nvs_bin = credentials.get_nvs_binary_path(args.size)
        if nvs_bin is None:
            raise RuntimeError("Failed to generate NVS binary")

        print(
            f"Flashing {credentials.network_type} network NVS to port={args.port}, "
            f"offset=0x{args.offset:x}, size=0x{args.size:x}"
        )
        _flash_nvs_partition(args.port, args.baud, args.offset, Path(nvs_bin))
        print("Network provisioning NVS flashed successfully")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if credentials is not None:
            credentials.destroy()


if __name__ == "__main__":
    raise SystemExit(main())
