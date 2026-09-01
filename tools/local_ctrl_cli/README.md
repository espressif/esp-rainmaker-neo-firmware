# Local Control CLI

Interactive CLI for ESP RainMaker local control and on-network challenge-response services.

The tool:
- Discovers services for a node by `node_id` over mDNS.
- Connects to the local endpoints service (`_esp_rmaker_ctrl._tcp`), which carries
  local control and/or challenge-response depending on the node's `cap` TXT record.
- Connects to BLE-only nodes with `--ble`, over the same endpoints on protocomm's
  BLE GATT transport.
- Opens an interactive prompt (`lc>`) for commands.

Both transports use the same endpoints, the same protobuf messages and the same
security schemes. Only the addressing and the session/version endpoint names
differ.

## Location

`tools/local_ctrl_cli/local_ctrl_cli.py`

## Prerequisites

- Run from this repository so the script can import shared utilities under `tools/common`.
- For mDNS: a reachable node advertising `_esp_rmaker_ctrl._tcp` with at least one
  capability in its `cap` TXT record (`local_ctrl` and/or `ch_resp`).
- For BLE: a node advertising its provisioning service.

## Usage

From repo root:

```bash
python3 tools/local_ctrl_cli/local_ctrl_cli.py [--pop <proof_of_possession>] [--auto-reconnect] [--verbose] <node_id>
python3 tools/local_ctrl_cli/local_ctrl_cli.py --ble [--ble-name <device_name>] [--ble-adapter <adapter>] [--pop <proof_of_possession>]
```

Arguments:
- `--pop` (optional): Proof of Possession for the security flow (SEC1, or SEC2 where it is reused as the SRP6a password).
- `--auto-reconnect` (optional): Retry once after reconnect when a command fails.
- `--verbose` (optional): Enable verbose `LocalController` logging.
- `node_id`: Node id / thing name. Required for mDNS, and not used with `--ble`.
- `--ble` (optional): Use the BLE transport instead of mDNS / TCP.
- `--ble-name` (optional): Advertised BLE device name, for example `PROV_A1B2C3`.
  Omit it to select the device from a scan list.
- `--ble-adapter` (optional): Adapter to scan with, for example `hci0`. Linux /
  BlueZ only. Leave it unset to use the default adapter.

The transport is always explicit. Without `--ble` the tool behaves exactly as
before and never scans for BLE devices.

If PoP is required by the device and `--pop` is not provided, the CLI prompts once.

For SEC2 (SRP6a), the username is fixed to `wifiprov` to match the device and is not prompted or configurable.

## BLE addressing and discovery

A BLE node is addressed by its advertised name, not by `node_id`, because the
node id is not known before a session exists. With `--ble-name` the tool connects
straight to that device. Without it, the underlying `esp_prov` client lists every
BLE device in range and prompts for a selection.

## Session Output

On successful connect, the CLI prints:
- Node state (`commissioned`, etc., based on discovery metadata)
- Cached local-control endpoint when available (`ip:port`), or the BLE device name

Then it enters REPL mode:

```text
lc>
```

## Commands

Always available:
- `help` - Show command help.
- `status` - Show node/service/session status.
- `reconnect` - Re-discover and reconnect.
- `quit` / `exit` - Exit CLI.

Available when local-control is advertised:
- `get-config` - Read node config.
- `get-params [<filter_json>]` - Read node params. The optional filter selects params, for example `get-params {"params":{"light":["power"]}}`. See the `ParamsFilter` rules in the local control endpoint protocol spec.
- `set <device> <param> <json_value>` - Set one parameter.
- `set-json <json_object>` - Set parameters using a JSON object payload.

Available when challenge-response is advertised:
- `challenge <data> [utf8|hex]` - Send challenge payload (default `utf8`).
- `get-node-id` - Read challenge-response node id.
- `disable-chal-resp` - Disable challenge-response service.

Unavailable commands are rejected with a clear message based on discovered service support.

## Examples

Start session:

```bash
python3 tools/local_ctrl_cli/local_ctrl_cli.py my_node --auto-reconnect
```

Start a BLE session, selecting the device from a list:

```bash
python3 tools/local_ctrl_cli/local_ctrl_cli.py --ble --pop abcd1234
```

Start a BLE session against a known device:

```bash
python3 tools/local_ctrl_cli/local_ctrl_cli.py --ble --ble-name PROV_A1B2C3 --pop abcd1234
```

Read params:

```text
lc> get-params
```

Set one parameter:

```text
lc> set Light Power true
lc> set Thermostat Temperature 24
```

Set multiple parameters via JSON:

```text
lc> set-json {"Light":{"Power":true,"Brightness":40}}
```

Challenge-response with UTF-8:

```text
lc> challenge hello
```

Challenge-response with hex payload:

```text
lc> challenge 01020304 hex
```

Check status:

```text
lc> status
```

## Notes

- `set` parses `<json_value>` as JSON first; if parsing fails, it is sent as a string.
- `set-json` requires a JSON object (dictionary) payload.
- With `--auto-reconnect`, command flow is: fail -> reconnect -> retry once.
- On mDNS, a failed request also re-resolves the service and retries once inside
  the controller. BLE has no equivalent, so a dropped link needs a `reconnect`.
- `get-node-id` is not implemented by the device on either transport.
