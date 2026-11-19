# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for the host_ctrl int64 value codec on the host side."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import pytest  # noqa: E402

from host_ctrl_python.commands import CommunicationProtocol, Int64  # noqa: E402
from host_ctrl_python.commands.add import CommandAddParam  # noqa: E402


@pytest.fixture(scope="module")
def protocol():
    return CommunicationProtocol()


def test_int64_type_char(protocol):
    assert protocol.data_type_int64 == "l"
    assert protocol.get_param_value_str(Int64(5000000000)) == "l5000000000"
    assert protocol.get_param_value_str(5) == "i5"


def test_int64_parse(protocol):
    value = protocol.parse_param_value("l-5000000000")
    assert type(value) is Int64
    assert value == -5000000000


def test_int64_add_param_bounds(protocol):
    cmd = CommandAddParam(
        "d", "t", "p", "pt", None, Int64(5), ["read"], 0, 6000000000, 1
    )
    assert "|l5|0|6000000000|1|" in cmd._payload(protocol)
