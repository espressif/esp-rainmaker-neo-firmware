# Controller Identity and Data Plane

How a RainMaker Neo controller authenticates, learns which group it controls, and reaches that group's nodes.

## Overview

The device X.509 certificate is the only identity. There is no session token and nothing to mint: the controller signs every REST call with SigV4 credentials that the AWS IoT Credential Provider issues for its certificate, and the cloud resolves the certificate behind those credentials to the node on every request. Revoking the certificate revokes everything.

```
MQTT <-- getGroupInfo {"pgrp": "<group_id>", "subgrps": [...]}           <-- learns its group here
MQTT <-- getServerConfig {"rmng-base": {ApiGatewayUrl, CredentialProviderEndpoint}}
cert --> IoT Credential Provider (controller role alias) --> SigV4 credentials
REST --> GET {ApiGatewayUrl}/v1/groups/<group_id>                        (SigV4-signed, service execute-api)
REST <-- { node_ids, subgroups }                                         <-- learns every shadow name
REST --> GET {ApiGatewayUrl}/v1/groups/<group_id>/nodes/<n>/config
MQTT --> shadow get + update observe + control publishes                 (scoped to <group_id> by the IoT policy)
```

## Configuration

The firmware feature is off by default. Set `CONFIG_ESP_RMAKER_NEO_CONTROLLER=y` to include it.

| Item | Source |
|---|---|
| REST API base URL | Server config `rmng-base.ApiGatewayUrl` (`ESP_RMAKER_SERVER_CONFIG_API_GATEWAY_URL`). |
| Credential Provider endpoint | Server config `rmng-base.CredentialProviderEndpoint`. |
| Role alias | `CONFIG_ESP_RMAKER_NEO_CONTROLLER_ROLE_ALIAS`, default `rmng-controller-role-v1`. Only a certificate carrying `rmng-controller-policy` may assume it; the cloud attaches that policy when it registers a node with the `controller` capability. |

The node requests the server config after each connection until it arrives. The controller waits for it on its first REST call, up to `CONFIG_ESP_RMAKER_NEO_CONTROLLER_SERVER_CONFIG_TIMEOUT_MS`. The base URL may carry a stage path, for example `https://abc123.execute-api.us-east-1.amazonaws.com/prod`; every path below is appended to it.

## Group

