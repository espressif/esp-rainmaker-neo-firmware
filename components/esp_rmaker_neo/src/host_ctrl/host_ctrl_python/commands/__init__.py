# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: E402 -- sys.path prelude must run before the imports below

import re
from time import time
from serial import Serial
from pathlib import Path
from typing import Type, Optional
import json
from io import IOBase
from ..color import bcolors, ColorFormatter

# Add the osal/ext-io/esp/python directory to the Python path
import sys
import os

OSAL_EXT_IO_ESP_PYTHON_DIR = os.path.join(
    os.path.dirname(__file__),
    "..",
    "..",
    "..",
    "..",
    "..",
    "osal",
    "ext-io",
    "esp",
    "python",
)
if OSAL_EXT_IO_ESP_PYTHON_DIR not in sys.path:
    sys.path.append(OSAL_EXT_IO_ESP_PYTHON_DIR)
from uart_multiplexer import UARTMultiplexer


class CommunicationProtocol:
    def __init__(self):
        # Load constants from the C header using a file-relative path
        header_path = (
            Path(__file__).resolve().parent
            / "../../../../include/esp_rmaker_host_ctrl_constants.h"
        )

        self._constants = self.__parse_header_constants(header_path)

        # Markers
        self.end_char = self._constants.get("RMAKER_HOST_CTRL_END_CHAR")
        self.delimiter_char = self._constants.get("RMAKER_HOST_CTRL_DELIMITER_CHAR")

        # Commands
        self.command_ping = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_PING")
        try:
            self.command_ping_length = int(
                self._constants.get("RMAKER_HOST_CTRL_VAL_PING_LENGTH")
            )
        except ValueError:
            self.command_ping_length = -1
        self.command_start = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_START")
        self.command_stop = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_STOP")
        self.command_reset = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_RESET")
        self.command_reset_keep_nvs = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_RESET_KEEP_NVS"
        )
        self.command_kill = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_KILL")
        self.command_add = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_ADD")
        self.command_remove = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_REMOVE"
        )
        self.command_update = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_UPDATE"
        )
        self.command_get = self._constants.get("RMAKER_HOST_CTRL_COMMAND_CHAR_GET")
        self.command_wait_flags = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_WAIT_FLAGS"
        )
        self.command_clear_flags = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_CLEAR_FLAGS"
        )
        self.command_time_control = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_TIME_CONTROL"
        )
        self.command_mqtt_control = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_MQTT_CONTROL"
        )
        self.command_cloud_control = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_CLOUD_CONTROL"
        )
        self.command_bridge = self._constants.get(
            "RMAKER_HOST_CTRL_COMMAND_CHAR_BRIDGE"
        )

        # Bridge sub-commands
        self.bridge_sub_add_child = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD"
        )
        self.bridge_sub_add_child_no_ack = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD_NO_ACK"
        )
        self.bridge_sub_remove_child = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_REMOVE_CHILD"
        )
        self.bridge_sub_mark_online = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_MARK_ONLINE"
        )
        self.bridge_sub_child_thing_name = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_THING_NAME"
        )
        self.bridge_sub_child_local_id = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_LOCAL_ID"
        )
        self.bridge_sub_child_group_info = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GROUP_INFO"
        )
        self.bridge_sub_list_children = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_LIST_CHILDREN"
        )
        self.bridge_sub_commit_devices = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_COMMIT_DEVICES"
        )
        self.bridge_sub_child_fill_info = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_FILL_INFO"
        )
        self.bridge_sub_child_add_param = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_ADD_PARAM"
        )
        self.bridge_sub_child_update_param = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_UPDATE_PARAM"
        )
        self.bridge_sub_child_get_param = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GET_PARAM"
        )
        self.bridge_sub_child_wait_flags = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_WAIT_FLAGS"
        )
        self.bridge_sub_child_clear_flags = self._constants.get(
            "RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_CLEAR_FLAGS"
        )
        # Max children per list_children page (firmware clamps to this).
        # Default 32 if the header somehow lacks the define.
        _page_max = self._constants.get("RMAKER_HOST_CTRL_BRIDGE_LIST_PAGE_MAX")
        self.bridge_list_page_max = int(_page_max) if _page_max else 32

        # Payload types
        self.payload_param = self._constants.get(
            "RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_PARAM"
        )
        self.payload_services = self._constants.get(
            "RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_SERVICES"
        )
        self.payload_tag = self._constants.get("RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TAG")
        self.payload_timezone = self._constants.get(
            "RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TIMEZONE"
        )
        self.payload_local_config = self._constants.get(
            "RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_LOCAL_CONFIG"
        )

        # Services
        self.service_timezone = self._constants.get(
            "RMAKER_HOST_CTRL_SERVICE_CHAR_TIMEZONE"
        )
        self.service_latency = self._constants.get(
            "RMAKER_HOST_CTRL_SERVICE_CHAR_LATENCY"
        )
        self.service_local_ctrl = self._constants.get(
            "RMAKER_HOST_CTRL_SERVICE_CHAR_LOCAL_CTRL"
        )
        self.service_on_network_chal_resp = self._constants.get(
            "RMAKER_HOST_CTRL_SERVICE_CHAR_ON_NETWORK_CHAL_RESP"
        )

        # Local configuration keys
        self.local_config_key_sched_ver = self._constants.get(
            "RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_SCHED_VER"
        )
        self.local_config_key_trigger_ver = self._constants.get(
            "RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_TRIGGER_VER"
        )
        self.local_config_key_local_ctrl_http_port = self._constants.get(
            "RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_HTTP_PORT"
        )
        self.local_config_key_local_ctrl_pop = self._constants.get(
            "RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_POP"
        )

        # Data types
        self.data_type_int = self._constants.get("RMAKER_HOST_CTRL_DATA_TYPE_CHAR_INT")
        self.data_type_float = self._constants.get(
            "RMAKER_HOST_CTRL_DATA_TYPE_CHAR_FLOAT"
        )
        self.data_type_boolean = self._constants.get(
            "RMAKER_HOST_CTRL_DATA_TYPE_CHAR_BOOLEAN"
        )
        self.data_type_string = self._constants.get(
            "RMAKER_HOST_CTRL_DATA_TYPE_CHAR_STRING"
        )
        self.data_type_object = self._constants.get(
            "RMAKER_HOST_CTRL_DATA_TYPE_CHAR_OBJECT"
        )
        self.data_type_array = self._constants.get(
            "RMAKER_HOST_CTRL_DATA_TYPE_CHAR_ARRAY"
        )

        # Gettables
        self.gettable_current_time = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME"
        )
        self.gettable_current_time_ms = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME_MS"
        )
        self.gettable_current_timezone = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIMEZONE"
        )
        self.gettable_thing_name = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_THING_NAME"
        )
        self.gettable_signature = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_SIGNATURE"
        )
        self.gettable_indexed_shadow = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_INDEXED_SHADOW"
        )
        self.gettable_named_shadow = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_NAMED_SHADOW"
        )
        self.gettable_param = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_PARAM"
        )
        self.gettable_tag_value = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_TAG_VALUE"
        )
        self.gettable_group_info = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_GROUP_INFO"
        )
        self.gettable_alexa_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_ALEXA_ENABLED"
        )
        self.gettable_gva_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_GVA_ENABLED"
        )
        self.gettable_st_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_ST_ENABLED"
        )
        self.gettable_sched_version = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_SCHED_VERSION"
        )
        self.gettable_trigger_version = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_TRIGGER_VERSION"
        )
        self.gettable_heap_status = self._constants.get(
            "RMAKER_HOST_CTRL_GETTABLE_CHAR_HEAP_STATUS"
        )

        # Waitables
        self.flag_online = self._constants.get("RMAKER_HOST_CTRL_FLAG_CHAR_ONLINE")
        self.flag_state_reported = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_STATE_REPORTED"
        )
        self.flag_timeseries_reported = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_TIMESERIES_REPORTED"
        )
        self.flag_node_config_sent = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_NODE_CONFIG_SENT"
        )
        self.flag_notification_sent = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_NOTIFICATION_SENT"
        )
        self.flag_state_started_listening = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_STATE_STARTED_LISTENING"
        )
        self.flag_group_info = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_GROUP_INFO"
        )
        self.flag_alexa_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_ALEXA_ENABLED"
        )
        self.flag_gva_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_GVA_ENABLED"
        )
        self.flag_st_enabled = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_ST_ENABLED"
        )
        self.flag_sched_version = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_VERSION"
        )
        self.flag_sched_details = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_DETAILS"
        )
        self.flag_trigger_version = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_VERSION"
        )
        self.flag_trigger_details = self._constants.get(
            "RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_DETAILS"
        )

        # Properties
        self.property_param_read = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_READ"
        )
        self.property_param_write = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_WRITE"
        )
        self.property_param_indexed = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_INDEXED"
        )
        self.property_param_persist = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_PERSIST"
        )
        self.property_param_time_series = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TIME_SERIES"
        )
        self.property_param_ts_cumulative = self._constants.get(
            "RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TS_CUMULATIVE"
        )

        # Time control
        self.time_control_set_time = self._constants.get(
            "RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_SET"
        )
        self.time_control_advance_time = self._constants.get(
            "RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_ADVANCE"
        )

        # MQTT control
        self.mqtt_control_force_network_failure = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_FAILURE"
        )
        self.mqtt_control_restore_network_default = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_RESTORE"
        )
        self.mqtt_control_force_operations_failure = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_FAILURE"
        )
        self.mqtt_control_restore_operations_default = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_RESTORE"
        )
        self.mqtt_control_disconnect = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_DISCONNECT"
        )
        self.mqtt_control_connect = self._constants.get(
            "RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_CONNECT"
        )

        # Cloud control
        self.cloud_control_send = self._constants.get(
            "RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_SEND"
        )
        self.cloud_control_event_getSchedVer = self._constants.get(
            "RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getSchedVer"
        )
        self.cloud_control_event_getTriggerVer = self._constants.get(
            "RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getTriggerVer"
        )

        # Response codes
        self.response_ok = self._constants.get("RMAKER_HOST_CTRL_RESPONSE_CHAR_OK")
        self.response_error = self._constants.get(
            "RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR"
        )
        self.response_invalid = self._constants.get(
            "RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID"
        )
        self.response_type_mismatch = self._constants.get(
            "RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH"
        )
        self.response_not_found = self._constants.get(
            "RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND"
        )
        self.response_timeout = self._constants.get(
            "RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT"
        )

        # Service mapping
        self.service_mapping = {
            "timezone": self.service_timezone,
            "latency": self.service_latency,
            "local_ctrl": self.service_local_ctrl,
            "on_network_chal_resp": self.service_on_network_chal_resp,
        }

        # Local configuration keys mapping
        self.local_config_key_mapping = {
            self.local_config_key_sched_ver: "schedule version",
            self.local_config_key_trigger_ver: "trigger version",
            self.local_config_key_local_ctrl_http_port: "local control HTTP port",
            self.local_config_key_local_ctrl_pop: "local control PoP",
        }

        # Response message mapping
        self.response_message_mapping = {
            self.response_ok: "success",
            self.response_error: "internal error",
            self.response_invalid: "invalid command",
            self.response_type_mismatch: "parameter value type mismatch",
            self.response_not_found: "not found",
            self.response_timeout: "timeout",
        }

        # Flag mapping
        self.flag_mapping = {
            self.flag_online: "online",
            self.flag_state_reported: "state reported",
            self.flag_timeseries_reported: "timeseries reported",
            self.flag_node_config_sent: "node config sent",
            self.flag_notification_sent: "notification sent",
            self.flag_state_started_listening: "state started listening",
            self.flag_group_info: "group info",
            self.flag_alexa_enabled: "Alexa enabled",
            self.flag_gva_enabled: "GVA enabled",
            self.flag_st_enabled: "SmartThings enabled",
            self.flag_sched_version: "sched version",
            self.flag_sched_details: "sched details",
            self.flag_trigger_version: "trigger version",
            self.flag_trigger_details: "trigger details",
        }

        # Parameter property mapping
        self.property_param_mapping = {
            self.property_param_read: "read",
            self.property_param_write: "write",
            self.property_param_indexed: "indexed",
            self.property_param_persist: "persist",
            self.property_param_time_series: "time_series",
            self.property_param_ts_cumulative: "ts_cumulative",
        }

        # Parameter property mapping reverse
        self.property_param_mapping_reverse = {
            v: k for k, v in self.property_param_mapping.items()
        }

        # Cloud control event mapping
        self.cloud_control_event_mapping = {
            self.cloud_control_event_getSchedVer: "getSchedVer",
            self.cloud_control_event_getTriggerVer: "getTriggerVer",
        }

    @staticmethod
    def __c_char_to_python(value: str) -> str:
        # Convert a C char literal content (without quotes) to a Python string
        # Handles escaped forms like \r, \n, \t, etc.
        try:
            return bytes(value, "utf-8").decode("unicode_escape")
        except Exception:
            return value

    @classmethod
    def __parse_header_constants(cls, header_path: Path) -> dict:
        constants: dict[str, str] = {}
        if not header_path.exists():
            return constants

        define_re = re.compile(
            r"^\s*#define\s+([A-Za-z0-9_]+)\s+(?:'([^']+)'|([^\s]+))"
        )
        with header_path.open("r", encoding="utf-8") as f:
            for line in f:
                m = define_re.match(line)
                if not m:
                    continue
                name, raw_char, raw_val = m.group(1), m.group(2), m.group(3)
                constants[name] = cls.__c_char_to_python(
                    raw_char if raw_char else raw_val
                )
        return constants

    def __get_dtype_char(self, dtype: Type) -> str:
        if dtype is int:
            return self.data_type_int
        elif dtype is float:
            return self.data_type_float
        elif dtype is bool:
            return self.data_type_boolean
        elif dtype is str:
            return self.data_type_string
        elif dtype is dict:
            return self.data_type_object
        elif dtype is list:
            return self.data_type_array
        else:
            raise ValueError(f"Invalid dtype: {dtype}")

    def format_value(self, value: bool | int | float | str | dict | list) -> str:
        if type(value) is bool:  # if boolean, format as 1 or 0
            return "1" if value else "0"
        if type(value) is float:  # if float, format as float with 3 decimal places
            return f"{value:.3f}"
        if (
            type(value) is dict or type(value) is list
        ):  # if object or array, format as json
            return json.dumps(value, separators=(",", ":"))
        return str(value)  # if string, integer, or other, format as string

    def get_param_value_str(self, value: bool | int | float | str | dict | list) -> str:
        return f"{self.__get_dtype_char(type(value))}{self.format_value(value)}"

    def parse_param_value(
        self, value_str: str
    ) -> bool | int | float | str | dict | list | None:
        try:
            dtype = value_str[0]
            value_str = value_str[1:]
            if dtype == self.data_type_int:
                return int(value_str)
            elif dtype == self.data_type_float:
                return float(value_str)
            elif dtype == self.data_type_boolean:
                return value_str == "1"
            elif dtype == self.data_type_string:
                return value_str
            elif dtype == self.data_type_object:
                return dict(json.loads(value_str))
            elif dtype == self.data_type_array:
                return list(json.loads(value_str))
            else:
                return None
        except Exception:
            return None

    def parse_param_properties(self, properties_str: str) -> list[str]:
        properties: list[str] = []

        for char in properties_str:
            if char in self.property_param_mapping:
                properties.append(self.property_param_mapping[char])

        return properties

    def parse_param_properties_long_names(self, properties: list[str]) -> str:
        prop_chars = []
        for prop in properties:
            if prop in self.property_param_mapping_reverse:
                prop_chars.append(self.property_param_mapping_reverse[prop])

        return "".join(prop_chars)

    def get_return_code_message(self, return_code: str) -> str:
        return self.response_message_mapping.get(
            return_code, f"Unknown return code: '{return_code}'"
        )

    def get_flag_long_name(self, flag: str) -> str:
        return self.flag_mapping.get(flag, f"Unknown flag: '{flag}'")

    def get_local_config_key_long_name(self, key: str) -> str:
        return self.local_config_key_mapping.get(
            key, f"Unknown local config key: '{key}'"
        )

    def get_cloud_control_event_long_name(self, event: str) -> str:
        return self.cloud_control_event_mapping.get(
            event, f"Unknown cloud control event: '{event}'"
        )


