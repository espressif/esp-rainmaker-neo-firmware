# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from util.protobuf import (
    LocalCtrlCommandManager,
    Status,
    ChalRespCommandManager,
    GetResponse,
    SetResponse,
    ChalRespChallengeResponse,
    ChalRespGetNodeIDResponse,
    ChalRespResponse,
)
from util.esp_prov_env import import_esp_prov
from util.local_ctrl_transport import (
    BleTransport,
    HttpTransport,
    TransportError,
    DEFAULT_BLE_ADAPTER,
)
from zeroconf import Zeroconf, ServiceBrowser, ServiceListener
import json
import socket
import requests

CHAL_RESP_ENDPOINT = "ch_resp"
LOCAL_CTRL_SERVICE_NAME = "_esp_rmaker_ctrl"

# Capabilities a caller selects, and the keys of the per-node state facets. The mDNS
# service type is always LOCAL_CTRL_SERVICE_NAME, since one service carries both.
#
# These are NOT the `cap` TXT values — for challenge-response the two differ. Compare
# against TXT_CAP_* below when reading a TXT record.
CAPABILITY_LOCAL_CTRL = "local_ctrl"
CAPABILITY_CHAL_RESP = "chal_resp"

# Tokens the device puts in the `cap` TXT record (LOCAL_CTRL_TXT_CAP_* on the firmware
# side). Note ch_resp, not chal_resp.
TXT_CAP_LOCAL_CTRL = "local_ctrl"
TXT_CAP_CHAL_RESP = CHAL_RESP_ENDPOINT
# SRP6a username for SEC2. Fixed to "wifiprov" for parity with the device
# (which hardcodes the same value) and ESP-IDF unified provisioning.
SEC2_USERNAME = "wifiprov"

# Transport discriminators stored on each session and facet.
PROTOCOL_TCP = "tcp"
PROTOCOL_BLE = "ble"