The controller's group is the node's own group. The cloud sends it in the `getGroupInfo` cloud event (see [Cloud Communication](../networking/cloud_communication.md#getgroupinfo)) on every connection, and pushes it unprompted when the node is added to a group, moved between subgroups or groups, or removed. `{}` means the node is in no group.

The SDK keeps the value across reboots. Firmware reads it with `esp_rmaker_get_stored_group_id()` and `esp_rmaker_get_stored_subgroups()`, and `RMAKER_EVENT_GROUP_INFO_UPDATED` reports a change. Until the node is in a group, every controller operation returns `ESP_RMAKER_NOT_FOUND` without contacting the cloud.

On a change of group, REST calls follow at once: the cloud checks the membership on every request. The IoT policy reads the `group_id` thing attribute when the connection opens, so MQTT follows only after a reconnect, which the SDK makes when the primary group changes. The controller then rebuilds its inventory and drops its subscriptions to the old group's shadows; see [Migration](#migration).

### Migration

A controller is a member of the group it drives, so the cloud reports a group change to it through the same `getGroupInfo` event every node gets. That event is what the firmware reacts to.

Every shadow name is derived from the group, so a migration invalidates the whole inventory. On the change the firmware:

1. Rebuilds the inventory from `GET /v1/groups/{group_id}`, with the group that the event just stored.
2. Unsubscribes every shadow filter the new inventory does not use. A leftover filter is replayed on every reconnect, and the refreshed IoT policy no longer authorizes the old group's shadows.
3. Reports the rebuild as a change list to the callback registered with `esp_rmaker_controller_watch_changes()`. A node that stays in the group keeps its handle. A node that leaves is freed and reported REMOVED, and its handle goes stale: each call rejects it.

The firmware is told on **every** `getGroupInfo`, not only on the change, so a re-sync that failed because MQTT was down is retried on the next one. Nothing happens until the application has synced the group at least once: with no inventory held there is nothing to invalidate.

When the node leaves its group (`getGroupInfo` reports `{}`), there is no new group to rebuild from. The firmware drops the inventory and every shadow filter it held, and calls the same callback.

A migration that also changes the node's primary group forces an MQTT reconnect, which refreshes the device policy. The re-sync races that reconnect and may fail to subscribe; the next `getGroupInfo` after the reconnect completes it.

## Data APIs

The controller role grants `GET` on `/v1/groups/*` and nothing else. API Gateway refuses any other method, the bare `GET /v1/groups`, and every route outside `/v1/groups/`.

| Operation | Request |
|---|---|
| Read the group, with its `node_ids`, `node_details` and `subgroups` | `GET /v1/groups/{group_id}` |
| Read a node's config | `GET /v1/groups/{group_id}/nodes/{node_id}/config` |
| Read a node's schedules, triggers, tags, time series | `GET /v1/groups/{group_id}/nodes/{node_id}/{schedules,triggers,tags,timeseries/...}` |

There is no `GET /v1/groups/{group_id}/nodes` route: the group response lists the nodes. The group's users and automations are not readable by a controller.

| Status | Meaning | Firmware result |
|---|---|---|
| `404` | No such resource, or a group the controller cannot access; the two are indistinguishable. | `ESP_RMAKER_NOT_FOUND` |
| `401` | The node is in no group, or the signer is not an attached certificate. | `ESP_RMAKER_INVALID_STATE` |
| `403` | A method or route outside the grant. | `ESP_RMAKER_INVALID_STATE` |
| `403`, "security token ... expired" | The SigV4 credentials expired. | Fresh credentials are fetched and the request replayed once. |

The signer re-fetches the credentials ahead of their expiry, by the device clock, so an expired-credentials `403` points at a clock that drifted.

### Group Response

`GET /v1/groups/{group_id}` returns the full group, not just its node ids (abridged):

```json
{ "group_id": "grp123", "group_name": "Home", "access_type": "controller",
  "node_ids": ["n1", "n2"],
  "subgroups": [ { "subgroup_id": "sgA", "subgroup_name": "Kitchen", "node_ids": ["n1"] } ] }
```

Every node's subgroup path is therefore already on the wire, which is what lets the firmware derive every shadow name itself. An application never supplies one.

The firmware records `group_name` and reports it through `esp_rmaker_controller_get_group_name()`. A body that carries no `group_name` leaves the name empty.

`node_ids` lists the controller itself, because a controller is a member of the group it drives. The firmware drops its own node id while it builds the inventory, in both `node_ids` and every subgroup's `node_ids`, so the node list an application iterates holds only the nodes it drives.

### Node Config Response

The `data_model` field selects how the body is read. The firmware parses **only `"default"`**, and rejects anything else before building a tree; the check is an allow-list, so a data model added later is rejected rather than half-parsed. A body with no `data_model` field predates the field and reads as `"default"`. In practice that means a Matter node in the group is listed but never parsed.

For `"default"`, the firmware walks both `devices[]` and `services[]` into one list, flagging the latter, and reads each device and param id from `id`, accepting `name` as an alias so a body from either producer parses. `data_type` maps to a value type with the same string table the node's own config payload emits, and `ui_type` maps to an `esp_rmaker_ui_type_t` the same way. A device's `primary` is recorded as the id it names and resolved to a param on every read.

## MQTT Topics

A node's params live in a named shadow. The shadow name is derived from the group, and from the subgroup path when the node sits under one.

| Node placement | Shadow name |
|---|---|
| Directly in the group | `params-<group_id>` |
| Under subgroups | `params-<group_id>-<subgroups>`, where `<subgroups>` is the dash-joined subgroup path |

The subgroup ids in a shadow name are **sorted alphabetically**. The firmware sorts them, so a group response listing them in any order still addresses the right shadow.

| Operation | Topic |
|---|---|
| Request a node's params | `$aws/things/<node_id>/shadow/name/<shadow_name>/get` |
| Receive them | `$aws/things/+/shadow/name/<shadow_name>/get/accepted` |
| Observe live changes | `$aws/things/+/shadow/name/<shadow_name>/update/accepted` |
| Set one node's params | `rainmaker/nodes/<node_id>/user/<shadow_name>/params` |
| Set the whole group's params | `rainmaker/nodes/groups/<group_id>/control` |
| Set one subgroup's params | `rainmaker/nodes/groups/<group_id>/subgroups/<subgroup_id>/control` |

The controller's IoT policy authorizes any thing's `params-<group_id>` shadows, so one `+` subscription covers every node sharing a shadow name. A controller subscribes once per distinct shadow name rather than once per node. Each shadow name costs two filters, and those share one table with the agent's own filters, bounded by `CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS`.

### Live State

The firmware observes `update/accepted` group-wide, so the cached values track the group continuously rather than only when the application asks.

`update/accepted` is what it merges, not `update/documents`: a node publishes only the params that changed ([State Management](../state_management.md), "Partial Updates"), and `update/accepted` echoes exactly that delta. `update/documents` would carry `previous` and `current` in full on every message.

Per message, the firmware reads only `state.reported` -- the same topic carries desired-state writes, which are a request rather than the node's state:

| Field | Handling |
|---|---|
| `params` | Merged into the cached values. A param the message omits keeps its value. |
| `online` | Recorded on the node. A transition is reported through `esp_rmaker_controller_watch_nodes()`. |
| `ncfg_ver` | Compared for inequality against the recorded token. A change means the node's data model changed, so the firmware re-fetches the config when it holds one, then reads the shadow to seed the new params. |

A delta never carries unchanged params, so one shadow `get` per node is still needed to seed full state. After that the subscription keeps it current.

The `get` reply carries the same three fields, so it marks the config stale on a moved `ncfg_ver` exactly as an `update/accepted` does. Reading state before fetching a config therefore says whether the fetch is owed at all, which is the order `esp_rmaker_controller_node_sync()` runs: a re-sync of an unchanged node costs one `get` and no config `GET`.

`ncfg_ver` is treated as an opaque change token and never parsed: [State Management](../state_management.md) calls it a 64-character lowercase SHA-256 hex string, while the User MQTT AsyncAPI declares it an integer.

### Write Payloads

The two planes key params differently, and the firmware owns the difference.

| Plane | Topic | Payload |
|---|---|---|
| One node | `rainmaker/nodes/<node_id>/user/<shadow_name>/params` | `{"<device id>":{"<param id>":v}}` |
| Group or subgroup | `rainmaker/nodes/groups/<group_id>[/subgroups/<subgroup_id>]/control` | `{"<device type>":{"params":{"<param type>":v}}}` |

A node write is keyed by device and param **id**; a group write by **type**, wrapped in a `params` envelope. The group plane has no wildcard device type. The receiving node matches the key against each device type exactly, so a group write reaches only the devices of the one type it names.

**The group plane reaches services exactly as it reaches devices.** The receiving node iterates its combined device and service list and filters on the service flag in neither mode, so a hub can set a whole group's timezone in one publish:

```json
{"esp.service.time": {"params": {"esp.param.tz": "Asia/Shanghai"}}}
```

The firmware refuses a write to a param whose config lacks the `write` property. The node does not enforce this -- it parses whatever the payload names -- so this is client-side hygiene that turns a silently-ignored publish into an immediate error. A group write takes bare type strings with no param handle, so it cannot make that check.

## Standby

`esp_rmaker_controller_set_active(false)` puts the controller on standby: it keeps reading but refuses to set params. The switch is local to the device; the cloud has no such state.

## Access Control

The cloud scopes a controller to the one group it belongs to, from authoritative state: the REST checkpoint reads the node's current group on every request, and the IoT policy reads the `group_id` thing attribute, which only the cloud sets. The device asserts nothing about its group. Both planes enforce the scope independently.