class Command:
    """
    Represents a command to the host control.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        """
        {abstract} implemented by subclasses
        """
        raise NotImplementedError("Subclasses must implement this method")

    def log(self, protocol: CommunicationProtocol) -> None:
        """
        {abstract} implemented by subclasses
        """
        raise NotImplementedError("Subclasses must implement this method")


class Response:
    """
    Represents a response from the host control.
    """

    def __init__(self, protocol: CommunicationProtocol, return_str: str):
        self.protocol = protocol

        if len(return_str) < 1:
            self.return_code = None
        else:
            self.return_code = return_str[0]

        if len(return_str) > 1:
            self.payload = return_str[1:]
        else:
            self.payload = None

        self.is_ok = self.return_code == protocol.response_ok

    def log(self) -> None:
        """
        Log the response.
        """
        log_str = f"<- Response: {ColorFormatter.format(self.protocol.get_return_code_message(self.return_code), bcolors.OKGREEN if self.is_ok else bcolors.FAIL)}"
        if self.payload is not None and len(self.payload) > 0:
            log_str += f" | {ColorFormatter.format(self.payload, bcolors.OKCYAN)}"
        print(log_str)


class PortManager:
    """
    Manages the serial port for the host control.
    """

    def __init__(
        self,
        port: str,
        baudrate: int,
        protocol: CommunicationProtocol,
        timeout: Optional[float] = None,
    ):
        self.port = port
        self.baudrate = baudrate
        self.protocol = protocol
        self.encoding = "ascii"
        self.connection: Serial = None
        self.timeout = timeout

    def connect(self) -> bool:
        """
        Connect to the serial port.
        """
        raise NotImplementedError(
            "Subclasses must implement the connect method, and set self.connection to the connection"
        )

    def disconnect(self) -> bool:
        """
        Disconnect from the serial port.
        """
        raise NotImplementedError(
            "Subclasses must implement the disconnect method, and set self.connection to None"
        )

    def _send_command(self, command: Command) -> Response:
        """
        Send a command to the host control.
        """
        command_str = f"{command.build(self.protocol)}{self.protocol.end_char}"
        command_bytes = command_str.encode(self.encoding)
        self.connection.write(command_bytes)
        self.connection.flush()  # Ensure command is sent immediately

    def send_command(self, command: Command, timeout_ms: int = 2000) -> Response:
        """
        {abstract} implemented by subclasses
        """
        raise NotImplementedError("Subclasses must implement this method")

    def __getstate__(self) -> dict:
        """
        Ensure the mux and serial FD are not serialized across processes.
        """
        was_connected = bool(getattr(self, "connection", None))
        try:
            self.disconnect()
        except Exception:
            pass
        state = self.__dict__.copy()
        # Ensure connection is dropped even if base implementation changes.
        state["connection"] = None
        # If we were connected, let the PortManager re-open itself on unpickle (safe re-animation).
        try:
            state["_resume_on_unpickle"] = was_connected
        except Exception:
            state["_resume_on_unpickle"] = False
        return state

    def __setstate__(self, state: dict):
        resume = bool(state.pop("_resume_on_unpickle", False))
        self.__dict__.update(state)
        # Connection is always rebuilt from connect() state.
        self.connection = None
        if resume:
            self.connect()


class PortManagerSingle(PortManager):
    """
    Manages the serial port for the host control.
    """

    def __init__(
        self,
        port: str,
        baudrate: int,
        protocol: CommunicationProtocol,
        timeout: Optional[float] = None,
    ):
        super().__init__(port, baudrate, protocol, timeout=timeout)
        self.connection = None

    def connect(self) -> bool:
        if not self.connection:
            self.connection = Serial(self.port, self.baudrate, timeout=self.timeout)
        return True

    def disconnect(self) -> bool:
        if self.connection:
            self.connection.close()
            self.connection = None
        return True

    def send_command(self, command: Command, timeout_ms: int = 2000) -> Response:
        self.connection.reset_input_buffer()
        self._send_command(command)

        # Wait for the response
        timeout_s = timeout_ms / 1000
        old_timeout = self.connection.timeout
        self.connection.timeout = timeout_s
        start_time = time()
        line = None
        response = None
        while not line:
            if time() - start_time > timeout_s:
                # Timeout
                response = Response(self.protocol, self.protocol.response_timeout)
                break
            line = (
                self.connection.read_until(self.protocol.end_char.encode(self.encoding))
                .decode(self.encoding)
                .replace(self.protocol.end_char, "")
            )
            if line:
                response = Response(self.protocol, line)
                break

        # Restore the original timeout
        self.connection.timeout = old_timeout
        return response


class PortManagerMultiplexed(PortManager):
    """
    Manages the serial port for the host control.
    Uses a UART multiplexer to split the input stream into monitor and remote streams.
    """

    def __init__(
        self,
        port: str,
        baudrate: int,
        protocol: CommunicationProtocol,
        timeout: Optional[float] = None,
    ):
        super().__init__(port, baudrate, protocol, timeout=timeout)
        self.mux = UARTMultiplexer(port, baudrate, timeout=timeout)
        self.connection = None

    def connect(self) -> bool:
        if not self.connection:
            try:
                self.mux.start()
                self.connection = self.mux.get_connection()
                return True
            except Exception as e:
                print(f"Error starting UART multiplexer: {e}")
                return False
        return True

    def disconnect(self) -> bool:
        if self.connection:
            self.mux.stop()
            self.connection = None
        return True

    def send_command(self, command: Command, timeout_ms: int = 2000) -> Response:
        """
        Send a command to the host control.
        """
        self.mux.reset_remote_input()
        self._send_command(command)

        # Wait for the response
        timeout_s = timeout_ms / 1000
        old_timeout = self.mux.remote_buffer.timeout
        self.mux.remote_buffer.timeout = timeout_s
        start_time = time()
        line = None
        response = None
        while not line:
            if time() - start_time > timeout_s:
                # Timeout
                response = Response(self.protocol, self.protocol.response_timeout)
                break
            read_line = self.mux.read_remote_until(
                self.protocol.end_char.encode(self.encoding)
            )
            if read_line:
                line = read_line.decode(self.encoding).replace(
                    self.protocol.end_char, ""
                )
                if line:
                    response = Response(self.protocol, line)
                    break

        # Restore the original timeout
        self.mux.remote_buffer.timeout = old_timeout
        return response

    def get_monitor_stream(self) -> IOBase:
        """
        Get the monitor stream.
        """
        return self.mux.get_monitor_stream()

    def get_remote_stream(self) -> IOBase:
        """
        Get the remote stream.
        """
        return self.mux.get_remote_stream()

    def read_monitor_line(self, terminator: bytes = b"\n") -> Optional[bytes]:
        """
        Read a line from the monitor stream.
        """
        return self.mux.read_monitor_until(terminator)
