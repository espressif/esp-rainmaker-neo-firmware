# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from pytest_embedded import Dut


def test_all(dut: Dut):
    dut.run_all_single_board_cases(reset=False, timeout=180)
