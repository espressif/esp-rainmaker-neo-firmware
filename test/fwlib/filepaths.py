# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

# Repo root = test/fwlib/../..
ROOT_DIR = Path(__file__).resolve().parents[2]
RMNG_SDK_DIR = ROOT_DIR

# Gitignore file
RMNG_SDK_GITIGNORE_FILE = RMNG_SDK_DIR / ".gitignore"

# Module paths
RMNG_SDK_HOST_CTRL_MODULE_DIR = (
    RMNG_SDK_DIR / "components" / "esp_rmaker_neo" / "src" / "host_ctrl"
)
RMNG_SDK_OSAL_OTA_MODULE_DIR = RMNG_SDK_DIR / "components" / "osal" / "ota"

# Device simulator paths (unified ESP-IDF + POSIX example under the test tree)
RMNG_SDK_DEVICE_SIM_DIR = RMNG_SDK_DIR / "test" / "sims" / "device-sim"
RMNG_SDK_DEVICE_SIM_POSIX_DIR = RMNG_SDK_DEVICE_SIM_DIR
RMNG_SDK_DEVICE_SIM_ESP_DIR = RMNG_SDK_DEVICE_SIM_DIR
RMNG_SDK_DEVICE_SIM_ESP_BINARY_NAME = "device-sim.bin"

# OTA simulator paths (unified ESP-IDF + POSIX example under the test tree)
RMNG_SDK_OTA_SIM_DIR = RMNG_SDK_DIR / "test" / "sims" / "ota-sim"
RMNG_SDK_OTA_SIM_POSIX_DIR = RMNG_SDK_OTA_SIM_DIR
RMNG_SDK_OTA_SIM_ESP_DIR = RMNG_SDK_OTA_SIM_DIR
RMNG_SDK_OTA_SIM_ESP_BINARY_NAME = "ota-sim.bin"
