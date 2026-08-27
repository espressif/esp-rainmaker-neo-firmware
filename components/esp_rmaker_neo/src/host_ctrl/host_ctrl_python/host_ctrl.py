# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import json
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, rsa, padding
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import utils as crypto_utils
from .data import ParamData, HeapStatusData
from .globals import Globals
from .commands import PortManager, Command, Response
from .commands.flow import (
    CommandPing,
    CommandStart,
    CommandStop,
    CommandReset,
    CommandResetKeepNvs,
    CommandKill,
)
from .commands.add import CommandAddParam, CommandAddTag, CommandAddServices
from .commands.update import (
    CommandUpdateParam,
    CommandUpdateTag,
    CommandUpdateTimezone,
    CommandUpdateLocalConfig,
)
from .commands.flags import CommandWaitFlags, CommandClearFlags
from .commands.get import (
    CommandGetCurrentTime,
    CommandGetCurrentTimeMs,
    CommandGetCurrentTimezone,
    CommandGetThingName,
    CommandGetSignature,
    CommandGetIndexedShadow,
    CommandGetNamedShadow,
    CommandGetParam,
    CommandGetTagValue,
    CommandGetGroupInfo,
    CommandGetAlexaEnabled,
    CommandGetGvaEnabled,
    CommandGetStEnabled,
    CommandGetSchedVersion,
    CommandGetTriggerVersion,
    CommandGetHeapStatus,
)
from .commands.time_control import (
    CommandTimeControlSetTime,
    CommandTimeControlAdvanceTime,
)
from .commands.mqtt_control import (
    CommandMqttControlForceNetworkFailure,
    CommandMqttControlRestoreNetworkDefault,
    CommandMqttControlForceOperationsFailure,
    CommandMqttControlRestoreOperationsDefault,
    CommandMqttControlDisconnect,
    CommandMqttControlConnect,
)
from .commands.cloud_control import CommandCloudControlSend
from .commands.bridge import (
    CommandBridgeAddChild,
    CommandBridgeAddChildNoAck,
    CommandBridgeRemoveChild,
    CommandBridgeMarkOnline,
    CommandBridgeChildThingName,
    CommandBridgeChildLocalId,
    CommandBridgeChildGroupInfo,
    CommandBridgeListChildren,
    CommandBridgeCommitDevices,
    CommandBridgeChildFillInfo,
    CommandBridgeChildAddParam,
    CommandBridgeChildUpdateParam,
    CommandBridgeChildGetParam,
    CommandBridgeChildWaitFlags,
    CommandBridgeChildClearFlags,
)
from datetime import datetime as dt, timedelta as td
from zoneinfo import ZoneInfo

DEFAULT_TIMEOUT_MS = 3000
DEFAULT_START_TIMEOUT_MS = 120000  # Time synchronization is in >= 15s intervals, may take a significant time to synchronize
DEFAULT_STOP_TIMEOUT_MS = 30000  # Stop is fast, so we can use a shorter timeout


