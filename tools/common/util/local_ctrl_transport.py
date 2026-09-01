# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Transports for the local-control endpoints.

The endpoints are plain protocomm handlers, so the same protobuf payloads run
over HTTP and over BLE GATT. Only the session/version endpoint names, the
version JSON layout and the addressing differ.
"""

import asyncio
import json
from typing import Any, Optional

import requests

from util.esp_prov_env import import_esp_prov

# Service UUID advertised by RainMaker nodes. The client prefers the UUID it
# reads from the advertisement and uses this only as a fallback.
RMAKER_BLE_SERVICE_UUID = "021a9004-0382-4aea-bff4-6b3f1c5adfb4"

# None selects the system's default adapter. Only BlueZ names an adapter, and
# passing one on any other platform raises a deprecation warning from bleak.
DEFAULT_BLE_ADAPTER = None

# (session endpoint, version endpoint, security JSON key, capability JSON key).
# The first scheme whose session endpoint the device exposes wins.
_BLE_ENDPOINT_SCHEMES = (
    ("prov-session", "proto-ver", "prov", "rmaker_extra"),
    (
        "rmaker_local_ctrl/session",
        "rmaker_local_ctrl/version",
        "rmaker_local_ctrl",
        "rmaker_local_ctrl",
    ),
)


class TransportError(Exception):
    """Transport-level failure. The caller may re-resolve and retry."""


class LocalCtrlTransport:
    """Endpoint-addressed request/response channel."""

    session_endpoint: str = ""
    version_endpoint: str = ""
    sec_info_key: str = ""
    cap_info_key: str = ""
    version_probe: bytes = b"none"

    def __init__(self, log=None):
        self._log = log if log is not None else (lambda _message: None)

    def request(self, endpoint: str, payload: bytes) -> Optional[bytes]:
        raise NotImplementedError

    def read_version_info(self) -> Optional[dict]:
        """
        Read the version endpoint.

        Returns:
            dict: {"sec_ver": int, "sec_patch_ver": int, "cap": list} or None
        """
        try:
            raw = self.request(self.version_endpoint, self.version_probe)
            if not raw:
                return None
            data = json.loads(raw.decode("utf-8"))
        except Exception as e:
            self._log(f"Failed to read {self.version_endpoint}: {e}")
            return None
        self._log(
            f"{self.version_endpoint} response: {json.dumps(data, sort_keys=True)}"
        )

        sec_info = data.get(self.sec_info_key) or {}
        cap_info = data.get(self.cap_info_key) or {}
        return {
            "sec_ver": sec_info.get("sec_ver", 0),
            "sec_patch_ver": sec_info.get("sec_patch_ver", 0),
            "cap": list(cap_info.get("cap", [])),
        }

    def close(self) -> None:
        pass


class HttpTransport(LocalCtrlTransport):
    """Local-control endpoints over the node's HTTP server."""

    session_endpoint = "rmaker_local_ctrl/session"
    version_endpoint = "rmaker_local_ctrl/version"
    sec_info_key = "rmaker_local_ctrl"
    cap_info_key = "rmaker_local_ctrl"
    version_probe = b"none"

    def __init__(self, http_session: requests.Session, base_url: str, log=None):
        super().__init__(log=log)
        self.http_session = http_session
        self.base_url = base_url

    def request(self, endpoint: str, payload: bytes) -> Optional[bytes]:
        url = f"{self.base_url}/{endpoint}"
        headers = {
            "Content-Type": "application/octet-stream",
            "Content-Length": str(len(payload)),
        }
        self._log(f"Sending request to {url}")
        try:
            response = self.http_session.post(
                url, data=payload, headers=headers, timeout=10
            )
        except requests.RequestException as e:
            raise TransportError(str(e)) from e

        self._log(f"Response status: {response.status_code}")
        if response.status_code == 200:
            return response.content
        self._log(
            f"Local control request failed with status {response.status_code}: {response.text}"
        )
        return None

    def globalize_session_cookies(self) -> None:
        """Apply the protocomm session cookie to every path."""
        for cookie in self.http_session.cookies:
            if cookie.name == "session":
                self._log(f"Setting session cookie for all requests: {cookie.value}")
                cookie.path = "/"

    def close(self) -> None:
        try:
            self.http_session.close()
        except Exception as e:
            self._log(f"Error closing HTTP session: {e}")


