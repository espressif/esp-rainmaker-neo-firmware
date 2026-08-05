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
from zeroconf import Zeroconf, ServiceBrowser, ServiceListener
import json
import os
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
        if "http_session" not in session_data:
            session_data["http_session"] = requests.Session()
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
                    if (
                        session_data
                        and session_data.get("http_session")
                        and session_data.get("ip_address")
                        and session_data.get("mdns_port") is not None
                    ):
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
                    and session_data.get("http_session")
                    and session_data.get("ip_address")
                    and session_data.get("mdns_port") is not None
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

    def _create_local_ctrl_connection(self, node_id):
        """
        Create HTTP connection to a node's local control server.

        Args:
            node_id (str): Node ID (thing name)
        Returns:
            str: Base URL for the local control server
        """
        session_key, session_data = self._get_or_resolve_service_session(
            node_id, LOCAL_CTRL_SERVICE_NAME, "tcp"
        )
        if not session_data:
            return None
        ip_address = session_data.get("ip_address")
        service_port = session_data.get("mdns_port")
        formatted_ip = self._format_ip_for_url(ip_address)
        base_url = f"http://{formatted_ip}:{service_port}"
        self._log(f"Created local control connection to {base_url}")
        return base_url

    def _get_local_ctrl_session(self, node_id):
        """
        Get or create a reusable HTTP session for local control connections.
        Resolves IP address via mDNS on first access.

        Args:
            node_id (str): Node ID (thing name)
        Returns:
            requests.Session: Reusable HTTP session
        """
        session_key, session_data = self._get_or_resolve_service_session(
            node_id, LOCAL_CTRL_SERVICE_NAME, "tcp"
        )
        if not session_data:
            return None
        self._log(
            f"Using reusable HTTP session for {session_key} (IP: {session_data.get('ip_address')})"
        )
        return session_data["http_session"]

    def _get_local_ctrl_security_ctx(self, node_id, port=8080):
        """
        Get the security context for a local control session.

        Args:
            node_id (str): Node ID (thing name)
            port (int): Port number (default 8080 for local control)

        Returns:
            Security context or None if not established
        """
        session_key = f"{node_id}:{port}"
        if session_key in self._local_ctrl_sessions:
            return self._local_ctrl_sessions[session_key]["security_ctx"]
        return None

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

    def _globalize_session_cookies(self, session):
        """
        Apply all the session cookies to the entire session.
        """
        for cookie in session.cookies:
            if cookie.name == "session":
                self._log(f"Setting session cookie for all requests: {cookie.value}")
                cookie.path = "/"

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
            ip_address = session_data.get("ip_address")
            service_port = session_data.get("mdns_port")
            formatted_ip = self._format_ip_for_url(ip_address)
            base_url = f"http://{formatted_ip}:{service_port}"
            session = session_data.get("http_session")
            if session is None:
                return False

            sec_patch_ver = 0
            if sec_version is None:
                self._log(f"Checking version endpoint for {node_id}...")
                version_url = f"{base_url}/rmaker_local_ctrl/version"
                try:
                    version_response = session.post(
                        version_url, timeout=10, data=b"none"
                    )
                    if version_response.status_code != 200:
                        self._log(
                            f"Failed to get version: {version_response.status_code}"
                        )
                        return False

                    version_data = version_response.json()
                    local_ctrl_info = version_data.get("rmaker_local_ctrl", {})
                    sec_ver = local_ctrl_info.get("sec_ver", 0)
                    sec_patch_ver = local_ctrl_info.get("sec_patch_ver", 0)
                    self._log(
                        f"Detected security version: {sec_ver}, patch: {sec_patch_ver}"
                    )
                except Exception as e:
                    self._log(f"Error checking version endpoint: {e}")
                    return False
            else:
                sec_ver = sec_version
                self._log(
                    f"Using security version from caller (not version endpoint): {sec_ver}"
                )

            print(f"Session ID: {session.cookies.get_dict().get('session')}")

            # Establish secure session for SEC1 / SEC2. Both require a PoP retrieved from the
            # device shadow; SEC2 additionally requires the SRP6a username.
            if sec_ver in (1, 2):
                pop = None
                if pop_retrieval_fn is not None:
                    pop = pop_retrieval_fn()

                username = None
                if sec_ver == 2:
                    if username_retrieval_fn is None:
                        self._log(
                            "SEC2 requires username_retrieval_fn but none was provided"
                        )
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

                import sys
                from pathlib import Path

                from util.github_sync import (
                    get_github_archive_repo_root,
                    materialize_github_folder,
                )
                from util.github_deps import (
                    ESP_IDF_REF,
                    ESP_IDF_REPO_URL,
                    IDF_EXTRA_COMPONENTS_REF,
                    IDF_EXTRA_COMPONENTS_REPO_URL,
                    NETWORK_PROVISIONING_REL,
                )

                # idf-extra-components esp_prov/proto loads protocomm *_pb2 via os.environ["IDF_PATH"].
                prev_idf_path = os.environ.get("IDF_PATH")
                paths_added: list[str] = []
                try:
                    idf_root = get_github_archive_repo_root(
                        ESP_IDF_REPO_URL, ESP_IDF_REF
                    )
                    os.environ["IDF_PATH"] = str(idf_root)

                    np_root = materialize_github_folder(
                        IDF_EXTRA_COMPONENTS_REPO_URL,
                        IDF_EXTRA_COMPONENTS_REF,
                        NETWORK_PROVISIONING_REL,
                    )
                    esp_prov_dir = (Path(np_root) / "tool" / "esp_prov").resolve()
                    esp_prov_s = str(esp_prov_dir)
                    paths_added.append(esp_prov_s)
                    if esp_prov_s not in sys.path:
                        sys.path.insert(0, esp_prov_s)

                    import security

                except Exception as e:
                    for p in reversed(paths_added):
                        if p in sys.path:
                            sys.path.remove(p)
                    if prev_idf_path is not None:
                        os.environ["IDF_PATH"] = prev_idf_path
                    else:
                        os.environ.pop("IDF_PATH", None)
                    if isinstance(e, ImportError):
                        raise RuntimeError(
                            f"Failed to import ESP-IDF security modules from GitHub (esp_prov / protocomm): {e}"
                        ) from e
                    raise

                try:
                    if sec_ver == 1:
                        # Security1 expects a string PoP. Empty PoP allowed when device doesn't require it.
                        sec = security.Security1(pop or "", False)
                    else:
                        # Security2 expects (sec_patch_ver, username, password, verbose). PoP is reused as the SRP password.
                        sec = security.Security2(
                            sec_patch_ver, username, pop or "", False
                        )

                    # Perform the session establishment loop (same as esp_local_ctrl.py)
                    response_data = None
                    while True:
                        request = sec.security_session(response_data)
                        if request is None:
                            break

                        # Convert the latin-1 string to bytes (same as ESP transport does)
                        request_bytes = (
                            request.encode("latin-1")
                            if isinstance(request, str)
                            else request
                        )

                        init_url = f"{base_url}/rmaker_local_ctrl/session"
                        response = session.post(
                            init_url, data=request_bytes, timeout=10
                        )
                        if response.status_code != 200:
                            self._log(f"Session request failed: {response.status_code}")
                            return False

                        # Convert response bytes to latin-1 string for Security1/2
                        response_data = response.content.decode("latin-1")

                    # Store the security context for this session
                    self._local_ctrl_sessions[session_key]["security_ctx"] = sec
                    self._log(
                        f"SEC{sec_ver} handshake completed successfully for {node_id}"
                    )
                    self._globalize_session_cookies(session)
                    return True

                finally:
                    for p in reversed(paths_added):
                        if p in sys.path:
                            sys.path.remove(p)
                    if prev_idf_path is not None:
                        os.environ["IDF_PATH"] = prev_idf_path
                    else:
                        os.environ.pop("IDF_PATH", None)
            elif sec_ver == 0:
                # For SEC0 (unsecured), just mark session as established
                self._log(f"Using unsecured session (SEC0) for {node_id}")
                self._globalize_session_cookies(session)
                return True
            else:
                self._log(f"Unsupported security version: {sec_ver}")
                return False

        except Exception as e:
            self._log(f"Session establishment failed: {str(e)}")
            return False

    def _get_data_via_endpoint(self, node_id, port, endpoint, data_type, security_ctx):
        """
        Fetch params/config through the endpoint protocol's client-pull
        fragmentation loop. Returns the assembled bytes or None on failure.
        """
        assembled = bytearray()
        offset = 0
        while True:
            cmd = self._local_ctrl_manager.cmd_get_data(data_type, offset, security_ctx)
            response_data = self._send_endpoint_request(
                node_id, port, cmd, endpoint, LOCAL_CTRL_SERVICE_NAME
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
        port,
        protobuf_message,
        endpoint,
        capability=CAPABILITY_LOCAL_CTRL,
    ):
        """
        Send a protobuf message to the given endpoint using a reusable session.

        Args:
            node_id (str): Node ID (thing name)
            port (int): Port number
            protobuf_message (str): Serialized protobuf message as latin-1 string
            endpoint (str): HTTP endpoint path without leading slash

        Returns:
            bytes: Response protobuf message or None if failed
        """
        try:
            session_key = f"{node_id}:{port}"
            session_data = self._local_ctrl_sessions.get(session_key)
            if (
                not session_data
                or session_data.get("mdns_service_type") != LOCAL_CTRL_SERVICE_NAME
            ):
                session_key, session_data = self._get_or_resolve_service_session(
                    node_id=node_id,
                    capability=capability,
                    protocol="tcp",
                )
            if not session_data:
                self._log(
                    f"No mDNS session metadata available for node={node_id}, capability={capability}"
                )
                return None
            ip_address = session_data.get("ip_address")
            service_port = session_data.get("mdns_port")
            session = session_data.get("http_session")
            if session is None or not ip_address or service_port is None:
                self._log(
                    f"Incomplete mDNS session metadata for {session_key}: "
                    f"ip={ip_address}, port={service_port}, session={'yes' if session else 'no'}"
                )
                return None

            self._log(
                f"Using HTTP session key={session_key}, capability={capability}, "
                f"session_id={id(session)}, ip={ip_address}, port={service_port}"
            )

            # Convert latin-1 string back to bytes for HTTP transport
            message_bytes = (
                protobuf_message.encode("latin-1")
                if isinstance(protobuf_message, str)
                else protobuf_message
            )

            formatted_ip = self._format_ip_for_url(ip_address)
            url = f"http://{formatted_ip}:{service_port}/{endpoint}"
            headers = {
                "Content-Type": "application/octet-stream",
                "Content-Length": str(len(message_bytes)),
            }

            self._log(f"Sending request to {url}")
            self._log_bytes("Request data", message_bytes)

            try:
                response = session.post(
                    url, data=message_bytes, headers=headers, timeout=10
                )
            except requests.RequestException as req_err:
                self._log(
                    f"Request failed for {node_id} ({capability}): {req_err}; refreshing session"
                )
                try:
                    session.close()
                except Exception:
                    pass
                session_key, session_data = self._get_or_resolve_service_session(
                    node_id=node_id,
                    capability=capability,
                    protocol="tcp",
                    force_refresh=True,
                )
                if not session_data:
                    return None
                session = session_data.get("http_session")
                ip_address = session_data.get("ip_address")
                service_port = session_data.get("mdns_port")
                if session is None or not ip_address or service_port is None:
                    return None
                formatted_ip = self._format_ip_for_url(ip_address)
                url = f"http://{formatted_ip}:{service_port}/{endpoint}"
                response = session.post(
                    url, data=message_bytes, headers=headers, timeout=10
                )

            self._log(f"Response status: {response.status_code}")
            self._log_bytes("Response data", response.content)

            if response.status_code == 200:
                return response.content
            else:
                self._log(
                    f"Local control request failed with status {response.status_code}: {response.text}"
                )
                return None

        except Exception as e:
            self._log(f"Error sending local control request: {str(e)}")
            return None

    def _discover_chal_resp_service(self, node_id, timeout=5):
        """
        Discover the node's local endpoints service and check its `cap` TXT
        record for the ch_resp capability (single `_esp_rmaker_ctrl`
        advertisement; security details come from the version endpoint).
        """
        try:
            service = self.resolve_mdns_service(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp", timeout=timeout
            )
            if not service or not service.get("ip") or service.get("port") is None:
                return None
            txt = service.get("txt_records", {})
            caps = [c.strip() for c in txt.get("cap", "").split(",") if c.strip()]
            if CHAL_RESP_ENDPOINT not in caps:
                self._log(f"Node {node_id} does not advertise the ch_resp capability")
                return None
            self._upsert_session_from_service(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp", service
            )
            return {
                "ip": service.get("ip"),
                "txt_records": txt,
                "port": service.get("port"),
            }
        except Exception as e:
            self._log(
                f"Error discovering challenge-response capability for {node_id}: {e}"
            )
            return None

    def _ensure_chal_resp_session(
        self,
        node_id,
        port,
        pop_retrieval_fn=None,
        username_retrieval_fn=None,
    ):
        """
        Ensure a session/security context exists for the node's local endpoints
        service (security version is read from the version endpoint).
        """
        session_key = f"{node_id}:{port}"
        if (
            session_key in self._local_ctrl_sessions
            and self._local_ctrl_sessions[session_key].get("security_ctx") is not None
        ):
            return True

        return self.establish_session(
            node_id=node_id,
            pop_retrieval_fn=pop_retrieval_fn,
            capability=CAPABILITY_LOCAL_CTRL,
            protocol="tcp",
            username_retrieval_fn=username_retrieval_fn,
        )

    def challenge_response(
        self,
        node_id,
        challenge_payload,
        pop_retrieval_fn=None,
        group_id=None,
        username_retrieval_fn=None,
    ):
        """
        Send a challenge-response command to /ch_resp endpoint.
        """
        try:
            service = self._discover_chal_resp_service(node_id)
            if not service:
                self._log(f"Challenge-response service not found for node {node_id}")
                return None

            service_port = service["port"]
            if not self._ensure_chal_resp_session(
                node_id,
                service_port,
                pop_retrieval_fn,
                username_retrieval_fn,
            ):
                return None

            security_ctx = self._get_local_ctrl_security_ctx(node_id, service_port)
            req = self._chal_resp_manager.challenge_response(
                challenge_payload, security_ctx
            )
            response_data = self._send_endpoint_request(
                node_id, service_port, req, CHAL_RESP_ENDPOINT, LOCAL_CTRL_SERVICE_NAME
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
        Fetch node ID from challenge-response endpoint.
        """
        try:
            service = self._discover_chal_resp_service(node_id)
            if not service:
                return None
            service_port = service["port"]
            if not self._ensure_chal_resp_session(
                node_id,
                service_port,
                pop_retrieval_fn,
                username_retrieval_fn,
            ):
                return None
            security_ctx = self._get_local_ctrl_security_ctx(node_id, service_port)
            req = self._chal_resp_manager.get_node_id(security_ctx)
            response_data = self._send_endpoint_request(
                node_id, service_port, req, CHAL_RESP_ENDPOINT, LOCAL_CTRL_SERVICE_NAME
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
        Disable challenge-response via /ch_resp endpoint.
        """
        try:
            service = self._discover_chal_resp_service(node_id)
            if not service:
                return None
            service_port = service["port"]
            if not self._ensure_chal_resp_session(
                node_id,
                service_port,
                pop_retrieval_fn,
                username_retrieval_fn,
            ):
                return None
            security_ctx = self._get_local_ctrl_security_ctx(node_id, service_port)
            req = self._chal_resp_manager.disable(security_ctx)
            response_data = self._send_endpoint_request(
                node_id, service_port, req, CHAL_RESP_ENDPOINT, LOCAL_CTRL_SERVICE_NAME
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

    def get_node_config(self, node_id):
        """
        Get node configuration via local control.

        Args:
            node_id (str): Node ID (thing name)
        Returns:
            GetResponse: Response containing node config or None if failed or no session
        """
        try:
            session_key, session_data = self._get_or_resolve_service_session(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp"
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None
            service_port = session_data.get("mdns_port")

            security_ctx = self._local_ctrl_sessions[session_key].get("security_ctx")
            data = self._get_data_via_endpoint(
                node_id, service_port, "get_config", "config", security_ctx
            )
            if data is not None:
                self._log(f"Successfully got node config for {node_id}")
                return GetResponse(status=Status.Success, name="config", value=data)
            self._log(f"Failed to get node config for {node_id}")

        except Exception as e:
            self._log(f"Error getting node config: {str(e)}")

        return None

    def get_node_params(self, node_id):
        """
        Get node parameters via local control.

        Args:
            node_id (str): Node ID (thing name)
        Returns:
            GetResponse: Response containing node params or None if failed or no session
        """
        try:
            session_key, session_data = self._get_or_resolve_service_session(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp"
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None
            service_port = session_data.get("mdns_port")

            security_ctx = self._local_ctrl_sessions[session_key].get("security_ctx")
            data = self._get_data_via_endpoint(
                node_id, service_port, "get_params", "params", security_ctx
            )
            if data is not None:
                self._log(f"Successfully got node params for {node_id}")
                return GetResponse(status=Status.Success, name="params", value=data)
            self._log(f"Failed to get node params for {node_id}")

        except Exception as e:
            self._log(f"Error getting node params: {str(e)}")

        return None

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
            session_key, session_data = self._get_or_resolve_service_session(
                node_id, LOCAL_CTRL_SERVICE_NAME, "tcp"
            )
            if not session_data:
                self._log(
                    f"No active session for {node_id}. Use 'local_ctrl connect' first."
                )
                return None
            service_port = session_data.get("mdns_port")

            security_ctx = self._local_ctrl_sessions[session_key].get("security_ctx")
            set_msg = self._local_ctrl_manager.cmd_set_params(
                json.dumps(params_data).encode("utf-8"), security_ctx
            )
            response_data = self._send_endpoint_request(
                node_id, service_port, set_msg, "set_params", LOCAL_CTRL_SERVICE_NAME
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

    def close_sessions(self):
        """
        Close all local control HTTP sessions.

        This should be called when done with local control operations
        to free up resources.
        """
        # Close all HTTP sessions
        for session_key, session_data in self._local_ctrl_sessions.items():
            session = session_data["http_session"]
            try:
                session.close()
                self._log(f"Closed HTTP session for {session_key}")
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