class LocalController:
    """
    A class that represents a local controller.
    """

    def __init__(self, logging=False):
        """
        Initialize the local controller.
        """
        # Initialize local control manager and sessions
        self._local_ctrl_manager = LocalCtrlCommandManager()
        self._chal_resp_manager = ChalRespCommandManager()
        self._local_ctrl_sessions = {}  # Will store {'node:port': {'http_session': session, 'security_ctx': ctx, 'ip_address': ip, 'txt_records': txt}}
        self._node_service_state = {}  # Per-node service availability cache for local_ctrl/chal_resp
        self._zeroconf = None  # Lazy initialization of Zeroconf instance
        self._logging = logging

    def _get_node_state_label(self, local_available, chal_available):
        if local_available and chal_available:
            return "Local Control + Challenge-Response"
        if local_available:
            return "Local Control Only"
        if chal_available:
            return "Challenge-Response Only"
        return "Unavailable"

    def _upsert_session_from_service(self, node_id, service_type, protocol, service):
        ip_address = service.get("ip")
        service_port = service.get("port")
        txt_records = service.get("txt_records", {})
        if not ip_address or service_port is None:
            return None, None

        session_key = f"{node_id}:{service_port}"
        if session_key not in self._local_ctrl_sessions:
            self._local_ctrl_sessions[session_key] = {
                "http_session": requests.Session(),
                "transport": None,
                "security_ctx": None,
                "ip_address": None,
                "txt_records": {},
                "mdns_service_type": service_type,
                "mdns_protocol": protocol,
                "mdns_port": None,
            }

        session_data = self._local_ctrl_sessions[session_key]
        session_data["ip_address"] = ip_address
        session_data["txt_records"] = txt_records
        session_data["mdns_service_type"] = service_type
        session_data["mdns_protocol"] = protocol
        session_data["mdns_port"] = service_port
        if not session_data.get("http_session"):
            session_data["http_session"] = requests.Session()
        base_url = f"http://{self._format_ip_for_url(ip_address)}:{service_port}"
        session_data["transport"] = HttpTransport(
            session_data["http_session"], base_url, log=self._log
        )
        return session_key, session_data

    def discover_sessions(self, node_id, timeout=5, force_refresh=False):
        """
        Discover the node's local endpoints service and derive the available
        capabilities (local control / challenge-response) from its `cap` TXT
        record.
        """
        if not force_refresh and node_id in self._node_service_state:
            return self._node_service_state[node_id]

        service = self.resolve_mdns_service(
            node_id, LOCAL_CTRL_SERVICE_NAME, "tcp", timeout=timeout
        )
        available = bool(
            service and service.get("ip") and service.get("port") is not None
        )
        txt = service.get("txt_records", {}) if service else {}
        caps = [c.strip() for c in txt.get("cap", "").split(",") if c.strip()]
        local_available = available and TXT_CAP_LOCAL_CTRL in caps
        chal_available = available and TXT_CAP_CHAL_RESP in caps

        session_key = None
        if available:
            session_key, _ = self._upsert_session_from_service(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp", service
            )

        def _facet(feature_available):
            return {
                "available": feature_available,
                "service_type": LOCAL_CTRL_SERVICE_NAME,
                "protocol": "tcp",
                "session_key": session_key if feature_available else None,
                "ip": service.get("ip") if service else None,
                "port": service.get("port") if service else None,
                "txt_records": txt,
            }

        state = {
            "node_id": node_id,
            "state": self._get_node_state_label(local_available, chal_available),
            CAPABILITY_LOCAL_CTRL: _facet(local_available),
            CAPABILITY_CHAL_RESP: _facet(chal_available),
        }
        self._node_service_state[node_id] = state
        self._log(
            f"Discovered node state for {node_id}: {state['state']} "
            f"(local_ctrl={local_available}, ch_resp={chal_available})"
        )
        return state

    def discover_ble_session(
        self, devname=None, adapter=DEFAULT_BLE_ADAPTER, force_refresh=False
    ):
        """
        Connect to a BLE node and derive its available capabilities.

        The device is addressed by its advertised name, which also keys the
        session. Capabilities come from the version endpoint, since BLE carries
        no mDNS TXT record.

        Args:
            devname (str | None): Advertised BLE name. None lets the user pick.
            adapter (str): BLE adapter name (BlueZ only)
            force_refresh (bool): Reconnect even when a session is cached

        Returns:
            dict: Node state, in the same shape discover_sessions returns

        Raises:
            TransportError: The device is unreachable or exposes no known endpoints
        """
        if devname and not force_refresh and devname in self._node_service_state:
            return self._node_service_state[devname]

        # A connected node may stop advertising, so drop the old connection before the scan.
        for key in list(self._local_ctrl_sessions):
            if key.endswith(f":{PROTOCOL_BLE}") and (
                devname is None or key == f"{devname}:{PROTOCOL_BLE}"
            ):
                self._close_session(key)

        transport = BleTransport(devname=devname, adapter=adapter, log=self._log)
        transport.connect()
        node_id = transport.devname

        version_info = transport.read_version_info()
        if version_info is None:
            transport.close()
            raise TransportError(
                f"Could not read the version endpoint '{transport.version_endpoint}'"
            )

        caps = version_info["cap"]
        local_available = TXT_CAP_LOCAL_CTRL in caps
        chal_available = TXT_CAP_CHAL_RESP in caps
        if not caps:
            # Older firmware may not report capabilities; fall back to the
            # endpoint names read from the GATT descriptors.
            endpoints = transport.endpoints
            local_available = "get_params" in endpoints or "get_config" in endpoints
            chal_available = CHAL_RESP_ENDPOINT in endpoints

        session_key = f"{node_id}:{PROTOCOL_BLE}"
        self._local_ctrl_sessions[session_key] = {
            "http_session": None,
            "transport": transport,
            "security_ctx": None,
            "ip_address": None,
            "txt_records": {},
            "mdns_service_type": LOCAL_CTRL_SERVICE_NAME,
            "mdns_protocol": PROTOCOL_BLE,
            "mdns_port": None,
            "ble_devname": node_id,
            "ble_endpoints": sorted(transport.endpoints),
            "sec_ver": version_info["sec_ver"],
            "sec_patch_ver": version_info["sec_patch_ver"],
        }

        def _facet(feature_available):
            return {
                "available": feature_available,
                "service_type": LOCAL_CTRL_SERVICE_NAME,
                "protocol": PROTOCOL_BLE,
                "session_key": session_key if feature_available else None,
                "ip": None,
                "port": None,
                "txt_records": {},
            }

        state = {
            "node_id": node_id,
            "state": self._get_node_state_label(local_available, chal_available),
            CAPABILITY_LOCAL_CTRL: _facet(local_available),
            CAPABILITY_CHAL_RESP: _facet(chal_available),
        }
        self._node_service_state[node_id] = state
        self._log(
            f"Discovered BLE node state for {node_id}: {state['state']} "
            f"(local_ctrl={local_available}, ch_resp={chal_available})"
        )
        return state

    def get_cached_node_state(self, node_id):
        return self._node_service_state.get(node_id)

    def _log(self, message):
        """
        Log a message.
        """
        if self._logging:
            print(f"[LocalController] {message}")

    def _log_bytes(self, tag, bytes):
        """
        Log a message with the bytes in hex format.
        """
        if self._logging:
            hex_str = bytes.hex()
            if len(hex_str) > 100:
                hex_str = hex_str[:100] + "..."
            print(f"[LocalController] {tag} ({len(bytes)} bytes): {hex_str}")

    def resolve_mdns_service(self, node_id, service_type, protocol="tcp", timeout=5):
        """
        Resolve an mDNS service for a node by matching TXT record "node_id".

        Args:
            node_id (str): Node ID (thing name)
            service_type (str): Service type (for example "_esp_rmaker_ctrl" or full type)
            protocol (str): Service protocol (for example "tcp" or "_tcp")
            timeout (int): Timeout in seconds for mDNS resolution

        Returns:
            dict: {"ip": str|None, "txt_records": dict, "port": int|None} or None if failed
        """
        try:
            if self._zeroconf is None:
                self._zeroconf = Zeroconf()

            normalized_service_type = service_type.strip()
            if normalized_service_type.endswith(".local."):
                full_service_type = normalized_service_type
            else:
                normalized_protocol = protocol.strip().lstrip("_")
                if normalized_protocol not in ("tcp", "udp"):
                    self._log(f"Invalid mDNS protocol '{protocol}', defaulting to tcp")
                    normalized_protocol = "tcp"
                full_service_type = (
                    f"{normalized_service_type}._{normalized_protocol}.local."
                )

            self._log(
                f"Browsing mDNS service {full_service_type} for node_id={node_id}..."
            )

            import threading

            resolved_event = threading.Event()
            resolved_service = [{"ip": None, "txt_records": {}, "port": None}]
            node_id_lower = node_id.lower()

            class MDNSListener(ServiceListener):
                def add_service(self, zc, service_type, name):
                    if resolved_event.is_set():
                        return

                    info = zc.get_service_info(service_type, name)
                    if not info:
                        return

                    txt_records = {}
                    if info.properties:
                        for key, value in info.properties.items():
                            k = (
                                key.decode("utf-8", errors="ignore")
                                if isinstance(key, bytes)
                                else str(key)
                            )
                            v = (
                                value.decode("utf-8", errors="ignore")
                                if isinstance(value, bytes)
                                else str(value)
                            )
                            txt_records[k] = v

                    service_node_id = txt_records.get("node_id", "").strip().lower()
                    if not service_node_id or service_node_id != node_id_lower:
                        return

                    resolved_ip = None
                    if hasattr(info, "parsed_scoped_addresses"):
                        parsed_scoped = info.parsed_scoped_addresses()
                        if parsed_scoped:
                            resolved_ip = parsed_scoped[0].split("%")[0]
                    if not resolved_ip and hasattr(info, "parsed_addresses"):
                        parsed_addresses = info.parsed_addresses()
                        if parsed_addresses:
                            resolved_ip = parsed_addresses[0]
                    if not resolved_ip and info.addresses:
                        for addr in info.addresses:
                            if len(addr) == 4:
                                resolved_ip = socket.inet_ntoa(addr)
                                break
                            if len(addr) == 16:
                                resolved_ip = socket.inet_ntop(socket.AF_INET6, addr)
                                break

                    resolved_service[0] = {
                        "ip": resolved_ip,
                        "txt_records": txt_records,
                        "port": int(info.port) if info.port else None,
                    }
                    resolved_event.set()

                def update_service(self, zc, service_type, name):
                    self.add_service(zc, service_type, name)

            listener = MDNSListener()
            browser = ServiceBrowser(self._zeroconf, full_service_type, listener)
            if resolved_event.wait(timeout):
                browser.cancel()
                resolved = resolved_service[0]
                self._log(
                    f"Resolved {full_service_type} for node_id={node_id}: "
                    f"ip={resolved.get('ip')}, port={resolved.get('port')}, txt={resolved.get('txt_records')}"
                )
                return resolved
            browser.cancel()

            self._log(
                f"Failed to resolve service {full_service_type} for node_id={node_id} "
                f"within {timeout} seconds"
            )
            return None

        except Exception as e:
            self._log(f"Error resolving mDNS service for {node_id}: {str(e)}")
            import traceback

            traceback.print_exc()
            return None

    def _get_or_resolve_service_session(
        self,
        node_id,
        capability=CAPABILITY_LOCAL_CTRL,
        protocol="tcp",
        timeout=5,
        force_refresh=False,
    ):
        """
        Get or resolve service metadata/session keyed by mDNS-advertised port.

        Returns:
            tuple[str | None, dict | None]: (session_key, session_data)
        """
        if not force_refresh:
            state = self.get_cached_node_state(node_id)
            if state:
                # The cached facets track per-capability availability; the session
                # itself only needs the service to be present, so fall back to the
                # other facet's metadata (a ch_resp-only node advertises the same
                # service without the local_ctrl capability).
                service_state = state.get(capability, {})
                if not service_state.get("available"):
                    for other in (CAPABILITY_LOCAL_CTRL, CAPABILITY_CHAL_RESP):
                        other_state = state.get(other, {})
                        if other_state.get("available"):
                            service_state = other_state
                            break
                if service_state.get("available"):
                    session_key = service_state.get("session_key")
                    session_data = (
                        self._local_ctrl_sessions.get(session_key)
                        if session_key
                        else None
                    )
                    if session_data and session_data.get("transport") is not None:
                        return session_key, session_data
                else:
                    self._log(
                        f"Service unavailable for node_id={node_id}, capability={capability} (cached)"
                    )
                    return None, None

            for session_key, session_data in self._local_ctrl_sessions.items():
                if not session_key.startswith(f"{node_id}:"):
                    continue
                if (
                    session_data.get("mdns_service_type") == LOCAL_CTRL_SERVICE_NAME
                    and session_data.get("mdns_protocol") == protocol
                    and session_data.get("transport") is not None
                ):
                    return session_key, session_data

        state = self.discover_sessions(node_id, timeout=timeout, force_refresh=True)
        service_state = state.get(capability, {})
        if not service_state.get("available"):
            self._log(
                f"Service unavailable for node_id={node_id}, capability={capability}"
            )
            return None, None

        session_key = service_state.get("session_key")
        session_data = (
            self._local_ctrl_sessions.get(session_key) if session_key else None
        )
        if not session_data:
            service = {
                "ip": service_state.get("ip"),
                "port": service_state.get("port"),
                "txt_records": service_state.get("txt_records", {}),
            }
            session_key, session_data = self._upsert_session_from_service(
                node_id, LOCAL_CTRL_SERVICE_NAME, protocol, service
            )
        return session_key, session_data

    def _format_ip_for_url(self, ip_address):
        """
        Format an IP address for use in URLs.
        IPv6 addresses need to be enclosed in square brackets.

        Args:
            ip_address (str): IP address

        Returns:
            str: Formatted IP address for URL
        """
        if ":" in ip_address:  # IPv6
            return f"[{ip_address}]"
        else:  # IPv4
            return ip_address

    def establish_session(
        self,
        node_id,
        pop_retrieval_fn=None,
        capability=CAPABILITY_LOCAL_CTRL,
        protocol="tcp",
        sec_version=None,
        username_retrieval_fn=None,
    ):
        """
        Establish a local control session, retrieving PoP / username from device shadow.

        Args:
            node_id (str): Node ID (thing name)
            pop_retrieval_fn (function): Function returning PoP string. Used by SEC1 and SEC2
                (in SEC2, PoP is reused as the SRP6a password).
            capability (str): Capability the service must offer —
                CAPABILITY_LOCAL_CTRL or CAPABILITY_CHAL_RESP. Not a `cap` TXT
                value; see TXT_CAP_* for those.
            protocol (str): mDNS service protocol (default "tcp")
            sec_version (int | None): If set, use this security version and skip the HTTP
                version endpoint. If None, POST /rmaker_local_ctrl/version on the resolved
                service. Challenge-response-only nodes should pass the value from mDNS TXT
                ``sec_version``; when local control is also available, prefer None so the
                version endpoint (which reports ``local_ctrl.sec_ver``) is used.
            username_retrieval_fn (function | None): Function returning the SRP6a username
                string. Required when ``sec_ver == 2``.

        Returns:
            bool: True if session established successfully
        """
        try:
            session_key, session_data = self._get_or_resolve_service_session(
                node_id=node_id,
                capability=capability,
                protocol=protocol,
            )
            if not session_data:
                return False
            transport = session_data.get("transport")
            if transport is None:
                return False

            return self._run_security_handshake(
                node_id=node_id,
                session_key=session_key,
                transport=transport,
                pop_retrieval_fn=pop_retrieval_fn,
                username_retrieval_fn=username_retrieval_fn,
                sec_version=sec_version,
            )

        except Exception as e:
            self._log(f"Session establishment failed: {str(e)}")
            return False

    def _run_security_handshake(
        self,
        node_id,
        session_key,
        transport,
        pop_retrieval_fn=None,
        username_retrieval_fn=None,
        sec_version=None,
        sec_patch_version=None,
    ):
        """
        Run the protocomm session-security handshake over the given transport.

        Args:
            sec_version (int | None): Security version. If None, read the transport's
                version endpoint.
            sec_patch_version (int | None): Patch version to pair with sec_version.

        Returns:
            bool: True if the session was established
        """
        sec_patch_ver = sec_patch_version or 0
        if sec_version is None:
            self._log(f"Checking version endpoint for {node_id}...")
            version_info = transport.read_version_info()
            if version_info is None:
                self._log("Failed to read the version endpoint")
                return False
            sec_ver = version_info["sec_ver"]
            sec_patch_ver = version_info["sec_patch_ver"]
            self._log(f"Detected security version: {sec_ver}, patch: {sec_patch_ver}")
        else:
            sec_ver = sec_version
            self._log(
                f"Using security version from caller (not version endpoint): {sec_ver}"
            )

        if sec_ver == 0:
            self._log(f"Using unsecured session (SEC0) for {node_id}")
            self._finish_session(session_key, transport, None)
            return True

        if sec_ver not in (1, 2):
            self._log(f"Unsupported security version: {sec_ver}")
            return False

        # SEC1 and SEC2 both need a PoP; SEC2 additionally needs the SRP6a username.
        pop = pop_retrieval_fn() if pop_retrieval_fn is not None else None

        username = None
        if sec_ver == 2:
            if username_retrieval_fn is None:
                self._log("SEC2 requires username_retrieval_fn but none was provided")
                return False
            username = username_retrieval_fn()
            if not username:
                self._log("SEC2 username retrieval returned empty value")
                return False

        self._log(
            f"Performing SEC{sec_ver} handshake for {node_id} "
            f"(PoP {'provided' if pop else 'not provided'}"
            + (f", username='{username}'" if sec_ver == 2 else "")
            + ")"
        )

        security, _transport_mod = import_esp_prov()
        if sec_ver == 1:
            # Security1 accepts an empty PoP when the device does not require one.
            sec = security.Security1(pop or "", False)
        else:
            # PoP is reused as the SRP6a password.
            sec = security.Security2(sec_patch_ver, username, pop or "", False)

        response_data = None
        while True:
            request = sec.security_session(response_data)
            if request is None:
                break
            request_bytes = (
                request.encode("latin-1") if isinstance(request, str) else request
            )
            response = transport.request(transport.session_endpoint, request_bytes)
            if response is None:
                self._log("Session request failed")
                return False
            response_data = response.decode("latin-1")

        self._finish_session(session_key, transport, sec)
        self._log(f"SEC{sec_ver} handshake completed successfully for {node_id}")
        return True

    def _finish_session(self, session_key, transport, security_ctx):
        """Store the security context and apply any transport-specific session state."""
        if session_key in self._local_ctrl_sessions:
            self._local_ctrl_sessions[session_key]["security_ctx"] = security_ctx
        globalize = getattr(transport, "globalize_session_cookies", None)
        if globalize is not None:
            globalize()

    def _get_data_via_endpoint(
        self,
        node_id,
        session_key,
        endpoint,
        data_type,
        security_ctx,
        params_filter=None,
    ):
        """
        Fetch params/config through the endpoint protocol's client-pull
        fragmentation loop. Returns the assembled bytes or None on failure.
        """
        assembled = bytearray()
        offset = 0
        while True:
            cmd = self._local_ctrl_manager.cmd_get_data(
                data_type, offset, security_ctx, params_filter
            )
            response_data = self._send_endpoint_request(
                node_id, session_key, cmd, endpoint
            )
            if response_data is None:
                return None
            parsed = self._local_ctrl_manager.parse_get_data_response(
                response_data, security_ctx
            )
            if parsed is None:
                self._log(f"Failed to parse {endpoint} response")
                return None
            status, resp_offset, payload, total_len = parsed
            if status != Status.Success:
                self._log(f"{endpoint} returned status {status}")
                return None
            if resp_offset != offset:
                self._log(
                    f"{endpoint} offset mismatch (sent {offset}, got {resp_offset})"
                )
                return None
            assembled.extend(payload)
            offset += len(payload)
            if offset >= total_len or not payload:
                break
        return bytes(assembled)

    def _send_endpoint_request(
        self,
        node_id,
        session_key,
        protobuf_message,
        endpoint,
        capability=CAPABILITY_LOCAL_CTRL,
    ):
        """
        Send a protobuf message to the given endpoint over the session's transport.

        Args:
            node_id (str): Node ID (thing name)
            session_key (str): Key into the session table
            protobuf_message (str | bytes): Serialized message as a latin-1 string
            endpoint (str): Endpoint name

        Returns:
            bytes: Response message or None if failed
        """
        try:
            session_data = self._local_ctrl_sessions.get(session_key)
            if not session_data or session_data.get("transport") is None:
                self._log(f"No session available for node={node_id}, key={session_key}")
                return None

            message_bytes = (
                protobuf_message.encode("latin-1")
                if isinstance(protobuf_message, str)
                else protobuf_message
            )
            self._log_bytes("Request data", message_bytes)

            transport = session_data["transport"]
            try:
                response = transport.request(endpoint, message_bytes)
            except TransportError as err:
                if session_data.get("mdns_protocol") != PROTOCOL_TCP:
                    self._log(f"Transport error for {node_id} ({capability}): {err}")
                    return None
                # Only mDNS sessions can be re-resolved and retried in place.
                self._log(
                    f"Request failed for {node_id} ({capability}): {err}; refreshing session"
                )
                transport.close()
                _key, session_data = self._get_or_resolve_service_session(
                    node_id=node_id,
                    capability=capability,
                    protocol=PROTOCOL_TCP,
                    force_refresh=True,
                )
                if not session_data or session_data.get("transport") is None:
                    return None
                try:
                    response = session_data["transport"].request(
                        endpoint, message_bytes
                    )
                except TransportError as retry_err:
                    self._log(f"Retry failed for {node_id}: {retry_err}")
                    return None

            if response is not None:
                self._log_bytes("Response data", response)
            return response

        except Exception as e:
            self._log(f"Error sending local control request: {str(e)}")
            return None

    def _protocol_for(self, node_id, capability):
        """Return the transport protocol cached for a node's capability."""
        state = self.get_cached_node_state(node_id) or {}
        return state.get(capability, {}).get("protocol", PROTOCOL_TCP)

    def _session_for_capability(self, node_id, capability):
        """
        Resolve the session serving a capability, without establishing security.

        Returns:
            tuple[str | None, dict | None]: (session_key, session_data)
        """
        return self._get_or_resolve_service_session(
            node_id=node_id,
            capability=capability,
            protocol=self._protocol_for(node_id, capability),
        )

    def _ensure_capability_session(
        self,
        node_id,
        capability,
        pop_retrieval_fn=None,
        username_retrieval_fn=None,
    ):
        """
        Resolve a session offering the capability and establish its security
        context if it does not have one yet.

        Returns:
            tuple[str | None, Any]: (session_key, security_ctx)
        """
        session_key, session_data = self._session_for_capability(node_id, capability)
        if not session_data:
            self._log(f"Node {node_id} does not offer the {capability} capability")
            return None, None

        if session_data.get("security_ctx") is None:
            if not self.establish_session(
                node_id=node_id,
                pop_retrieval_fn=pop_retrieval_fn,
                capability=capability,
                protocol=self._protocol_for(node_id, capability),
                username_retrieval_fn=username_retrieval_fn,
            ):
                return None, None
            session_data = self._local_ctrl_sessions.get(session_key)
            if not session_data:
                return None, None

        return session_key, session_data.get("security_ctx")

    def challenge_response(
        self,
        node_id,
        challenge_payload,
        pop_retrieval_fn=None,
        group_id=None,
        username_retrieval_fn=None,
    ):
        """
        Send a challenge-response command to the ch_resp endpoint.
        """
        try:
            session_key, security_ctx = self._ensure_capability_session(
                node_id,
                CAPABILITY_CHAL_RESP,
                pop_retrieval_fn,
                username_retrieval_fn,
            )
            if session_key is None:
                self._log(f"Challenge-response service not found for node {node_id}")
                return None

            req = self._chal_resp_manager.challenge_response(
                challenge_payload, security_ctx
            )
            response_data = self._send_endpoint_request(
                node_id, session_key, req, CHAL_RESP_ENDPOINT, CAPABILITY_CHAL_RESP
            )
            if not response_data:
                return None
            response = self._chal_resp_manager.parse_response(
                response_data, security_ctx
            )
            if isinstance(response, ChalRespChallengeResponse):
                return response
            return None
        except Exception as e:
            self._log(f"Error in challenge-response for {node_id}: {e}")
            return None

    def get_chal_resp_node_id(
        self,
        node_id,
        pop_retrieval_fn=None,
        group_id=None,
        username_retrieval_fn=None,
    ):
        """
        Fetch node ID from the challenge-response endpoint.

        @note The device does not implement this command; it is kept for
        protocol completeness.
        """
        try:
            session_key, security_ctx = self._ensure_capability_session(
                node_id,
                CAPABILITY_CHAL_RESP,
                pop_retrieval_fn,
                username_retrieval_fn,
            )
            if session_key is None:
                return None
            req = self._chal_resp_manager.get_node_id(security_ctx)
            response_data = self._send_endpoint_request(
                node_id, session_key, req, CHAL_RESP_ENDPOINT, CAPABILITY_CHAL_RESP
            )
            if not response_data:
                return None
            response = self._chal_resp_manager.parse_response(
                response_data, security_ctx
            )
            if isinstance(response, ChalRespGetNodeIDResponse):
                return response
            return None
        except Exception as e:
            self._log(f"Error getting challenge-response node ID for {node_id}: {e}")
            return None

    def disable_chal_resp(
        self,
        node_id,
        pop_retrieval_fn=None,
        group_id=None,
        username_retrieval_fn=None,
    ):
        """
        Disable challenge-response via the ch_resp endpoint.
        """
        try:
            session_key, security_ctx = self._ensure_capability_session(
                node_id,
                CAPABILITY_CHAL_RESP,
                pop_retrieval_fn,
                username_retrieval_fn,
            )
            if session_key is None:
                return None
            req = self._chal_resp_manager.disable(security_ctx)
            response_data = self._send_endpoint_request(
                node_id, session_key, req, CHAL_RESP_ENDPOINT, CAPABILITY_CHAL_RESP
            )
            if not response_data:
                return None
            response = self._chal_resp_manager.parse_response(
                response_data, security_ctx
            )
            if isinstance(response, ChalRespResponse):
                return response
            return None
        except Exception as e:
            self._log(f"Error disabling challenge-response for {node_id}: {e}")
            return None

    def _get_node_data(self, node_id, endpoint, data_type, params_filter=None):
        """
        Read params or config via local control.

        Returns:
            GetResponse: Response holding the data, or None on failure
        """
        try:
            session_key, session_data = self._session_for_capability(
                node_id, CAPABILITY_LOCAL_CTRL
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None

            data = self._get_data_via_endpoint(
                node_id,
                session_key,
                endpoint,
                data_type,
                session_data.get("security_ctx"),
                params_filter,
            )
            if data is not None:
                self._log(f"Successfully got node {data_type} for {node_id}")
                return GetResponse(status=Status.Success, name=data_type, value=data)
            self._log(f"Failed to get node {data_type} for {node_id}")

        except Exception as e:
            self._log(f"Error getting node {data_type}: {str(e)}")

        return None

    def get_node_config(self, node_id):
        """
        Get node configuration via local control.

        Args:
            node_id (str): Node ID (thing name)
        Returns:
            GetResponse: Response containing node config or None if failed or no session
        """
        return self._get_node_data(node_id, "get_config", "config")

    def get_node_params(self, node_id, params_filter=None):
        """
        Get node parameters via local control.

        Args:
            node_id (str): Node ID (thing name)
            params_filter (bytes | None): Optional ParamsFilter JSON, for example
                b'{"params":{"light":["power"]}}'
        Returns:
            GetResponse: Response containing node params or None if failed or no session
        """
        return self._get_node_data(node_id, "get_params", "params", params_filter)

    def set_node_params(self, node_id, params_data):
        """
        Set node parameters via local control.

        Args:
            node_id (str): Node ID (thing name)
            params_data (dict): New parameter data
        Returns:
            SetResponse: Response indicating success/failure or None if failed or no session
        """
        try:
            session_key, session_data = self._session_for_capability(
                node_id, CAPABILITY_LOCAL_CTRL
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None

            security_ctx = session_data.get("security_ctx")
            set_msg = self._local_ctrl_manager.cmd_set_params(
                json.dumps(params_data).encode("utf-8"), security_ctx
            )
            response_data = self._send_endpoint_request(
                node_id, session_key, set_msg, "set_params", CAPABILITY_LOCAL_CTRL
            )
            if response_data:
                response = self._local_ctrl_manager.parse_set_params_response(
                    response_data, security_ctx
                )
                if isinstance(response, SetResponse):
                    self._log(f"Successfully set node params for {node_id}")
                    return response
                self._log(f"Failed to parse set params response for {node_id}")
            else:
                self._log(f"Failed to set node params for {node_id}")

        except Exception as e:
            self._log(f"Error setting node params: {str(e)}")

        return None

    def send_to_endpoint(self, node_id, endpoint, data: bytes):
        """
        Send raw bytes to an application endpoint over the local control session.

        Args:
            node_id (str): Node ID (thing name)
            endpoint (str): Endpoint name registered with esp_rmaker_local_ctrl_add_endpoint()
            data (bytes): Request payload
        Returns:
            bytes: Decrypted response payload, or None if the request failed or no session
        """
        try:
            session_key, session_data = self._session_for_capability(
                node_id, CAPABILITY_LOCAL_CTRL
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None

            security_ctx = session_data.get("security_ctx")
            request = security_ctx.encrypt_data(data) if security_ctx else data
            response_data = self._send_endpoint_request(
                node_id, session_key, request, endpoint, CAPABILITY_LOCAL_CTRL
            )
            if response_data is None:
                self._log(f"No response from endpoint {endpoint} on {node_id}")
                return None
            return (
                security_ctx.decrypt_data(response_data)
                if security_ctx
                else response_data
            )

        except Exception as e:
            self._log(f"Error sending to endpoint {endpoint}: {str(e)}")

        return None

    def _close_session(self, session_key):
        """Close one session's transport and forget the session and its node state."""
        session_data = self._local_ctrl_sessions.pop(session_key, None)
        if session_data is None:
            return
        self._node_service_state.pop(session_key.rsplit(":", 1)[0], None)
        transport = session_data.get("transport")
        if transport is not None:
            try:
                transport.close()
                self._log(f"Closed transport for {session_key}")
            except Exception as e:
                self._log(f"Error closing session for {session_key}: {e}")

    def close_sessions(self):
        """
        Close all local control sessions.

        This should be called when done with local control operations
        to free up resources.
        """
        # Close every transport
        for session_key, session_data in self._local_ctrl_sessions.items():
            transport = session_data.get("transport")
            if transport is None:
                continue
            try:
                transport.close()
                self._log(f"Closed transport for {session_key}")
            except Exception as e:
                self._log(f"Error closing session for {session_key}: {e}")
        self._local_ctrl_sessions.clear()
        self._node_service_state.clear()

        # Close Zeroconf instance if it was created
        if self._zeroconf is not None:
            try:
                self._zeroconf.close()
                self._zeroconf = None
            except Exception as e:
                self._log(f"Error closing Zeroconf: {e}")
