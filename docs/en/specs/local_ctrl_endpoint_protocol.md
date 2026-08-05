# Local Control Endpoint Protocol

This is the SDK's only local-control protocol: one wire protocol and **one
service instance** serving every local endpoint over a single HTTP server.
Local control and challenge-response are independently enable-able endpoint
*sets* on that instance rather than mutually exclusive services, because the
endpoints are plain protocomm handlers and protocomm's HTTP transport is a
process-wide singleton. Responses are fragmented so each one fits a small-MTU
transport such as BLE GATT.

## Variants

| local control | challenge-response | Behavior |
|---|---|---|
| off | off | no instance, nothing advertised |
| on | off | params/config endpoints; `cap=["get_params","set_params","get_config"]` |
| off | on | `ch_resp` only (on-network user-node association); `cap=["ch_resp"]` |
| on | on | one instance serving both endpoint sets |

## Discovery

A single service — **`_esp_rmaker_ctrl._tcp`** — is advertised whenever
the instance is up, with the node ID as both the hostname and the service
instance name. TXT records:

| Key | Value |
|-----|-------|
| `node_id` | Node ID |
| `cap` | Comma-separated active capabilities: `local_ctrl` and/or `ch_resp` |

Clients listing devices filter on `cap` without connecting (e.g. "available
for control" vs "available for on-network association"). The port is carried
by the mDNS SRV record; the security details are served by the version
endpoint. The `cap` TXT record is refreshed when endpoint sets are enabled or
disabled.

The service name is fixed at `esp_rmaker_ctrl`, which is exactly the 15 bytes
RFC 6763 §7.2 allows after the underscore. It carries both endpoint sets
regardless of the name — a node serving only challenge-response advertises the
same service with `cap=["ch_resp"]`.

## Session and version endpoints

The service owns its protocomm instance. Session security is protocomm SEC1
(with or without PoP, per `ESP_RMAKER_LOCAL_CTRL_SEC1_POP`) or SEC2 (SRP6a
with username `wifiprov`) — Kconfig-selected; security 0 is not supported.
Established on:

- **`rmaker_local_ctrl/session`** — protocomm session-security endpoint.
- **`rmaker_local_ctrl/version`** — POST any payload; responds with the
  service info JSON:

```json
{"rmaker_local_ctrl": {"ver": "v1.0", "sec_ver": 2, "sec_patch_ver": 1,
                       "cap": ["get_params", "set_params", "get_config"]}}
```

`sec_ver` / `sec_patch_ver` are reported by protocomm for the registered
security scheme. `cap` lists the active endpoint sets (as in the TXT record,
with local control expanded to its endpoint names) and additionally contains
`"no_pop"` for security 1 without PoP, following the network-provisioning
capability convention.

This endpoint is the **authority** on capabilities and is rewritten whenever an
endpoint set toggles; the TXT record is only a browse-time filter that may be
absent (BLE, or a client that connected from a QR code without browsing) or
stale. Note the deliberate difference in vocabulary: the TXT record names the
endpoint *set* (`local_ctrl`) to stay small, while this endpoint lists the
individual endpoint names a client will call.

## Data endpoints

All endpoints inherit the session security:

| Endpoint      | Request                       | Response |
|---------------|-------------------------------|----------|
| `get_params`  | protobuf `RMakerLocalCtrlPayload` (CmdGetData, DataType=TypeParams) | protobuf (RespGetData, fragmented) |
| `get_config`  | protobuf `RMakerLocalCtrlPayload` (CmdGetData, DataType=TypeConfig) | protobuf (RespGetData, fragmented) |
| `set_params`  | **raw JSON** (same body as the cloud set-params payload) | raw JSON: `{"status":"success"}` or `{"status":"fail","description":"…"}` |

Schema: [`local_ctrl.proto`](https://github.com/espressif/esp-rainmaker-neo-firmware/blob/main/components/esp_rmaker_neo/src/local_ctrl/local_ctrl.proto). The endpoint names
and the message/field numbering are the wire contract: treat them as frozen —
changing them breaks deployed clients.

The `ch_resp` endpoint (challenge-response, used for on-network user-node
association) is registered on the same instance when its endpoint set is
enabled, and appears in `cap`. A client-issued `DisableChalResp` command
**persists across reboots**: the endpoint is removed (after a short delay so
the response is flushed; a ch_resp-only instance is stopped entirely), later
enable attempts are refused, and the state is cleared only by a factory
reset.

## Fragmentation (get_params / get_config)

Client-pull, 200-byte fragments:

1. Client sends `CmdGetData{Offset: 0}` — the device (re)generates the full
   payload and caches it.
2. Response `RespGetData{Status, Buf{Offset, Payload, TotalLen}}` carries up to
   200 bytes at the requested offset; `TotalLen` is the full length.
3. Client repeats with `Offset += len(Payload)` until
   `Offset + len(Payload) == TotalLen`. The device frees the cache after
   serving the last fragment.

Notes: a request at offset 0 always regenerates; requests at a non-zero offset
without a prior offset-0 request fail with `Fail`. There is no per-session
cache — concurrent readers would clobber each other; clients must serialize
their own transfers.

## Reserved schema fields

`CmdGetData.Timestamp` / `CmdGetData.HasTimestamp` are reserved for a future
signed-response extension and are currently ignored by the device — responses
always carry the raw params/config JSON.

## Reference implementations

- Firmware: [`local_ctrl/`](https://github.com/espressif/esp-rainmaker-neo-firmware/tree/main/components/esp_rmaker_neo/src/local_ctrl) (handlers + proto), served by
  [`svc_local_endpoints.c`](https://github.com/espressif/esp-rainmaker-neo-firmware/blob/main/components/esp_rmaker_neo/src/services/svc_local_endpoints.c).
- Client: [`local_ctrl.py`](https://github.com/espressif/esp-rainmaker-neo-firmware/blob/main/tools/common/util/local_ctrl.py) (`LocalController`) and the
  interactive [`local_ctrl_cli/`](https://github.com/espressif/esp-rainmaker-neo-firmware/tree/main/tools/local_ctrl_cli).
