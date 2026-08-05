# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandWaitFlags(Command):
    """
    Represents a command to wait for event flags.
    """

    def __init__(self, flags: list[str], timeout_ms: int):
        self.flags = flags
        self.timeout_ms = timeout_ms

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_wait_flags}"

    def _payload(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            "".join(self.flags),
            str(self.timeout_ms),
            "",
        ]
        return d.join(parts)

    def build(self, protocol: CommunicationProtocol) -> str:
        return self._prefix(protocol) + self._payload(protocol)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: (timeout: {ColorFormatter.ok_blue(str(self.timeout_ms))} ms) Waiting for flags [ {ColorFormatter.ok_cyan(', '.join([protocol.get_flag_long_name(f) for f in self.flags]))} ]"
        )


class CommandClearFlags(Command):
    """
    Represents a command to clear event flags.
    """

    def __init__(self, flags: list[str]):
        self.flags = flags

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_clear_flags}"

    def _payload(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            "".join(self.flags),
            "",
        ]
        return d.join(parts)

    def build(self, protocol: CommunicationProtocol) -> str:
        return self._prefix(protocol) + self._payload(protocol)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Clearing flags [ {ColorFormatter.ok_cyan(', '.join([protocol.get_flag_long_name(f) for f in self.flags]))} ]"
        )
