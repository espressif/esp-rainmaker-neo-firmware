# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for the host_ctrl correlation id on the host side."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import pytest  # noqa: E402

from host_ctrl_python.commands import (  # noqa: E402
    Command,
    CommunicationProtocol,
    PortManagerSingle,
)


class PingCommand(Command):
    def build(self, protocol: CommunicationProtocol) -> str:
        return protocol.command_ping

    def log(self, protocol: CommunicationProtocol) -> None:
        pass


class FakeSerial:
    """Hands out queued response lines and records what was written."""

    def __init__(self, lines):
        self._lines = list(lines)
        self.writes = []
        self.timeout = None
        self.resets = 0

    def read_until(self, expected: bytes) -> bytes:
        return self._lines.pop(0).encode("ascii") if self._lines else b""

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        pass

    def reset_input_buffer(self) -> None:
        self.resets += 1


@pytest.fixture(scope="module")
def protocol():
    return CommunicationProtocol()


def _manager(protocol, lines):
    manager = PortManagerSingle("fake", 115200, protocol)
    manager.connection = FakeSerial(lines)
    return manager


def test_seq_format_and_split(protocol):
    assert protocol.seq_supported
    assert protocol.format_seq(0x00A1) == "#00A1"
    assert protocol.split_seq("#00A1oabc") == (0x00A1, "oabc")


@pytest.mark.parametrize("line", ["oabc", "#00A", "#ZZZZo", ""])
def test_split_seq_leaves_untagged_or_malformed_line(protocol, line):
    assert protocol.split_seq(line) == (None, line)


def test_untagged_reply_accepted_until_peer_tags(protocol):
    manager = _manager(protocol, [])
    response = manager._match_response("oabc", 1)
    assert response is not None and response.is_ok and response.payload == "abc"
    assert manager.desync_count == 0


def test_untagged_reply_dropped_once_peer_tags(protocol):
    manager = _manager(protocol, [])
    assert manager._match_response("#0001o", 1) is not None
    assert manager._match_response("o", 2) is None
    assert manager.desync_count == 1


def test_reply_to_other_command_dropped(protocol):
    manager = _manager(protocol, [])
    assert manager._match_response("#0001o", 2) is None
    assert manager.desync_count == 1


def test_send_command_skips_stale_reply(protocol):
    end = protocol.end_char
    manager = _manager(protocol, [f"#0002e{end}", f"#0003oabc{end}"])
    # Command 1 got a tagged reply; command 2 timed out, and its late reply is still queued.
    assert manager._match_response("#0001o", 1) is not None
    manager._seq = 2

    response = manager.send_command(PingCommand(), timeout_ms=500)

    assert manager.connection.writes == [
        f"#0003{protocol.command_ping}{end}".encode("ascii")
    ]
    assert response.is_ok and response.payload == "abc"
    assert manager.desync_count == 1
    # The firmware tags, so the input is no longer flushed before a command.
    assert manager.connection.resets == 0