class NodeConfig:
    """
    Represents a configuration for a node.
    """

    def __init__(self, json_config: dict):
        self.json_config = self._validate_config(json_config)

    @classmethod
    def from_json_path(cls, json_path: str) -> "NodeConfig":
        with open(json_path, "r") as f:
            return cls(json.load(f))

    def _validate_config(self, config: dict) -> dict:
        """
        Validate the configuration and normalize it.
        """
        protocol = Globals.protocol
        devices = config.get("devices", [])
        services = config.get("services", [])
        tags = config.get("tags", {})

        if not isinstance(devices, list):
            raise ValueError("'devices' must be an array")
        if not isinstance(tags, dict):
            raise ValueError("'tags' must be an object")

        norm = {"devices": [], "services": [], "tags": {}}

        for idx, dev in enumerate(devices):
            if not isinstance(dev, dict):
                raise ValueError(f"devices[{idx}] must be an object")
            dev_id = dev.get("id")
            dev_type = dev.get("type")
            params = dev.get("params", [])
            if not isinstance(dev_id, str) or dev_id == "":
                raise ValueError(f"devices[{idx}].id must be a non-empty string")
            if not isinstance(dev_type, str) or dev_type == "":
                raise ValueError(f"devices[{idx}].type must be a non-empty string")
            if not isinstance(params, list):
                raise ValueError(f"devices[{idx}].params must be an array")

            norm_params = []
            for pidx, param in enumerate(params):
                if not isinstance(param, dict):
                    raise ValueError(f"devices[{idx}].params[{pidx}] must be an object")
                pid = param.get("id")
                ptype = param.get("type")
                pui_type = param.get("ui_type")
                dtype = param.get("data_type")
                pvalue = param.get("value")
                props = param.get("properties", [])
                bounds = param.get("bounds", {})
                if bounds is not None and not isinstance(bounds, dict):
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].bounds must be an object if provided"
                    )
                pmin = bounds.get("min") if bounds is not None else None
                pmax = bounds.get("max") if bounds is not None else None
                pstep = bounds.get("step") if bounds is not None else None

                if not isinstance(pid, str) or pid == "":
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].id must be a non-empty string"
                    )
                if not isinstance(ptype, str) or ptype == "":
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].type must be a non-empty string"
                    )
                if dtype not in ("int", "float", "bool", "string", "object", "array"):
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].data_type must be one of int|float|bool|string|object|array"
                    )
                if not isinstance(props, list):
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].properties must be an array if provided"
                    )

                possible_props = protocol.property_param_mapping.values()
                for prop in props:
                    if prop not in possible_props:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].properties must be an array of {'|'.join(possible_props)}"
                        )

                # If bounds key is present, enforce completeness and dtype constraints
                bounds_key_present = "bounds" in param
                if bounds_key_present and dtype not in ("int", "float"):
                    raise ValueError(
                        f"devices[{idx}].params[{pidx}].bounds is only allowed for int/float data types"
                    )
                if bounds_key_present:
                    missing = [k for k in ("min", "max", "step") if k not in bounds]
                    if missing:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].bounds is missing required fields: {', '.join(missing)}"
                        )

                if dtype == "int":
                    if not isinstance(pvalue, int) or isinstance(pvalue, bool):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be an integer for data_type=int"
                        )
                    # Validate optional bounds
                    if pmin is not None and (
                        not isinstance(pmin, int) or isinstance(pmin, bool)
                    ):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].min must be an integer"
                        )
                    if pmax is not None and (
                        not isinstance(pmax, int) or isinstance(pmax, bool)
                    ):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].max must be an integer"
                        )
                    if pstep is not None and (
                        not isinstance(pstep, int)
                        or isinstance(pstep, bool)
                        or pstep <= 0
                    ):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].step must be a positive integer"
                        )
                    if pmin is not None and pmax is not None and pmin > pmax:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].min cannot be greater than max"
                        )
                    if pmin is not None and pvalue < pmin:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be >= min"
                        )
                    if pmax is not None and pvalue > pmax:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be <= max"
                        )
                elif dtype == "float":
                    if not isinstance(pvalue, (int, float)) or isinstance(pvalue, bool):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be a number for data_type=float"
                        )
                    pvalue = float(pvalue)
                    # Validate optional bounds
                    if pmin is not None:
                        if not isinstance(pmin, (int, float)) or isinstance(pmin, bool):
                            raise ValueError(
                                f"devices[{idx}].params[{pidx}].min must be a number"
                            )
                        pmin = float(pmin)
                    if pmax is not None:
                        if not isinstance(pmax, (int, float)) or isinstance(pmax, bool):
                            raise ValueError(
                                f"devices[{idx}].params[{pidx}].max must be a number"
                            )
                        pmax = float(pmax)
                    if pstep is not None:
                        if (
                            not isinstance(pstep, (int, float))
                            or isinstance(pstep, bool)
                            or float(pstep) <= 0
                        ):
                            raise ValueError(
                                f"devices[{idx}].params[{pidx}].step must be a positive number"
                            )
                        pstep = float(pstep)
                    if pmin is not None and pmax is not None and pmin > pmax:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].min cannot be greater than max"
                        )
                    if pmin is not None and pvalue < pmin:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be >= min"
                        )
                    if pmax is not None and pvalue > pmax:
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be <= max"
                        )
                elif dtype == "bool":
                    if not isinstance(pvalue, bool):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be a boolean for data_type=bool"
                        )
                elif dtype == "string":
                    if not isinstance(pvalue, str):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be a string for data_type=string"
                        )
                elif dtype == "object":
                    if not isinstance(pvalue, dict):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be an object for data_type=object"
                        )
                elif dtype == "array":
                    if not isinstance(pvalue, list):
                        raise ValueError(
                            f"devices[{idx}].params[{pidx}].value must be an array for data_type=array"
                        )

                norm_param = {
                    "id": pid,
                    "type": ptype,
                    "data_type": dtype,
                    "value": pvalue,
                    "properties": props,
                }
                # Keep bounds under a nested object for int/float
                if dtype in ("int", "float"):
                    norm_param["bounds"] = {"min": pmin, "max": pmax, "step": pstep}

                # Add UI type if available
                if pui_type is not None:
                    norm_param["ui_type"] = pui_type

                norm_params.append(norm_param)

            norm["devices"].append(
                {
                    "id": dev_id,
                    "type": dev_type,
                    "params": norm_params,
                }
            )

        possible_services = protocol.service_mapping.keys()
        for idx, service in enumerate(services):
            if not isinstance(service, str):
                raise ValueError(f"services[{idx}] must be a string")
            if service == "":
                raise ValueError(f"services[{idx}] must be a non-empty string")
            if service not in possible_services:
                raise ValueError(f"services[{idx}] must be one of: {possible_services}")
            norm["services"].append(service)

        for tname, tvalue in tags.items():
            if not isinstance(tvalue, str):
                raise ValueError(f"tags[{tname}] must be a string")
            if tname == "":
                raise ValueError(f"tags[{tname}] must be a non-empty string")
            norm["tags"][tname] = tvalue

        return norm

    def get_set_config_commands(self) -> list[CommandAddParam | CommandAddTag]:
        """
        Get the commands to set the configuration.
        """
        commands = []
        for device in self.json_config.get("devices", []):
            for param in device.get("params", []):
                _bounds = (
                    param.get("bounds", {})
                    if isinstance(param.get("bounds", {}), dict)
                    else {}
                )
                commands.append(
                    CommandAddParam(
                        device["id"],
                        device["type"],
                        param["id"],
                        param["type"],
                        param.get("ui_type"),
                        param["value"],
                        param["properties"],
                        _bounds.get("min"),
                        _bounds.get("max"),
                        _bounds.get("step"),
                    )
                )
        services = self.json_config.get("services", [])
        if services:
            commands.append(CommandAddServices(services))
        for tname, tvalue in self.json_config.get("tags", {}).items():
            commands.append(CommandAddTag(tname, tvalue))
        return commands


