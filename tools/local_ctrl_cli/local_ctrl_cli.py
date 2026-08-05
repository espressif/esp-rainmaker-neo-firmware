#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import shlex
import sys
from pathlib import Path
from typing import Any, Callable, Optional

_COMMON_ROOT = Path(__file__).resolve().parents[1] / "common"
if str(_COMMON_ROOT) not in sys.path:
    sys.path.insert(0, str(_COMMON_ROOT))

from util.local_ctrl import (  # noqa: E402
    LocalController,
    LOCAL_CTRL_SERVICE_NAME,
    CAPABILITY_LOCAL_CTRL,
    SEC2_USERNAME,
)

# One advertised service; the active capabilities come from its `cap` TXT record.
_SERVICE = f"{LOCAL_CTRL_SERVICE_NAME}._tcp"


def _status_text(value: Any) -> str:
    if value is None:
        return "None"
    return getattr(value, "name", str(value))


class LocalCtrlCli:
    def __init__(
        self,
        node_id: str,
        pop: Optional[str],
        auto_reconnect: bool,
        verbose: bool,
    ):
        self.node_id = node_id
        self.pop = pop
        self.username = SEC2_USERNAME
        self.auto_reconnect = auto_reconnect
        self.ctrl = LocalController(logging=verbose)
        self.connected = False
        self._prompted_for_pop = False
        self.node_state: dict[str, Any] = {}

    def _resolved_local_ctrl_endpoint(self) -> Optional[tuple[str, int]]:
        """Return currently cached local-control endpoint from mDNS metadata."""
        for session_data in self.ctrl._local_ctrl_sessions.values():
            if (
                session_data.get("mdns_service_type") == LOCAL_CTRL_SERVICE_NAME
                and session_data.get("mdns_protocol") == "tcp"
            ):
                ip = session_data.get("ip_address")
                port = session_data.get("mdns_port")
                if ip and port is not None:
                    return ip, int(port)
        return None

    def _pop_retrieval_fn(self) -> Optional[str]:
        # Require by default whenever requested
        pop_required = True

        if not pop_required:
            return None
        if self.pop:
            return self.pop
        if self._prompted_for_pop:
            return None
        self._prompted_for_pop = True
        entered = input("PoP (required by device): ").strip()
        if entered:
            self.pop = entered
            return entered
        return None

    def _username_retrieval_fn(self) -> Optional[str]:
        # Username is fixed on the device; never prompt.
        return self.username

    def connect(self) -> bool:
        state = self.ctrl.discover_sessions(self.node_id)
        self.node_state = state
        has_local = bool(state.get("local_ctrl", {}).get("available"))
        has_chal = bool(state.get("chal_resp", {}).get("available"))
        if not has_local and not has_chal:
            self.connected = False
            return False

        # Single service: both capabilities share the session; security details
        # always come from the version endpoint.
        self.ctrl.establish_session(
            node_id=self.node_id,
            pop_retrieval_fn=self._pop_retrieval_fn,
            capability=CAPABILITY_LOCAL_CTRL,
            protocol="tcp",
            username_retrieval_fn=self._username_retrieval_fn,
        )
        self.connected = True
        return True

    def close(self) -> None:
        self.ctrl.close_sessions()
        self.connected = False
        self.node_state = {}

    def _supports_local_ctrl(self) -> bool:
        return bool(self.node_state.get("local_ctrl", {}).get("available"))

    def _supports_chal_resp(self) -> bool:
        return bool(self.node_state.get("chal_resp", {}).get("available"))

    def _supported_commands(self) -> list[str]:
        commands = ["help", "status", "reconnect", "quit", "exit"]
        if self._supports_local_ctrl():
            commands.extend(["get-config", "get-params", "set", "set-json"])
        if self._supports_chal_resp():
            commands.extend(["challenge", "get-node-id", "disable-chal-resp"])
        return commands

    def _with_reconnect(self, op_name: str, fn: Callable[[], Any]) -> Any:
        result = fn()
        if result is not None:
            return result
        if not self.auto_reconnect:
            return None
        print(f"{op_name} failed, attempting reconnect...")
        if not self.connect():
            print("Reconnect failed.")
            return None
        print("Reconnect successful, retrying command...")
        return fn()

    def print_status(self) -> None:
        resolved = self._resolved_local_ctrl_endpoint()
        if resolved:
            endpoint_text = f"{resolved[0]}:{resolved[1]}"
        else:
            endpoint_text = "unresolved"
        node_mode = self.node_state.get("state", "unknown")
        local_available = "yes" if self._supports_local_ctrl() else "no"
        chal_available = "yes" if self._supports_chal_resp() else "no"
        print(
            f"node={self.node_id}, "
            f"service={_SERVICE}, protocol=tcp, "
            f"node_state={node_mode}, local_ctrl={local_available}, chal_resp={chal_available}, "
            f"resolved_endpoint={endpoint_text}, "
            f"connected={'yes' if self.connected else 'no'}, "
            f"auto_reconnect={'on' if self.auto_reconnect else 'off'}"
        )

    def cmd_get_config(self) -> None:
        resp = self._with_reconnect(
            "get-config",
            lambda: self.ctrl.get_node_config(self.node_id),
        )
        if resp is None:
            print("get-config failed.")
            return
        try:
            payload = json.loads(resp.value.decode("utf-8"))
            print(json.dumps(payload, indent=2, sort_keys=True))
        except Exception:
            print(resp.value.decode("utf-8", errors="replace"))

    def cmd_get_params(self) -> None:
        resp = self._with_reconnect(
            "get-params",
            lambda: self.ctrl.get_node_params(self.node_id),
        )
        if resp is None:
            print("get-params failed.")
            return
        try:
            payload = json.loads(resp.value.decode("utf-8"))
            print(json.dumps(payload, indent=2, sort_keys=True))
        except Exception:
            print(resp.value.decode("utf-8", errors="replace"))

    def cmd_set_param(self, device: str, param: str, raw_value: str) -> None:
        try:
            value = json.loads(raw_value)
        except json.JSONDecodeError:
            # Convenience fallback: unquoted values are treated as strings.
            value = raw_value
        payload = {device: {param: value}}
        resp = self._with_reconnect(
            "set",
            lambda: self.ctrl.set_node_params(self.node_id, payload),
        )
        if resp is None:
            print("set failed.")
            return
        print(f"set status: {_status_text(resp.status)}")

    def cmd_set_json(self, payload_text: str) -> None:
        try:
            payload = json.loads(payload_text)
            if not isinstance(payload, dict):
                raise ValueError("payload is not a JSON object")
        except Exception as exc:
            print(f"invalid JSON payload: {exc}")
            return
        resp = self._with_reconnect(
            "set-json",
            lambda: self.ctrl.set_node_params(self.node_id, payload),
        )
        if resp is None:
            print("set-json failed.")
            return
        print(f"set-json status: {_status_text(resp.status)}")

    def cmd_challenge(self, data: str, encoding: str) -> None:
        if encoding == "hex":
            try:
                payload = bytes.fromhex(data)
            except ValueError as exc:
                print(f"invalid hex payload: {exc}")
                return
        else:
            payload = data.encode("utf-8")

        resp = self._with_reconnect(
            "challenge",
            lambda: self.ctrl.challenge_response(
                self.node_id,
                payload,
                pop_retrieval_fn=self._pop_retrieval_fn,
                username_retrieval_fn=self._username_retrieval_fn,
            ),
        )
        if resp is None:
            print("challenge failed.")
            return

        print(f"status: {_status_text(resp.status)}")
        print(f"node_id: {resp.node_id}")
        print(f"payload_hex: {resp.payload.hex()}")

    def cmd_get_node_id(self) -> None:
        resp = self._with_reconnect(
            "get-node-id",
            lambda: self.ctrl.get_chal_resp_node_id(
                self.node_id,
                pop_retrieval_fn=self._pop_retrieval_fn,
                username_retrieval_fn=self._username_retrieval_fn,
            ),
        )
        if resp is None:
            print("get-node-id failed.")
            return
        print(f"status: {_status_text(resp.status)}")
        print(f"node_id: {resp.node_id}")

    def cmd_disable_chal_resp(self) -> None:
        resp = self._with_reconnect(
            "disable-chal-resp",
            lambda: self.ctrl.disable_chal_resp(
                self.node_id,
                pop_retrieval_fn=self._pop_retrieval_fn,
                username_retrieval_fn=self._username_retrieval_fn,
            ),
        )
        if resp is None:
            print("disable-chal-resp failed.")
            return
        print(f"status: {_status_text(resp.status)}")


