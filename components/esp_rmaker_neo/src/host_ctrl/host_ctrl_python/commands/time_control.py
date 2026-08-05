# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter
from datetime import datetime as dt, timedelta as td


class CommandTimeControlSetTime(Command):
    """
    Represents a command to set the time.
    """

    def __init__(self, datetime: dt):
        self.time = int(datetime.timestamp())

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_time_control}{protocol.time_control_set_time}{self.time}{protocol.delimiter_char}"

    def log(self, protocol: CommunicationProtocol) -> None:
        time_str = dt.fromtimestamp(self.time).strftime("%Y-%m-%d %H:%M:%S %z")
        print(f"-> Command: Setting time to {ColorFormatter.ok_cyan(time_str)}")


class CommandTimeControlAdvanceTime(Command):
    """
    Represents a command to advance the time.
    """

    def __init__(self, delta: td):
        self.delta = delta

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_time_control}{protocol.time_control_advance_time}{self.delta.total_seconds()}{protocol.delimiter_char}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Advancing time by {ColorFormatter.ok_cyan(str(self.delta))}"
        )
