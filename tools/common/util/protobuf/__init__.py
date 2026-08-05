# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
ESP Local Control Protocol Wrapper

Simple wrapper for ESP Local Control protobuf messages.

Based on the ESP-IDF esp_local_ctrl component.
"""

from typing import Optional
from enum import Enum
from . import def_pb2
from . import local_ctrl_pb2
import json


class Status(Enum):
    """Status codes from the ESP Local Control protocol."""

    Success = 0
    InvalidSecScheme = 1
    InvalidProto = 2
    TooManySessions = 3
    InvalidArgument = 4
    InternalError = 5
    CryptoError = 6
    InvalidSession = 7


class Response:
    """Base response class with status."""

    def __init__(self, status: Status):
        self.status = status

    def __repr__(self):
        return f"{self.__class__.__name__}(status={self.status})"


class GetResponse(Response):
    """Response for get property operations."""

    def __init__(
        self,
        status: Status,
        name: str,
        value: bytes,
        prop_type: int = 0,
        flags: int = 0,
    ):
        super().__init__(status)
        self.name = name
        self.value = value
        self.prop_type = prop_type
        self.flags = flags

    def __repr__(self):
        return f"GetResponse(status={self.status}, name='{self.name}', value={self.value!r})"


class SetResponse(Response):
    """Response for set property operations."""

    def __init__(self, status: Status):
        super().__init__(status)


class ChalRespStatus(Enum):
    """Status codes from the RMaker challenge-response protocol."""

    Success = 0
    Fail = 1
    InvalidParam = 2
    Disabled = 3


class ChalRespResponse:
    """Base response for challenge-response operations."""

    def __init__(self, status: ChalRespStatus):
        self.status = status

    def __repr__(self):
        return f"{self.__class__.__name__}(status={self.status})"


class ChalRespChallengeResponse(ChalRespResponse):
    """Response containing signed challenge payload and node ID."""

    def __init__(self, status: ChalRespStatus, payload: bytes = b"", node_id: str = ""):
        super().__init__(status)
        self.payload = payload
        self.node_id = node_id


class ChalRespGetNodeIDResponse(ChalRespResponse):
    """Response containing node ID."""

    def __init__(self, status: ChalRespStatus, node_id: str = ""):
        super().__init__(status)
        self.node_id = node_id


class CommandManager:
    """Shared command manager utilities for protobuf serialization and transport."""

    @staticmethod
    def _to_bytes(data):
        """Normalize transport payload into bytes for protobuf parsing."""
        if isinstance(data, str):
            return data.encode("latin-1")
        return data

    @staticmethod
    def _serialize_for_transport(serialized: bytes, security_ctx=None):
        """Serialize bytes for transport, optionally encrypting first."""
        if security_ctx:
            serialized = security_ctx.encrypt_data(serialized)
        return serialized.decode("latin-1")

    def _deserialize_from_transport(self, data, security_ctx=None):
        """Deserialize transport payload into protobuf-ready bytes."""
        data = self._to_bytes(data)
        if security_ctx:
            data = security_ctx.decrypt_data(data)
        return data


class LocalCtrlCommandManager(CommandManager):
    """
    Manager for the local control endpoint protocol.

    Creates and parses the protobuf messages served on the get_params /
    get_config endpoints (see docs/spec/local_ctrl_endpoint_protocol.md);
    set_params carries raw JSON.
    """

    _DATA_TYPES = {
        "params": local_ctrl_pb2.TypeParams,
        "config": local_ctrl_pb2.TypeConfig,
    }

    # Endpoint-protocol status -> session Status (Success maps 1:1; the rest
    # collapse onto the closest generic status).
    _STATUS_MAP = {
        local_ctrl_pb2.Success: Status.Success,
        local_ctrl_pb2.Fail: Status.InternalError,
        local_ctrl_pb2.InvalidParam: Status.InvalidArgument,
        local_ctrl_pb2.NoMemory: Status.InternalError,
    }

    def cmd_get_data(self, data_type: str, offset: int, security_ctx=None):
        """
        Create a CmdGetData message for the get_params / get_config endpoints.

        Args:
            data_type: "params" or "config"
            offset: Byte offset of the requested fragment (0 (re)generates)
            security_ctx: Optional security context for encryption

        Returns:
            Serialized message as latin-1 string (encrypted if security_ctx provided)
        """
        msg = local_ctrl_pb2.RMakerLocalCtrlPayload()
        msg.msg = local_ctrl_pb2.TypeCmdGetData
        msg.cmdGetData.DataType = self._DATA_TYPES[data_type]
        msg.cmdGetData.Offset = offset
        serialized = msg.SerializeToString()
        return self._serialize_for_transport(serialized, security_ctx)

    def parse_get_data_response(self, data, security_ctx=None):
        """
        Parse a RespGetData fragment.

        Returns:
            (status, offset, payload_bytes, total_len) or None if parsing failed
        """
        try:
            data = self._to_bytes(data)
            if security_ctx:
                data = security_ctx.decrypt_data(data)
            msg = local_ctrl_pb2.RMakerLocalCtrlPayload()
            msg.ParseFromString(data)
            if msg.msg != local_ctrl_pb2.TypeRespGetData or not msg.HasField(
                "respGetData"
            ):
                return None
            resp = msg.respGetData
            status = self._STATUS_MAP.get(resp.Status, Status.InternalError)
            return (status, resp.Buf.Offset, bytes(resp.Buf.Payload), resp.Buf.TotalLen)
        except Exception:
            return None

    def cmd_set_params(self, params_json: bytes, security_ctx=None):
        """
        Create a set_params request (raw JSON body, optionally encrypted).
        """
        return self._serialize_for_transport(params_json, security_ctx)

    def parse_set_params_response(
        self, data, security_ctx=None
    ) -> Optional[SetResponse]:
        """
        Parse the set_params JSON status response into a SetResponse.
        """
        try:
            data = self._to_bytes(data)
            if security_ctx:
                data = security_ctx.decrypt_data(data)
            result = json.loads(data.decode("utf-8"))
            if result.get("status") == "success":
                return SetResponse(status=Status.Success)
            return SetResponse(status=Status.InternalError)
        except Exception:
            return None


class ChalRespCommandManager(CommandManager):
    """Manager for RMaker challenge-response protobuf commands."""

    def _build_message(
        self, msg_type, payload_field=None, payload_message=None, security_ctx=None
    ):
        msg = def_pb2.RMakerChRespPayload()
        msg.msg = msg_type
        if payload_field is not None and payload_message is not None:
            getattr(msg, payload_field).CopyFrom(payload_message)
        serialized = msg.SerializeToString()
        return self._serialize_for_transport(serialized, security_ctx)

    def challenge_response(self, payload: bytes, security_ctx=None):
        """Create challenge-response command message."""
        cmd = def_pb2.CmdCRPayload()
        cmd.payload = payload
        return self._build_message(
            def_pb2.TypeCmdChallengeResponse,
            payload_field="cmdChallengeResponsePayload",
            payload_message=cmd,
            security_ctx=security_ctx,
        )

    def get_node_id(self, security_ctx=None):
        """Get-node-id is not supported by the current chal_resp endpoint implementation."""
        raise NotImplementedError(
            "get_node_id is not supported by chal_resp endpoint. "
            "Supported commands are challenge_response and disable."
        )

    def disable(self, security_ctx=None):
        """Create disable challenge-response command message."""
        cmd = def_pb2.CmdDisableChalRespPayload()
        return self._build_message(
            def_pb2.TypeCmdDisableChalResp,
            payload_field="cmdDisableChalRespPayload",
            payload_message=cmd,
            security_ctx=security_ctx,
        )

    def parse_response(
        self, data: bytes, security_ctx=None
    ) -> Optional[ChalRespResponse]:
        """Parse challenge-response protobuf response."""
        try:
            data = self._deserialize_from_transport(data, security_ctx)
            msg = def_pb2.RMakerChRespPayload()
            msg.ParseFromString(data)
            status = ChalRespStatus(msg.status)
            if msg.msg == def_pb2.TypeRespChallengeResponse:
                payload = b""
                node_id = ""
                if msg.HasField("respChallengeResponsePayload"):
                    payload = msg.respChallengeResponsePayload.payload
                    node_id = msg.respChallengeResponsePayload.node_id
                return ChalRespChallengeResponse(
                    status=status, payload=payload, node_id=node_id
                )
            if msg.msg == def_pb2.TypeRespGetNodeID:
                node_id = ""
                if msg.HasField("respGetNodeIDPayload"):
                    node_id = msg.respGetNodeIDPayload.node_id
                return ChalRespGetNodeIDResponse(status=status, node_id=node_id)
            if msg.msg == def_pb2.TypeRespDisableChalResp:
                return ChalRespResponse(status=status)
        except Exception:
            return None
        return None


# Create a default instance for easy access
_default_manager = LocalCtrlCommandManager()
_default_chal_resp_manager = ChalRespCommandManager()


def challenge_response(payload: bytes, security_ctx=None):
    """Create protobuf message for challenge-response command."""
    return _default_chal_resp_manager.challenge_response(payload, security_ctx)


def get_node_id(security_ctx=None):
    """Create protobuf message for get-node-id command."""
    return _default_chal_resp_manager.get_node_id(security_ctx)


def disable_chal_resp(security_ctx=None):
    """Create protobuf message for disable challenge-response command."""
    return _default_chal_resp_manager.disable(security_ctx)


def parse_chal_resp_response(
    data: bytes, security_ctx=None
) -> Optional[ChalRespResponse]:
    """Parse challenge-response response protobuf."""
    return _default_chal_resp_manager.parse_response(data, security_ctx)


# Export the main classes and functions
__all__ = [
    "Status",
    "Response",
    "GetResponse",
    "SetResponse",
    "ChalRespStatus",
    "ChalRespResponse",
    "ChalRespChallengeResponse",
    "ChalRespGetNodeIDResponse",
    "CommandManager",
    "LocalCtrlCommandManager",
    "ChalRespCommandManager",
    "get_property",
    "set_property",
    "parse_response",
    "is_protobuf_available",
    "challenge_response",
    "get_node_id",
    "disable_chal_resp",
    "parse_chal_resp_response",
]
