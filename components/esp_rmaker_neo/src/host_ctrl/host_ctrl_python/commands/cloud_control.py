# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Cloud control commands.
"""

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandCloudControlSend(Command):
    """
    Represents a command to send cloud events.
    """

    def __init__(self, events: list[str]):
        self.events = events

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            "".join(self.events),
            "",
        ]
        return (
            f"{protocol.command_cloud_control}{protocol.cloud_control_send}"
            + d.join(parts)
        )

    def log(self, protocol: CommunicationProtocol) -> None:
        event_names = [
            protocol.get_cloud_control_event_long_name(event) for event in self.events
        ]
        print(
            f"-> Command: Sending cloud events: {ColorFormatter.ok_cyan(', '.join(event_names))}"
        )