class BleTransport(LocalCtrlTransport):
    """Local-control endpoints over protocomm's BLE GATT transport."""

    version_probe = b"---"

    def __init__(
        self,
        devname: Optional[str] = None,
        adapter: Optional[str] = DEFAULT_BLE_ADAPTER,
        log=None,
    ):
        super().__init__(log=log)
        self.devname = devname
        self.adapter = adapter
        self.endpoints: dict[str, str] = {}
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._tp: Any = None

    def connect(self) -> dict:
        """
        Scan, connect and resolve the device's endpoint map.

        Returns:
            dict: {"devname": str, "endpoints": list[str]}
        """
        try:
            _security, transport_mod = import_esp_prov()
        except Exception as e:
            raise TransportError(f"Could not load esp_prov: {e}") from e

        # esp_prov falls back to an interactive console client without bleak.
        if getattr(transport_mod.ble_cli, "fallback", False):
            raise TransportError(
                "BLE support needs the 'bleak' package. Install it with "
                "'pip install -r tools/requirements.txt'."
            )

        # An empty lookup keeps ble_cli's all-or-nothing check satisfied, so it
        # always returns the full descriptor-derived map.
        self._tp = transport_mod.Transport_BLE(
            service_uuid=RMAKER_BLE_SERVICE_UUID,
            nu_lookup={},
            ble_adapter=self.adapter,
        )
        # run_until_complete makes this the running loop, which is what bleak
        # reads; the thread's global loop stays untouched.
        self._loop = asyncio.new_event_loop()
        try:
            self._loop.run_until_complete(self._tp.connect(devname=self.devname))
        except SystemExit as e:
            # esp_prov's ble_cli calls exit(1) when a scan finds no device.
            self.close()
            raise TransportError("BLE connect failed: no device found") from e
        except Exception as e:
            self.close()
            raise TransportError(f"BLE connect failed: {e}") from e

        discovered = self._tp.cli.get_nu_lookup()
        if not discovered:
            self.close()
            raise TransportError(
                "Could not read the endpoint names from the device's GATT "
                "characteristic descriptors. The endpoint UUIDs are assigned "
                "dynamically, so they cannot be guessed."
            )

        self._tp.name_uuid_lookup = discovered
        self.endpoints = discovered
        self.devname = self._tp.cli.devname

        for session_ep, version_ep, sec_key, cap_key in _BLE_ENDPOINT_SCHEMES:
            if session_ep in discovered:
                self.session_endpoint = session_ep
                self.version_endpoint = version_ep
                self.sec_info_key = sec_key
                self.cap_info_key = cap_key
                break
        else:
            names = ", ".join(sorted(discovered)) or "none"
            self.close()
            raise TransportError(
                f"Device '{self.devname}' exposes no known session endpoint. Found: {names}"
            )

        self._log(
            f"BLE connected to '{self.devname}' "
            f"(session={self.session_endpoint}, endpoints={sorted(discovered)})"
        )
        return {"devname": self.devname, "endpoints": sorted(discovered)}

    def request(self, endpoint: str, payload: bytes) -> Optional[bytes]:
        if self._loop is None or self._tp is None:
            raise TransportError("BLE transport is not connected")
        if endpoint not in self.endpoints:
            self._log(f"Endpoint '{endpoint}' is not exposed by the device")
            return None
        self._log(f"Sending request to BLE endpoint {endpoint}")
        try:
            response = self._loop.run_until_complete(
                self._tp.send_data(endpoint, payload.decode("latin-1"))
            )
        except Exception as e:
            raise TransportError(f"BLE request to '{endpoint}' failed: {e}") from e
        if response is None:
            return None
        return response.encode("latin-1")

    def close(self) -> None:
        if self._loop is not None:
            try:
                if self._tp is not None:
                    self._loop.run_until_complete(self._tp.disconnect())
            except Exception as e:
                self._log(f"Error disconnecting BLE transport: {e}")
            finally:
                try:
                    self._loop.close()
                except Exception:
                    pass
        self._loop = None
        self._tp = None
        self.endpoints = {}
