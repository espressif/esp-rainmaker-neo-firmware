# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from random import choice
from string import ascii_letters, digits


class CommandPing(Command):
    def __init__(self, random_length: int):
        self.random_str = "".join(
            choice(ascii_letters + digits) for _ in range(random_length)
        )

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_ping}{self.random_str}{protocol.delimiter_char}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Pinging ESP RainMaker host control with random '{self.random_str}'"
        )


class CommandStart(Command):
    """
    Represents a command to start the flow.
    """

    def __init__(self, timeout_ms: int):
        self.timeout_ms = timeout_ms

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_start}{self.timeout_ms}{protocol.delimiter_char}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(f"-> Command: Starting ESP RainMaker with timeout {self.timeout_ms}ms")


class CommandStop(Command):
    """
    Represents a command to stop the flow.
    """

    def __init__(self, timeout_ms: int):
        self.timeout_ms = timeout_ms

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_stop}{self.timeout_ms}{protocol.delimiter_char}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(f"-> Command: Stopping ESP RainMaker with timeout {self.timeout_ms}ms")


class CommandReset(Command):
    """
    Represents a command to reset the flow.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_reset}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Resetting ESP RainMaker")


class CommandResetKeepNvs(Command):
    """
    Full node deinit + reinit, NVS preserved. Simulates a cold reboot.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_reset_keep_nvs}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Resetting ESP RainMaker (keep NVS)")


class CommandKill(Command):
    """
    Represents a command to kill the flow.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_kill}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Killing ESP RainMaker permanently")
