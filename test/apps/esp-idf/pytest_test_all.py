# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from pytest_embedded import Dut
from pytest_embedded_idf.utils import idf_parametrize

# Every SDK-supported target: all of them have Wi-Fi.
SUPPORTED_TARGETS = ["esp32", "esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32s3"]


@idf_parametrize("target", SUPPORTED_TARGETS, indirect=["target"])
def test_all(dut: Dut):
    dut.run_all_single_board_cases(reset=False, timeout=180)
