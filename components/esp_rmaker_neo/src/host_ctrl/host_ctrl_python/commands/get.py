# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandGetCurrentTime(Command):
    """
    Represents a command to get the current time from the node.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_current_time}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting current time")


class CommandGetCurrentTimeMs(Command):
    """
    Represents a command to get current Unix time in milliseconds from the node.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_current_time_ms}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting current time (ms)")


class CommandGetCurrentTimezone(Command):
    """
    Represents a command to get the current timezone from the node.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_current_timezone}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting current timezone")


class CommandGetThingName(Command):
    """
    Represents a command to get the thing name.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_thing_name}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting thing name")


class CommandGetSignature(Command):
    """
    Represents a command to get the signature for a challenge.
    """

    def __init__(self, challenge: str):
        self.challenge = challenge

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            self.challenge,
            "",
        ]
        return f"{protocol.command_get}{protocol.gettable_signature}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Getting signature for challenge {ColorFormatter.ok_cyan(self.challenge)} (length: {len(self.challenge)})"
        )


class CommandGetIndexedShadow(Command):
    """
    Represents a command to get the indexed shadow.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_indexed_shadow}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting indexed shadow")


class CommandGetNamedShadow(Command):
    """
    Represents a command to get the named shadow.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_named_shadow}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting named shadow")


class CommandGetParam(Command):
    """
    Represents a command to get a parameter (value, properties, etc.).
    """

    def __init__(self, device_id: str, param_id: str):
        self.device_id = device_id
        self.param_id = param_id

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_param}"

    def _payload(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            self.device_id,
            self.param_id,
            "",
        ]
        return d.join(parts)

    def build(self, protocol: CommunicationProtocol) -> str:
        return self._prefix(protocol) + self._payload(protocol)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Getting param {ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)}"
        )


class CommandGetTagValue(Command):
    """
    Represents a command to get the value of a tag.
    """

    def __init__(self, tag_name: str):
        self.tag_name = tag_name

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            self.tag_name,
            "",
        ]
        return f"{protocol.command_get}{protocol.gettable_tag_value}" + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Getting tag value of {ColorFormatter.ok_cyan(self.tag_name)}"
        )


class CommandGetGroupInfo(Command):
    """
    Represents a command to get the group info.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_group_info}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting group info")


class CommandGetAlexaEnabled(Command):
    """
    Represents a command to get the Alexa enabled status.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_alexa_enabled}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting Alexa enabled status")


class CommandGetGvaEnabled(Command):
    """
    Represents a command to get the GVA enabled status.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_gva_enabled}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting GVA enabled status")


class CommandGetSchedVersion(Command):
    """
    Represents a command to get the sched version.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_sched_version}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting sched version")


class CommandGetTriggerVersion(Command):
    """
    Represents a command to get the trigger version.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_trigger_version}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting trigger version")


class CommandGetHeapStatus(Command):
    """
    Represents a command to get the heap status.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_get}{protocol.gettable_heap_status}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print("-> Command: Getting heap status")
