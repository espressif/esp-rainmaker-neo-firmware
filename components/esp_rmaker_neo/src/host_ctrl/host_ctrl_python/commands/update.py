# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandUpdateParam(Command):
    """
    Represents a command to update a parameter.
    """

    def __init__(
        self,
        device_id: str,
        param_id: str,
        value: bool | int | float | str | dict | list,
    ):
        self.device_id = device_id
        self.param_id = param_id
        self.value = value

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_update}{protocol.payload_param}"

    def _payload(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        value_field = protocol.get_param_value_str(self.value)
        parts = [
            self.device_id,
            self.param_id,
            value_field,
            "",
        ]
        return d.join(parts)

    def build(self, protocol: CommunicationProtocol) -> str:
        return self._prefix(protocol) + self._payload(protocol)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Updating param {ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)} to ({type(self.value).__name__}) {ColorFormatter.ok_cyan(protocol.format_value(self.value))}"
        )


class CommandUpdateTag(Command):
    """
    Represents a command to update a tag.
    """

    def __init__(self, tag_name: str, tag_value: str):
        self.tag_name = tag_name
        self.tag_value = tag_value

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            self.tag_name,
            self.tag_value,
            "",
        ]
        return f"{protocol.command_update}{protocol.payload_tag}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Updating tag {ColorFormatter.ok_cyan(self.tag_name)} = '{ColorFormatter.ok_cyan(protocol.format_value(self.tag_value))}'"
        )


class CommandUpdateTimezone(Command):
    """
    Represents a command to update the timezone.
    """

    def __init__(self, timezone: str):
        self.timezone = timezone

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            self.timezone,
            "",
        ]
        return f"{protocol.command_update}{protocol.payload_timezone}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Updating timezone to {ColorFormatter.ok_cyan(self.timezone)}"
        )


class CommandUpdateLocalConfig(Command):
    """
    Represents a command to update the local configuration.
    """

    def __init__(self, key: str, value: bool | int | float | str | dict | list):
        self.key = key
        self.value = value

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        value_field = protocol.get_param_value_str(self.value)
        parts = [
            value_field,
            "",
        ]
        return (
            f"{protocol.command_update}{protocol.payload_local_config}{self.key}"
            + d.join(parts)
        )

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Updating local config {ColorFormatter.ok_cyan(protocol.get_local_config_key_long_name(self.key))} to ({type(self.value).__name__}) {ColorFormatter.ok_cyan(protocol.format_value(self.value))}"
        )
