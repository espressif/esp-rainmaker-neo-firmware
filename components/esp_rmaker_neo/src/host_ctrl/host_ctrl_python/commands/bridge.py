# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Bridge-multiplexed host control commands.

Every command is dispatched on the firmware side via
``RMAKER_HOST_CTRL_COMMAND_CHAR_BRIDGE`` plus a one-character sub-command.
"""

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter
from .add import CommandAddParam
from .update import CommandUpdateParam
from .get import CommandGetParam
from .flags import CommandWaitFlags, CommandClearFlags


class _BridgeCommandBase(Command):
    """Shared helpers for bridge sub-commands."""

    sub_char_attr: str = ""

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        sub = getattr(protocol, self.sub_char_attr)
        return f"{protocol.command_bridge}{sub}"


class CommandBridgeAddChild(_BridgeCommandBase):
    """Request creation of a bridged child Thing."""

    sub_char_attr = "bridge_sub_add_child"

    def __init__(self, child_suffix: str, bridge_local_id: str, timeout_ms: int):
        self.child_suffix = child_suffix
        self.bridge_local_id = bridge_local_id
        self.timeout_ms = timeout_ms

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [self.child_suffix, self.bridge_local_id, str(self.timeout_ms), ""]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge add_child suffix={ColorFormatter.ok_cyan(self.child_suffix)} "
            f"local_id={ColorFormatter.ok_cyan(self.bridge_local_id)} timeout_ms={self.timeout_ms}"
        )


class CommandBridgeAddChildNoAck(_BridgeCommandBase):
    """Fire-and-forget add: returns OK as soon as the bridge SDK accepts
    the request. Caller must poll list_children to confirm READY."""

    sub_char_attr = "bridge_sub_add_child_no_ack"

    def __init__(self, child_suffix: str, bridge_local_id: str):
        self.child_suffix = child_suffix
        self.bridge_local_id = bridge_local_id

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [self.child_suffix, self.bridge_local_id, ""]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge add_child_no_ack suffix={ColorFormatter.ok_cyan(self.child_suffix)} "
            f"local_id={ColorFormatter.ok_cyan(self.bridge_local_id)}"
        )


class CommandBridgeRemoveChild(_BridgeCommandBase):
    """Request removal of a bridged child Thing."""

    sub_char_attr = "bridge_sub_remove_child"

    def __init__(self, handle: int, timeout_ms: int):
        self.handle = handle
        self.timeout_ms = timeout_ms

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [str(self.handle), str(self.timeout_ms), ""]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge remove_child handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"timeout_ms={self.timeout_ms}"
        )


class CommandBridgeMarkOnline(_BridgeCommandBase):
    """Mark a bridged child reachable / unreachable."""

    sub_char_attr = "bridge_sub_mark_online"

    def __init__(self, handle: int, online: bool):
        self.handle = handle
        self.online = online

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [str(self.handle), "1" if self.online else "0", ""]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge mark_online handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"online={self.online}"
        )


class _BridgeChildHandleCommand(_BridgeCommandBase):
    """Sub-command that takes only a child handle."""

    def __init__(self, handle: int):
        self.handle = handle

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        return self._prefix(protocol) + d.join([str(self.handle), ""])

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge {self.sub_char_attr} handle="
            f"{ColorFormatter.ok_cyan(str(self.handle))}"
        )


class CommandBridgeChildThingName(_BridgeChildHandleCommand):
    sub_char_attr = "bridge_sub_child_thing_name"


class CommandBridgeChildLocalId(_BridgeChildHandleCommand):
    sub_char_attr = "bridge_sub_child_local_id"


class CommandBridgeChildGroupInfo(_BridgeChildHandleCommand):
    sub_char_attr = "bridge_sub_child_group_info"


class CommandBridgeListChildren(_BridgeCommandBase):
    """Enumerate currently-known bridged children, one page at a time.

    ``start`` is the index of the first host-visible child to return;
    ``count`` caps the page size (firmware clamps to its own page max).
    The wrapper pages until a short read."""

    sub_char_attr = "bridge_sub_list_children"

    def __init__(self, start: int = 0, count: int = 32):
        self.start = start
        self.count = count

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [str(self.start), str(self.count), ""]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(f"-> Command: Bridge list_children start={self.start} count={self.count}")


class CommandBridgeCommitDevices(_BridgeChildHandleCommand):
    """Commit the child's attached devices and (re-)publish node config."""

    sub_char_attr = "bridge_sub_commit_devices"


