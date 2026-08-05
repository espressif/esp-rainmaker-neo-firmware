# Local Control CLI

Interactive CLI for ESP RainMaker local control and on-network challenge-response services.

The tool:
- Discovers services for a node by `node_id` over mDNS.
- Connects to the local endpoints service (`_esp_rmaker_ctrl._tcp`), which carries
  local control and/or challenge-response depending on the node's `cap` TXT record.
- Opens an interactive prompt (`lc>`) for commands.

## Location

`tools/local_ctrl_cli/local_ctrl_cli.py`

## Prerequisites

- Run from this repository so the script can import shared utilities under `tools/common`.
- A reachable node advertising `_esp_rmaker_ctrl._tcp` with at least one capability
  in its `cap` TXT record (`local_ctrl` and/or `ch_resp`).

## Usage

From repo root:

```bash
python3 tools/local_ctrl_cli/local_ctrl_cli.py [--pop <proof_of_possession>] [--auto-reconnect] [--verbose] <node_id>
```

Arguments:
- `--pop` (optional): Proof of Possession for the security flow (SEC1, or SEC2 where it is reused as the SRP6a password).
- `--auto-reconnect` (optional): Retry once after reconnect when a command fails.
- `--verbose` (optional): Enable verbose `LocalController` logging.
- `node_id` (required): Node id / thing name.

If PoP is required by the device and `--pop` is not provided, the CLI prompts once.

For SEC2 (SRP6a), the username is fixed to `wifiprov` to match the device and is not prompted or configurable.

## Session Output

On successful connect, the CLI prints:
- Node state (`commissioned`, etc., based on discovery metadata)
- Cached local-control endpoint when available (`ip:port`)

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
- `get-params` - Read node params.
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