def _print_help_for_state(cli: LocalCtrlCli) -> None:
    print("Commands:")
    print("  help                                Show this help")
    print("  status                              Show session state")
    print("  reconnect                           Re-discover services and reconnect")
    if cli._supports_local_ctrl():
        print(
            "  get-config                          Read node config via local control"
        )
        print(
            "  get-params                          Read node params via local control"
        )
        print("  set <device> <param> <json_value>   Set one parameter")
        print("  set-json <json_object>              Set params using JSON object")
    if cli._supports_chal_resp():
        print(
            "  challenge <data> [utf8|hex]         Challenge-response command (default utf8)"
        )
        print("  get-node-id                         Read challenge-response node id")
        print(
            "  disable-chal-resp                   Disable challenge-response service"
        )
    print("  quit | exit                         Quit")


def _run_repl(cli: LocalCtrlCli) -> int:
    _print_help_for_state(cli)
    while True:
        try:
            line = input("lc> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not line:
            continue
        try:
            parts = shlex.split(line)
        except ValueError as exc:
            print(f"parse error: {exc}")
            continue

        cmd = parts[0].lower()
        args = parts[1:]

        if cmd in ("quit", "exit"):
            return 0
        if cmd == "help":
            _print_help_for_state(cli)
            continue
        if cmd == "status":
            cli.print_status()
            continue
        if cmd == "reconnect":
            print("Reconnecting...")
            if cli.connect():
                print("Connected.")
            else:
                print("Connect failed.")
            continue
        if cmd == "get-config":
            if not cli._supports_local_ctrl():
                print(
                    "Command unavailable: local-control service is not advertised by this node."
                )
                continue
            cli.cmd_get_config()
            continue
        if cmd == "get-params":
            if not cli._supports_local_ctrl():
                print(
                    "Command unavailable: local-control service is not advertised by this node."
                )
                continue
            cli.cmd_get_params()
            continue
        if cmd == "set":
            if not cli._supports_local_ctrl():
                print(
                    "Command unavailable: local-control service is not advertised by this node."
                )
                continue
            if len(args) < 3:
                print("usage: set <device> <param> <json_value>")
                continue
            device = args[0]
            param = args[1]
            value = " ".join(args[2:])
            cli.cmd_set_param(device, param, value)
            continue
        if cmd == "set-json":
            if not cli._supports_local_ctrl():
                print(
                    "Command unavailable: local-control service is not advertised by this node."
                )
                continue
            if not args:
                print("usage: set-json <json_object>")
                continue
            cli.cmd_set_json(" ".join(args))
            continue
        if cmd == "challenge":
            if not cli._supports_chal_resp():
                print(
                    "Command unavailable: challenge-response service is not advertised by this node."
                )
                continue
            if not args:
                print("usage: challenge <data> [utf8|hex]")
                continue
            encoding = args[1].lower() if len(args) >= 2 else "utf8"
            if encoding not in ("utf8", "hex"):
                print("encoding must be utf8 or hex")
                continue
            cli.cmd_challenge(args[0], encoding)
            continue
        if cmd == "get-node-id":
            if not cli._supports_chal_resp():
                print(
                    "Command unavailable: challenge-response service is not advertised by this node."
                )
                continue
            cli.cmd_get_node_id()
            continue
        if cmd == "disable-chal-resp":
            if not cli._supports_chal_resp():
                print(
                    "Command unavailable: challenge-response service is not advertised by this node."
                )
                continue
            cli.cmd_disable_chal_resp()
            continue

        print(f"unknown command: {cmd}. type 'help' for usage.")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Local control + challenge-response CLI tool",
    )
    parser.add_argument("node_id", help="Node id / thing name")
    parser.add_argument(
        "--pop",
        default=None,
        help="Proof of Possession for SEC1 / SEC2 (optional; reused as SRP password for SEC2)",
    )
    parser.add_argument(
        "--auto-reconnect",
        action="store_true",
        help="Reconnect and retry once when a command fails",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Enable verbose local controller logs"
    )
    return parser


def main(argv: list[str]) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    cli = LocalCtrlCli(
        node_id=args.node_id,
        pop=args.pop,
        auto_reconnect=args.auto_reconnect,
        verbose=args.verbose,
    )

    try:
        if not cli.connect():
            print("Failed to discover supported services for node.")
            return 1
        print(f"Node state: {cli.node_state.get('state', 'unknown')}")
        resolved = cli._resolved_local_ctrl_endpoint()
        if resolved:
            print(
                f"Connected to node={args.node_id} "
                f"(resolved mDNS endpoint={resolved[0]}:{resolved[1]})"
            )
        else:
            print(f"Connected to node={args.node_id} (mDNS endpoint not cached)")

        return _run_repl(cli)
    finally:
        cli.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
