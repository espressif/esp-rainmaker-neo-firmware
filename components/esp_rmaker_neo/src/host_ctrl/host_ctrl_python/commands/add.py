# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandAddParam(Command):
    """
    Represents a command to add a parameter.
    """

    def __init__(
        self,
        device_id: str,
        device_type: str,
        param_id: str,
        param_type: str,
        param_ui_type: str | None,
        value: bool | int | float | str | dict | list,
        properties: list[str],
        min_bound=None,
        max_bound=None,
        step=None,
    ):
        self.device_id = device_id
        self.device_type = device_type
        self.param_id = param_id
        self.param_type = param_type
        self.param_ui_type = param_ui_type
        self.value = value
        self.properties = properties
        self.min_bound = min_bound
        self.max_bound = max_bound
        self.step = step

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        """Wire prefix before the parts. Override in subclasses to retarget."""
        return f"{protocol.command_add}{protocol.payload_param}"

    def _payload(self, protocol: CommunicationProtocol) -> str:
        """The delimited parts that follow the prefix. Identical for self and bridge children."""
        d = protocol.delimiter_char or "|"

        # Check properties are valid
        prop_chars = protocol.parse_param_properties_long_names(self.properties)
        if len(prop_chars) != len(self.properties):
            raise ValueError(
                f"Invalid properties: {self.properties}. Some properties are not valid. Valid properties are: {protocol.property_param_mapping.values()}"
            )

        # Value with dtype prefix
        value_field = protocol.get_param_value_str(self.value)

        # Bounds fields: only applicable to int/float, but always include empty delimiters when not provided
        if type(self.value) is int:
            min_field = "" if self.min_bound is None else str(self.min_bound)
            max_field = "" if self.max_bound is None else str(self.max_bound)
            step_field = "" if self.step is None else str(self.step)
        elif type(self.value) is float:
            min_field = "" if self.min_bound is None else f"{float(self.min_bound):.3f}"
            max_field = "" if self.max_bound is None else f"{float(self.max_bound):.3f}"
            step_field = "" if self.step is None else f"{float(self.step):.3f}"
        else:
            min_field = ""
            max_field = ""
            step_field = ""

        parts = [
            self.device_id,
            self.device_type,
            self.param_id,
            self.param_type,
            self.param_ui_type or "",
            value_field,
            min_field,
            max_field,
            step_field,
            prop_chars,
            "",
        ]
        return d.join(parts)

    def build(self, protocol: CommunicationProtocol) -> str:
        return self._prefix(protocol) + self._payload(protocol)

    def log(self, protocol: CommunicationProtocol) -> None:
        log_str = f"-> Command: Setting param {ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)} to ({type(self.value).__name__}) {ColorFormatter.ok_cyan(protocol.format_value(self.value))} (properties: {ColorFormatter.ok_cyan(', '.join(self.properties))})"

        if self.param_ui_type is not None:
            log_str += f" (UI type: {ColorFormatter.ok_cyan(self.param_ui_type)})"
        if (
            self.min_bound is not None
            and self.max_bound is not None
            and self.step is not None
        ):
            log_str += f" (min: {protocol.format_value(self.min_bound)}, max: {protocol.format_value(self.max_bound)}, step: {protocol.format_value(self.step)})"
        print(log_str)


class CommandAddTag(Command):
    """
    Represents a command to add a tag.
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
        return f"{protocol.command_add}{protocol.payload_tag}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Setting tag {ColorFormatter.ok_cyan(self.tag_name)} = '{ColorFormatter.ok_cyan(protocol.format_value(self.tag_value))}'"
        )


class CommandAddServices(Command):
    """
    Represents a command to add standard services.
    """

    def __init__(self, services: list[str]):
        self.services = services

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            "".join(protocol.service_mapping[service] for service in self.services),
            "",
        ]
        return f"{protocol.command_add}{protocol.payload_services}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Adding standard services [ {ColorFormatter.ok_cyan(', '.join(self.services))} ]"
        )