class CommandBridgeChildFillInfo(_BridgeCommandBase):
    """Fill a bridged child node's info (name/type/fw_version/model)."""

    sub_char_attr = "bridge_sub_child_fill_info"

    def __init__(self, handle: int, name: str, type_: str, fw_version: str, model: str):
        self.handle = handle
        self.name = name
        self.type_ = type_
        self.fw_version = fw_version
        self.model = model

    def build(self, protocol: CommunicationProtocol) -> str:
        d = protocol.delimiter_char or "|"
        parts = [
            str(self.handle),
            self.name,
            self.type_,
            self.fw_version,
            self.model,
            "",
        ]
        return self._prefix(protocol) + d.join(parts)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge child_fill_info handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"name={ColorFormatter.ok_cyan(self.name)} type={ColorFormatter.ok_cyan(self.type_)} "
            f"fw_version={ColorFormatter.ok_cyan(self.fw_version)} model={ColorFormatter.ok_cyan(self.model)}"
        )


def _bridge_handle_prefix(
    protocol: CommunicationProtocol, sub_attr: str, handle: int
) -> str:
    """Wire prefix that retargets a self command at a bridged child:
    ``<command_bridge><sub_char><handle>|`` — the inner ``_payload``
    from the parent class then drops in unchanged."""
    d = protocol.delimiter_char or "|"
    return f"{protocol.command_bridge}{getattr(protocol, sub_attr)}{handle}{d}"


class CommandBridgeChildAddParam(CommandAddParam):
    """Add a param under a device attached to a bridged child.

    Reuses ``CommandAddParam`` verbatim; only the wire prefix is
    rewritten so the firmware routes the command through the bridge
    handler with the child handle attached.
    """

    sub_char_attr = "bridge_sub_child_add_param"

    def __init__(self, handle: int, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.handle = handle

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return _bridge_handle_prefix(protocol, self.sub_char_attr, self.handle)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge child_add_param handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"{ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)} = "
            f"({type(self.value).__name__}) {ColorFormatter.ok_cyan(protocol.format_value(self.value))} "
            f"(properties: {ColorFormatter.ok_cyan(', '.join(self.properties))})"
        )


class CommandBridgeChildUpdateParam(CommandUpdateParam):
    """Update a param under a device attached to a bridged child."""

    sub_char_attr = "bridge_sub_child_update_param"

    def __init__(self, handle: int, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.handle = handle

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return _bridge_handle_prefix(protocol, self.sub_char_attr, self.handle)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge child_update_param handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"{ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)} = "
            f"({type(self.value).__name__}) {ColorFormatter.ok_cyan(protocol.format_value(self.value))}"
        )


class CommandBridgeChildGetParam(CommandGetParam):
    """Read a param value/properties for a bridged child's device."""

    sub_char_attr = "bridge_sub_child_get_param"

    def __init__(self, handle: int, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.handle = handle

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return _bridge_handle_prefix(protocol, self.sub_char_attr, self.handle)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge child_get_param handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"{ColorFormatter.ok_cyan(self.device_id + '::' + self.param_id)}"
        )


class CommandBridgeChildWaitFlags(CommandWaitFlags):
    """Wait on the per-child flag bitmap."""

    sub_char_attr = "bridge_sub_child_wait_flags"

    def __init__(self, handle: int, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.handle = handle

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return _bridge_handle_prefix(protocol, self.sub_char_attr, self.handle)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge wait_flags handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"timeout={ColorFormatter.ok_blue(str(self.timeout_ms))} ms flags=[ "
            f"{ColorFormatter.ok_cyan(', '.join([protocol.get_flag_long_name(f) for f in self.flags]))} ]"
        )


class CommandBridgeChildClearFlags(CommandClearFlags):
    """Clear bits in the per-child flag bitmap."""

    sub_char_attr = "bridge_sub_child_clear_flags"

    def __init__(self, handle: int, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.handle = handle

    def _prefix(self, protocol: CommunicationProtocol) -> str:
        return _bridge_handle_prefix(protocol, self.sub_char_attr, self.handle)

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Bridge clear_flags handle={ColorFormatter.ok_cyan(str(self.handle))} "
            f"flags=[ {ColorFormatter.ok_cyan(', '.join([protocol.get_flag_long_name(f) for f in self.flags]))} ]"
        )