class NodeHostCtrl:
    """
    Represents a host control for a node.
    """

    def __init__(
        self,
        port_manager: PortManager,
        should_log: bool = True,
        public_key_pem: str | None = None,
    ):
        self.protocol = Globals.protocol
        self.port_manager = port_manager
        self.should_log = should_log
        self.public_key = None
        if public_key_pem is not None:
            self.public_key = serialization.load_pem_public_key(public_key_pem.encode())
        self.port_manager.connect()

        # Get the node thing name
        self.node_thing_name = self.get_thing_name()

        # Bridge sub-API (no-op on non-bridge firmware until first call).
        self.bridge = BridgeHostCtrl(self)

    def get_target_type(self) -> str | None:
        """
        Get the target type.
        """
        if self.target_type is None:
            self.target_type = self.ping()
        return self.target_type

    def quit(self) -> None:
        """
        Quit the host control.
        """
        self.port_manager.disconnect()

    def _send_cmd(self, cmd: Command, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> Response:
        """
        Send a command and get the response.
        """
        if self.should_log:
            cmd.log(self.protocol)
        resp = self.port_manager.send_command(cmd, timeout_ms=timeout_ms)
        if self.should_log:
            resp.log()
        return resp

    def _send_cmd_for_ok(
        self, cmd: Command, timeout_ms: int = DEFAULT_TIMEOUT_MS
    ) -> bool:
        """
        Send a command and get the response, checking for success.
        """
        return self._send_cmd(cmd, timeout_ms).is_ok

    def _send_cmd_for_payload(
        self, cmd: Command, timeout_ms: int = DEFAULT_TIMEOUT_MS
    ) -> str | None:
        """
        Send a command and get the response, returning the payload.
        """
        return self._send_cmd(cmd, timeout_ms).payload

    def ping(self) -> str | None:
        """
        Ping the host-controlled node.
        If the port is a valid host-controlled node, it will return its target type.
        """
        if self.protocol.command_ping_length <= 0:
            raise RuntimeError("Command ping length is not set")
        cmd = CommandPing(self.protocol.command_ping_length)
        ret = self._send_cmd_for_payload(cmd)
        if ret is None:
            return None
        ret_split = ret.split(self.protocol.delimiter_char)
        if len(ret_split) != 2:
            # invalid response
            return None
        if ret_split[0] != cmd.random_str:
            # random mismatch
            return None
        return ret_split[1]

    def set_config(self, config: NodeConfig) -> bool:
        """
        Set the configuration.
        """
        # Set config
        ret = True
        for command in config.get_set_config_commands():
            if not self._send_cmd_for_ok(command):
                ret = False

        return ret

    def start(self, timeout_ms: int = DEFAULT_START_TIMEOUT_MS) -> bool:
        """
        Start the flow.
        """
        cmd = CommandStart(timeout_ms)
        return self._send_cmd_for_ok(
            cmd, timeout_ms=timeout_ms * 2
        )  # delegate timeout responsibility to the node by doubling the Python timeout

    def stop(self, timeout_ms: int = DEFAULT_STOP_TIMEOUT_MS) -> bool:
        """
        Stop the flow.
        """
        cmd = CommandStop(timeout_ms)
        return self._send_cmd_for_ok(
            cmd, timeout_ms=timeout_ms * 2
        )  # delegate timeout responsibility to the node by doubling the Python timeout

    def reset(self, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> bool:
        """
        Reset the flow.
        """
        cmd = CommandReset()
        return self._send_cmd_for_ok(cmd, timeout_ms=timeout_ms)

    def reset_keep_nvs(self, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> bool:
        """
        Full node deinit + reinit on the firmware side, NVS preserved.
        Simulates a cold reboot — clears all RAM state (including the
        bridge child pool) while leaving persisted schedules / triggers /
        bridge child records intact in NVS.
        """
        cmd = CommandResetKeepNvs()
        return self._send_cmd_for_ok(cmd, timeout_ms=timeout_ms)

    def kill(self) -> bool:
        """
        Kill the flow.
        """
        cmd = CommandKill()
        return self._send_cmd_for_ok(cmd)

    def sign_challenge(self, challenge: str) -> str | None:
        """
        Sign a challenge.
        """
        cmd = CommandGetSignature(challenge)
        return self._send_cmd_for_payload(
            cmd, timeout_ms=5000
        )  # Slower chips might take a while to sign

    def verify_signature(self, challenge: str, signature: str | bytes) -> bool:
        """
        Verify a challenge signature with the configured public key.
        Signature may be provided as hex string or bytes.
        """
        if self.public_key is None:
            raise ValueError("Public key is not set for this host-controlled node")

        if isinstance(signature, str):
            signature_bytes = bytes.fromhex(signature)
        else:
            signature_bytes = signature

        challenge_bytes = challenge.encode()

        try:
            if isinstance(self.public_key, rsa.RSAPublicKey):
                self.public_key.verify(
                    signature_bytes,
                    challenge_bytes,
                    padding.PSS(
                        mgf=padding.MGF1(hashes.SHA256()),
                        salt_length=padding.PSS.MAX_LENGTH,
                    ),
                    hashes.SHA256(),
                )
            elif isinstance(self.public_key, ec.EllipticCurvePublicKey):
                # Ensure signature is normalized DER for ECDSA verification.
                r, s = crypto_utils.decode_dss_signature(signature_bytes)
                der_signature = crypto_utils.encode_dss_signature(r, s)
                self.public_key.verify(
                    der_signature,
                    challenge_bytes,
                    ec.ECDSA(hashes.SHA256()),
                )
            else:
                raise ValueError("Unsupported public key type")
        except Exception:
            return False

        return True

    def update_param(
        self,
        device_id: str,
        param_id: str,
        value: bool | int | float | str | dict | list,
    ) -> bool:
        """
        Update a parameter.
        """
        cmd = CommandUpdateParam(device_id, param_id, value)
        return self._send_cmd_for_ok(cmd)

    def update_tag(self, tag_name: str, value: str) -> bool:
        """
        Update a tag.
        """
        cmd = CommandUpdateTag(tag_name, value)
        return self._send_cmd_for_ok(cmd)

    def update_timezone(self, timezone: str) -> bool:
        """
        Update the POSIX timezone.
        """
        cmd = CommandUpdateTimezone(timezone)
        return self._send_cmd_for_ok(cmd)

    def update_sched_version(self, version: int) -> bool:
        """
        Update the schedule version.
        """
        cmd = CommandUpdateLocalConfig(
            self.protocol.local_config_key_sched_ver, version
        )
        return self._send_cmd_for_ok(cmd)

    def update_trigger_version(self, version: int) -> bool:
        """
        Update the trigger version.
        """
        cmd = CommandUpdateLocalConfig(
            self.protocol.local_config_key_trigger_ver, version
        )
        return self._send_cmd_for_ok(cmd)

    def set_local_ctrl_http_port(self, port: int) -> bool:
        """
        Set the local control HTTP server port on the node before the local_ctrl service is enabled.
        """
        cmd = CommandUpdateLocalConfig(
            self.protocol.local_config_key_local_ctrl_http_port, port
        )
        return self._send_cmd_for_ok(cmd)

    def set_local_ctrl_pop(self, pop: str) -> bool:
        """
        Set the local control PoP on the node before the local endpoints service is enabled.

        A real device carries a PoP from manufacturing data that the client already has;
        this is the test-side equivalent, so a session can be established against the
        PoP-backed security schemes instead of the PoP the node would generate itself.
        """
        cmd = CommandUpdateLocalConfig(
            self.protocol.local_config_key_local_ctrl_pop, pop
        )
        return self._send_cmd_for_ok(cmd)

    def _wait(self, flags: list[str], timeout_ms: int) -> bool:
        """
        Wait for certain flags to be set.
        """
        cmd = CommandWaitFlags(flags, timeout_ms)
        return self._send_cmd_for_ok(
            cmd, timeout_ms=timeout_ms * 2
        )  # delegate timeout responsibility to the node by doubling the Python timeout

    def _clear(self, flags: list[str]) -> bool:
        """
        Clear certain flags.
        """
        cmd = CommandClearFlags(flags)
        return self._send_cmd_for_ok(cmd)

    def wait_on_online(self, timeout_ms: int) -> bool:
        """
        Wait for the node to be online.
        """
        return self._wait([self.protocol.flag_online], timeout_ms)

    def wait_on_alexa_enabled(self, timeout_ms: int) -> bool:
        """
        Wait for the node to be Alexa enabled.
        """
        return self._wait([self.protocol.flag_alexa_enabled], timeout_ms)

    def wait_on_gva_enabled(self, timeout_ms: int) -> bool:
        """
        Wait for the GVA (Google Voice Assistant) enable response from the cloud.
        """
        return self._wait([self.protocol.flag_gva_enabled], timeout_ms)

    def wait_on_st_enabled(self, timeout_ms: int) -> bool:
        """
        Wait for the SmartThings enable response from the cloud.
        """
        return self._wait([self.protocol.flag_st_enabled], timeout_ms)

    def wait_on_state_started_listening(self, timeout_ms: int) -> bool:
        """
        Wait for the node to start listening for state changes.
        """
        return self._wait([self.protocol.flag_state_started_listening], timeout_ms)

    def wait_on_state_reported(self, timeout_ms: int) -> bool:
        """
        Wait for the node to report its state.
        """
        return self._wait([self.protocol.flag_state_reported], timeout_ms)

    def wait_on_timeseries_reported(self, timeout_ms: int) -> bool:
        """
        Wait for the node to report its timeseries.
        """
        return self._wait([self.protocol.flag_timeseries_reported], timeout_ms)

    def wait_on_node_config_sent(self, timeout_ms: int) -> bool:
        """
        Wait for the node to send its configuration.
        """
        return self._wait([self.protocol.flag_node_config_sent], timeout_ms)

    def wait_on_notification_sent(self, timeout_ms: int) -> bool:
        """
        Wait for the node to send a notification.
        """
        return self._wait([self.protocol.flag_notification_sent], timeout_ms)

    def wait_on_group_info(self, timeout_ms: int) -> bool:
        """
        Wait for the node to receive the group info.
        """
        return self._wait([self.protocol.flag_group_info], timeout_ms)

    def wait_on_sched_version(self, timeout_ms: int) -> bool:
        """
        Wait for the node to receive the schedule version.
        """
        return self._wait([self.protocol.flag_sched_version], timeout_ms)

    def wait_on_sched_details(self, timeout_ms: int) -> bool:
        """
        Wait for the node to receive the schedule details.
        """
        return self._wait([self.protocol.flag_sched_details], timeout_ms)

    def wait_on_trigger_version(self, timeout_ms: int) -> bool:
        """
        Wait for the node to receive the trigger version.
        """
        return self._wait([self.protocol.flag_trigger_version], timeout_ms)

    def wait_on_trigger_details(self, timeout_ms: int) -> bool:
        """
        Wait for the node to receive the trigger details.
        """
        return self._wait([self.protocol.flag_trigger_details], timeout_ms)

    def wait_on_all_cloud_get_events(
        self,
        timeout_ms: int,
        add_alexa_enabled: bool = True,
        add_gva_enabled: bool = True,
        add_st_enabled: bool = True,
    ) -> bool:
        """
        Wait for all cloud events to be received.
        """
        flags = [
            self.protocol.flag_group_info,
            self.protocol.flag_sched_version,
            self.protocol.flag_trigger_version,
        ]
        if add_alexa_enabled:
            flags.append(self.protocol.flag_alexa_enabled)
        if add_gva_enabled:
            flags.append(self.protocol.flag_gva_enabled)
        if add_st_enabled:
            flags.append(self.protocol.flag_st_enabled)
        return self._wait(flags, timeout_ms)

    def wait_on_all_sched_events(self, timeout_ms: int) -> bool:
        """
        Wait for all schedule events to be received.
        """
        return self._wait(
            [self.protocol.flag_sched_version, self.protocol.flag_sched_details],
            timeout_ms,
        )

    def wait_on_all_trigger_events(self, timeout_ms: int) -> bool:
        """
        Wait for all trigger events to be received.
        """
        return self._wait(
            [self.protocol.flag_trigger_version, self.protocol.flag_trigger_details],
            timeout_ms,
        )

    def clear_on_online(self) -> bool:
        """
        Clear the online flag.
        """
        return self._clear([self.protocol.flag_online])

    def clear_on_alexa_enabled(self) -> bool:
        """
        Clear the Alexa enabled flag.
        """
        return self._clear([self.protocol.flag_alexa_enabled])

    def clear_on_gva_enabled(self) -> bool:
        """
        Clear the GVA enabled flag.
        """
        return self._clear([self.protocol.flag_gva_enabled])

    def clear_on_st_enabled(self) -> bool:
        """
        Clear the SmartThings enabled flag.
        """
        return self._clear([self.protocol.flag_st_enabled])

    def clear_on_state_started_listening(self) -> bool:
        """
        Clear the state started listening flag.
        """
        return self._clear([self.protocol.flag_state_started_listening])

    def clear_on_state_reported(self) -> bool:
        """
        Clear the state reported flag.
        """
        return self._clear([self.protocol.flag_state_reported])

    def clear_on_timeseries_reported(self) -> bool:
        """
        Clear the timeseries reported flag.
        """
        return self._clear([self.protocol.flag_timeseries_reported])

    def clear_on_node_config_sent(self) -> bool:
        """
        Clear the node config sent flag.
        """
        return self._clear([self.protocol.flag_node_config_sent])

    def clear_on_notification_sent(self) -> bool:
        """
        Clear the notification sent flag.
        """
        return self._clear([self.protocol.flag_notification_sent])

    def clear_on_group_info(self) -> bool:
        """
        Clear the group info flag.
        """
        return self._clear([self.protocol.flag_group_info])

    def clear_on_sched_version(self) -> bool:
        """
        Clear the sched version flag.
        """
        return self._clear([self.protocol.flag_sched_version])

    def clear_on_sched_details(self) -> bool:
        """
        Clear the sched details flag.
        """
        return self._clear([self.protocol.flag_sched_details])

    def clear_on_trigger_version(self) -> bool:
        """
        Clear the trigger version flag.
        """
        return self._clear([self.protocol.flag_trigger_version])

    def clear_on_trigger_details(self) -> bool:
        """
        Clear the trigger details flag.
        """
        return self._clear([self.protocol.flag_trigger_details])

    def clear_on_all_cloud_get_events(self) -> bool:
        """
        Clear all cloud events.
        """
        flags = [
            self.protocol.flag_group_info,
            self.protocol.flag_alexa_enabled,
            self.protocol.flag_gva_enabled,
            self.protocol.flag_st_enabled,
            self.protocol.flag_sched_version,
            self.protocol.flag_trigger_version,
        ]
        return self._clear(flags)

    def clear_on_all_sched_events(self) -> bool:
        """
        Clear all schedule events.
        """
        flags = [
            self.protocol.flag_sched_version,
            self.protocol.flag_sched_details,
        ]
        return self._clear(flags)

    def clear_on_all_trigger_events(self) -> bool:
        """
        Clear all trigger events.
        """
        flags = [
            self.protocol.flag_trigger_version,
            self.protocol.flag_trigger_details,
        ]
        return self._clear(flags)

    def get_current_time(self) -> dt | None:
        """
        Get the current time.
        """
        cmd = CommandGetCurrentTime()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return None
        try:
            int_payload = int(payload)
            current_timezone = self.get_current_timezone()
            if current_timezone is None:
                return dt.fromtimestamp(int_payload)
            return dt.fromtimestamp(int_payload, tz=ZoneInfo(current_timezone))
        except ValueError:
            return None

    def get_current_time_ms(self) -> int | None:
        """
        Get current Unix time in milliseconds from the node (same clock as latency recv_ts).
        """
        cmd = CommandGetCurrentTimeMs()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return None
        try:
            return int(payload)
        except ValueError:
            return None

    def get_current_timezone(self) -> str | None:
        """
        Get the current timezone.
        """
        cmd = CommandGetCurrentTimezone()
        return self._send_cmd_for_payload(cmd)

    def get_thing_name(self) -> str | None:
        """
        Get the thing name.
        """
        cmd = CommandGetThingName()
        return self._send_cmd_for_payload(cmd)

    def get_indexed_shadow(self, timeout_ms: int = 5000) -> str | None:
        """
        Get the indexed shadow.
        """
        cmd = CommandGetIndexedShadow()
        json_str = self._send_cmd_for_payload(cmd, timeout_ms=timeout_ms)
        if json_str is None:
            return None
        try:
            return json.loads(json_str)
        except json.JSONDecodeError:
            return None

    def get_named_shadow(self, timeout_ms: int = 5000) -> str | None:
        """
        Get the named shadow.
        """
        cmd = CommandGetNamedShadow()
        json_str = self._send_cmd_for_payload(cmd, timeout_ms=timeout_ms)
        if json_str is None:
            return None
        try:
            return json.loads(json_str)
        except json.JSONDecodeError:
            return None

    def get_param(self, device_id: str, param_id: str) -> ParamData | None:
        """
        Get the value of a parameter.
        """
        cmd = CommandGetParam(device_id, param_id)
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return None
        return ParamData(self.protocol, payload)

    def get_tag_value(self, tag_name: str) -> str | None:
        """
        Get the value of a tag.
        """
        cmd = CommandGetTagValue(tag_name)
        return self._send_cmd_for_payload(cmd)

    def get_group_info_str(self) -> str | None:
        """
        Get the group info as a string.
        """
        cmd = CommandGetGroupInfo()
        return self._send_cmd_for_payload(cmd)

    def get_alexa_enabled(self) -> bool:
        """
        Get the Alexa enabled status.
        """
        cmd = CommandGetAlexaEnabled()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return False
        return self.protocol.parse_param_value(f"b{payload}")

    def get_gva_enabled(self) -> bool:
        """
        Get the GVA enabled status.
        """
        cmd = CommandGetGvaEnabled()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return False
        return self.protocol.parse_param_value(f"b{payload}")

    def get_st_enabled(self) -> bool:
        """
        Get the SmartThings enabled status.
        """
        cmd = CommandGetStEnabled()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return False
        return self.protocol.parse_param_value(f"b{payload}")

    def get_sched_version(self) -> int | None:
        """
        Get the sched version.
        """
        cmd = CommandGetSchedVersion()
        payload = self._send_cmd_for_payload(cmd)
        try:
            return int(payload)
        except ValueError:
            return None

    def get_trigger_version(self) -> int | None:
        """
        Get the trigger version.
        """
        cmd = CommandGetTriggerVersion()
        payload = self._send_cmd_for_payload(cmd)
        try:
            return int(payload)
        except ValueError:
            return None

    def get_heap_status(self) -> HeapStatusData | None:
        """
        Get the heap status.
        """
        cmd = CommandGetHeapStatus()
        payload = self._send_cmd_for_payload(cmd)
        if payload is None:
            return None
        return HeapStatusData(self.protocol, payload)

    def time_control_set_time(self, datetime: dt) -> bool:
        """
        Set the time.
        """
        cmd = CommandTimeControlSetTime(datetime)
        return self._send_cmd_for_ok(cmd)

    def time_control_advance_time(self, delta: td) -> bool:
        """
        Advance the time.
        """
        cmd = CommandTimeControlAdvanceTime(delta)
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_force_network_failure(self) -> bool:
        """
        Force all network operations (connect, send, recv) to fail.
        """
        cmd = CommandMqttControlForceNetworkFailure()
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_restore_network_default(self) -> bool:
        """
        Restore default network operations (connect, send, recv) settings.
        """
        cmd = CommandMqttControlRestoreNetworkDefault()
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_force_operations_failure(self) -> bool:
        """
        Force all MQTT operations (publish, subscribe, unsubscribe) to fail.
        """
        cmd = CommandMqttControlForceOperationsFailure()
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_restore_operations_default(self) -> bool:
        """
        Restore default MQTT operations (publish, subscribe, unsubscribe) settings.
        """
        cmd = CommandMqttControlRestoreOperationsDefault()
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_disconnect(self) -> bool:
        """
        Explicitly disconnect MQTT via esp_rmaker_mqtt_impl.disconnect.
        """
        cmd = CommandMqttControlDisconnect()
        return self._send_cmd_for_ok(cmd)

    def mqtt_control_connect(self) -> bool:
        """
        Explicitly (re)connect MQTT via esp_rmaker_mqtt_impl.connect.
        """
        cmd = CommandMqttControlConnect()
        return self._send_cmd_for_ok(cmd)

    def _cloud_control_send(self, events: list[str]) -> bool:
        """
        Send cloud events.
        """
        cmd = CommandCloudControlSend(events)
        return self._send_cmd_for_ok(cmd)

    def cloud_control_send_getSchedVer(self) -> bool:
        """
        Send the getSchedVer cloud event.
        """
        return self._cloud_control_send([self.protocol.cloud_control_event_getSchedVer])

    def cloud_control_send_getTriggerVer(self) -> bool:
        """
        Send the getTriggerVer cloud event.
        """
        return self._cloud_control_send(
            [self.protocol.cloud_control_event_getTriggerVer]
        )


# ----- Bridge API -----------------------------------------------------------


class BridgeChildHostCtrl:
    """
    Handle to a single bridged child Thing on the host-controlled firmware.
    """

    def __init__(
        self,
        parent: "NodeHostCtrl",
        handle: int,
        thing_name: str,
        bridge_local_id: str,
    ):
        self._parent = parent
        self._handle = handle
        self.thing_name = thing_name
        self.bridge_local_id = bridge_local_id
        self._removed = False
        self._info_filled = False
        self._devices_committed = False

    @property
    def handle(self) -> int:
        return self._handle

    @property
    def removed(self) -> bool:
        return self._removed

    def _require_live(self) -> None:
        if self._removed:
            raise RuntimeError(
                f"BridgeChildHostCtrl for {self.bridge_local_id} has been removed"
            )

    def mark_online(self, online: bool) -> bool:
        self._require_live()
        if not self._devices_committed:
            raise RuntimeError(
                f"mark_online refused: child {self.bridge_local_id} has not been committed; "
                "call commit_devices() first (after fill_info + add_param)"
            )
        return self._parent._send_cmd_for_ok(
            CommandBridgeMarkOnline(self._handle, online)
        )

    def get_thing_name(self) -> str | None:
        """Refresh the cloud-assigned thing name from firmware."""
        self._require_live()
        payload = self._parent._send_cmd_for_payload(
            CommandBridgeChildThingName(self._handle)
        )
        if payload is not None:
            self.thing_name = payload
        return payload

    def get_local_id(self) -> str | None:
        self._require_live()
        return self._parent._send_cmd_for_payload(
            CommandBridgeChildLocalId(self._handle)
        )

    def get_group_info_str(self) -> str | None:
        """Empty string until the child has received its first getGroupInfo."""
        self._require_live()
        # Empty payload is normal — `_send_cmd_for_payload` returns None for
        # an OK response with no body, callers should treat that as "".
        payload = self._parent._send_cmd_for_payload(
            CommandBridgeChildGroupInfo(self._handle)
        )
        return "" if payload is None else payload

    # --- param / device management ---

    @property
    def info_filled(self) -> bool:
        return self._info_filled

    def fill_info(self, name: str, type_: str, fw_version: str, model: str) -> bool:
        """Fill the child node's info (name/type/fw_version/model).

        Must be called before ``commit_devices`` — child node-config
        publish dereferences ``info`` and crashes if unfilled. Firmware
        rejects re-fill (``ESP_RMAKER_INVALID_STATE``), so this is a
        one-shot per child.
        """
        self._require_live()
        ok = self._parent._send_cmd_for_ok(
            CommandBridgeChildFillInfo(self._handle, name, type_, fw_version, model)
        )
        if ok:
            self._info_filled = True
        return ok

    @property
    def devices_committed(self) -> bool:
        return self._devices_committed

    def commit_devices(self) -> bool:
        self._require_live()
        if not self._info_filled:
            raise RuntimeError(
                f"commit_devices refused: child {self.bridge_local_id} has no info; "
                "call fill_info(name, type, fw_version, model) first"
            )
        ok = self._parent._send_cmd_for_ok(CommandBridgeCommitDevices(self._handle))
        if ok:
            self._devices_committed = True
        return ok

    def add_param(
        self,
        device_id: str,
        device_type: str,
        param_id: str,
        param_type: str,
        value: bool | int | float | str | dict | list,
        properties: list[str],
        param_ui_type: str | None = None,
        min_bound=None,
        max_bound=None,
        step=None,
    ) -> bool:
        self._require_live()
        cmd = CommandBridgeChildAddParam(
            self._handle,
            device_id,
            device_type,
            param_id,
            param_type,
            param_ui_type,
            value,
            properties,
            min_bound,
            max_bound,
            step,
        )
        return self._parent._send_cmd_for_ok(cmd)

    def update_param(
        self,
        device_id: str,
        param_id: str,
        value: bool | int | float | str | dict | list,
    ) -> bool:
        self._require_live()
        return self._parent._send_cmd_for_ok(
            CommandBridgeChildUpdateParam(self._handle, device_id, param_id, value)
        )

    def get_param(self, device_id: str, param_id: str) -> "ParamData | None":
        self._require_live()
        payload = self._parent._send_cmd_for_payload(
            CommandBridgeChildGetParam(self._handle, device_id, param_id)
        )
        if payload is None:
            return None
        return ParamData(self._parent.protocol, payload)

    # --- per-child flag bitmap ---

    def _wait(self, flags: list[str], timeout_ms: int) -> bool:
        self._require_live()
        return self._parent._send_cmd_for_ok(
            CommandBridgeChildWaitFlags(self._handle, flags, timeout_ms),
            timeout_ms=timeout_ms + 5000,
        )

    def _clear(self, flags: list[str]) -> bool:
        self._require_live()
        return self._parent._send_cmd_for_ok(
            CommandBridgeChildClearFlags(self._handle, flags)
        )

    def wait_on_online(self, timeout_ms: int) -> bool:
        return self._wait([self._parent.protocol.flag_online], timeout_ms)

    def clear_on_online(self) -> bool:
        return self._clear([self._parent.protocol.flag_online])

    def wait_on_group_info(self, timeout_ms: int) -> bool:
        return self._wait([self._parent.protocol.flag_group_info], timeout_ms)

    def clear_on_group_info(self) -> bool:
        return self._clear([self._parent.protocol.flag_group_info])

    def wait_on_state_started_listening(self, timeout_ms: int) -> bool:
        return self._wait(
            [self._parent.protocol.flag_state_started_listening], timeout_ms
        )

    def clear_on_state_started_listening(self) -> bool:
        return self._clear([self._parent.protocol.flag_state_started_listening])

    def wait_on_node_config_sent(self, timeout_ms: int) -> bool:
        return self._wait([self._parent.protocol.flag_node_config_sent], timeout_ms)

    def clear_on_node_config_sent(self) -> bool:
        return self._clear([self._parent.protocol.flag_node_config_sent])

    def wait_on_trigger_details(self, timeout_ms: int) -> bool:
        return self._wait([self._parent.protocol.flag_trigger_details], timeout_ms)

    def clear_on_trigger_details(self) -> bool:
        return self._clear([self._parent.protocol.flag_trigger_details])

    def wait_on_sched_details(self, timeout_ms: int) -> bool:
        return self._wait([self._parent.protocol.flag_sched_details], timeout_ms)

    def clear_on_sched_details(self) -> bool:
        return self._clear([self._parent.protocol.flag_sched_details])

    # Note: there is no per-child STATE_REPORTED. Use ``NodeHostCtrl.wait_on_state_reported``
    # on the parent — the state pipeline's publish-complete signal is global.
    # Same for NOTIFICATION_SENT: the automation report uses the parent's MQTT
    # client and only the parent-global flag fires.

    def set_config(self, config: NodeConfig) -> bool:
        """
        Apply a child's slice of a NodeConfig (devices/params only — tags
        and services are parent-shared and ignored). Auto-fills child
        node info (using NodeConfig's ``info`` block when present, else
        synthetic defaults derived from ``bridge_local_id``) so callers
        don't have to call ``fill_info`` separately.

        Does NOT call ``commit_devices`` — caller is responsible for
        committing (typically via ``start_bridge_child``). This avoids
        a double-commit pattern where ``set_config`` commits, then the
        test/harness clears flags and commits again, but the second
        commit hits the unchanged-checksum path and does not publish.
        """
        self._require_live()
        if not self._info_filled:
            info = config.json_config.get("info") or {}
            name = info.get("name") or self.bridge_local_id or "bridge-child"
            type_ = info.get("type") or "esp.node.generic"
            fw_version = info.get("fw_version") or "1.0"
            model = info.get("model") or "bridge-child"
            if not self.fill_info(name, type_, fw_version, model):
                return False
        ok = True
        for device in config.json_config.get("devices", []):
            for param in device.get("params", []):
                bounds = (
                    param.get("bounds", {})
                    if isinstance(param.get("bounds", {}), dict)
                    else {}
                )
                if not self.add_param(
                    device_id=device["id"],
                    device_type=device["type"],
                    param_id=param["id"],
                    param_type=param["type"],
                    value=param["value"],
                    properties=param["properties"],
                    param_ui_type=param.get("ui_type"),
                    min_bound=bounds.get("min"),
                    max_bound=bounds.get("max"),
                    step=bounds.get("step"),
                ):
                    ok = False
        return ok


class BridgeHostCtrl:
    """Per-NodeHostCtrl entry point for bridge operations."""

    DEFAULT_ADD_TIMEOUT_MS = 10000
    DEFAULT_REMOVE_TIMEOUT_MS = 10000

    def __init__(self, parent: "NodeHostCtrl"):
        self._parent = parent

    def add_child(
        self,
        child_suffix: str,
        bridge_local_id: str,
        timeout_ms: int = DEFAULT_ADD_TIMEOUT_MS,
    ) -> BridgeChildHostCtrl | None:
        """
        Request creation of a bridged child Thing and block until the
        cloud round-trip completes. Returns the child handle on success,
        None on timeout / error.
        """
        cmd = CommandBridgeAddChild(child_suffix, bridge_local_id, timeout_ms)
        # Generous outer timeout so the firmware-side poll runs to completion.
        payload = self._parent._send_cmd_for_payload(cmd, timeout_ms=timeout_ms + 5000)
        if payload is None:
            return None
        parts = payload.split(self._parent.protocol.delimiter_char)
        if len(parts) < 2:
            return None
        try:
            handle = int(parts[0])
        except ValueError:
            return None
        thing_name = parts[1]
        return BridgeChildHostCtrl(self._parent, handle, thing_name, bridge_local_id)

    def add_child_no_ack(
        self,
        child_suffix: str,
        bridge_local_id: str,
        timeout_ms: int = 5000,
    ) -> bool:
        """Fire-and-forget add. Returns True once the firmware bridge SDK
        has accepted the request; the child may still be in PENDING_ADD
        until its bridgeAck round-trip completes. Caller must use
        ``list_children`` / ``wait_for_children`` to learn when the
        child reaches READY and to obtain the handle + thing_name.
        Intended for bulk registration where one ``bridgeAck`` RTT per
        child would dominate setup wall time."""
        cmd = CommandBridgeAddChildNoAck(child_suffix, bridge_local_id)
        return self._parent._send_cmd_for_ok(cmd, timeout_ms=timeout_ms)

    def wait_for_children(
        self,
        expected_count: int,
        timeout_s: float = 30.0,
        poll_interval_s: float = 0.5,
    ) -> list[BridgeChildHostCtrl]:
        """Poll ``list_children`` until the count of READY children
        reaches ``expected_count`` or ``timeout_s`` elapses. Returns
        whatever the last poll saw (may be shorter than ``expected_count``
        if the timeout fires). Pairs with ``add_child_no_ack`` to batch
        many adds + single readiness barrier instead of N serial acks."""
        import time as _time

        deadline = _time.monotonic() + timeout_s
        last: list[BridgeChildHostCtrl] = []
        while True:
            last = self.list_children()
            if len(last) >= expected_count:
                return last
            if _time.monotonic() >= deadline:
                return last
            _time.sleep(poll_interval_s)

    def remove_child(
        self,
        child: BridgeChildHostCtrl,
        timeout_ms: int = DEFAULT_REMOVE_TIMEOUT_MS,
    ) -> bool:
        cmd = CommandBridgeRemoveChild(child.handle, timeout_ms)
        ok = self._parent._send_cmd_for_ok(cmd, timeout_ms=timeout_ms + 5000)
        if ok:
            child._removed = True
        return ok

    def _list_children_page(self, start: int, count: int) -> list[BridgeChildHostCtrl]:
        """Fetch a single page of children starting at index ``start``."""
        cmd = CommandBridgeListChildren(start=start, count=count)
        payload = self._parent._send_cmd_for_payload(cmd)
        if payload is None or payload == "":
            return []
        out: list[BridgeChildHostCtrl] = []
        for entry in payload.split(","):
            fields = entry.split(":")
            if len(fields) < 2:
                continue
            try:
                handle = int(fields[0])
            except ValueError:
                continue
            thing_name = fields[1]
            out.append(
                BridgeChildHostCtrl(
                    self._parent, handle, thing_name, bridge_local_id=""
                )
            )
        return out

    def list_children(self, page_size: int | None = None) -> list[BridgeChildHostCtrl]:
        """
        Return one BridgeChildHostCtrl per currently-ready child known to
        this host control layer. Only children added via host control (via
        add_child) appear here.

        The firmware response is paginated (it caps a single page to bound
        the payload buffer); this pages through with ``page_size`` until a
        short read, so large pools (hundreds of children) don't blow up a
        single response. ``page_size`` defaults to (and is clamped to) the
        firmware page cap parsed from the shared host-control constants header.

        Pagination indexes the host-visible children by position in
        firmware visitation order. If the ready-set grows *between* pages
        (a child finishing its add while we page), a child inserted before
        the cursor shifts later entries up by one, so the next page can
        re-emit a boundary child (and the newly-ready one is missed until
        a later call). We dedup by handle here so a concurrent insert can
        never inflate the result with a duplicate; a transient miss is
        self-correcting on the next ``list_children`` call. The paging
        terminator uses the raw firmware page length, not the deduped
        count, so dedup can't cause early termination.
        """
        # Clamp to the firmware page cap — a larger request is silently
        # clamped firmware-side, which would make every full page look like
        # a short read and stop paging after the first one.
        page_max = self._parent.protocol.bridge_list_page_max
        page_size = page_max if page_size is None else min(page_size, page_max)
        out: list[BridgeChildHostCtrl] = []
        seen_handles: set[int] = set()
        start = 0
        while True:
            page = self._list_children_page(start, page_size)
            for child in page:
                if child.handle in seen_handles:
                    continue
                seen_handles.add(child.handle)
                out.append(child)
            if len(page) < page_size:
                break
            start += len(page)
        return out
