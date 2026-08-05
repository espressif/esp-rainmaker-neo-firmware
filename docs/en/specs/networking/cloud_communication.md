# Cloud Communication

Communication with the cloud is achieved using specified *events*.

Publish to [to_cloud topic](mqtt_topics.md#to-cloud-publish):

```javascript
{
    "event": [ "<set_event_1>", "<get_event_2>", ... ],

    // include additional payload for each event that requires one
    "<set_event_1>": <set_event_1 payload>
}
```

Get a response by subscribing to [from_cloud topic](mqtt_topics.md#from-cloud-subscribe):

```javascript
{
    // these are the events sent previously
    "event": [ "<set_event_1>", "<get_event_2>", ... ],

    // payloads are returned by event name:

    // 'set' events receive an acknowledgement payload.
    "<set_event_1>": {
        "status": "success",            // ... and others
        "message": "<error message>"    // only available if status is not 'success'
    }

    // 'get' events receive an event-specific payload.
    "<get_event_2>": {
        "version": 0 // e.g., 'version' payload.
    }
}
```

## Response behaviour

The cloud side will send event payloads **as and when required**, i.e., not necessarily following a request-response model. This means:

- `get` events should be acted upon whenever received:
  - e.g., the node should update its group and subgroup IDs, and other related actions, whenever a `getGroupInfo` event is received. This is not necessarily fired from a previous `getGroupInfo` event sent to the cloud.
- `set` events still follow a request-response model, since an acknowledgement payload is sent from the cloud only if there was a prior event sent to the cloud to be acted upon.

## Version handshake

The two versioned services (schedules, automation triggers) use a paired `get*Ver` / `get*Details` handshake, and the node commits the new version to NVS only once it has both halves:

- A `get*Ver` whose version differs from the persisted one (or when nothing is persisted) marks the version as new. If the details have not arrived yet, the node queues a `get*Details` request in the same response cycle.
- A `get*Details` installs the details immediately and marks them new. If the version has not arrived yet, the node queues a `get*Ver` request.
- When both halves are new, the version is persisted and the handshake resets.
- A `get*Details` payload carries its own `version`. If it disagrees with the one from `get*Ver`, the **details** version wins.
- A version of `-1` (or an unparsable version) means "unknown": it is persisted as-is and the handshake resets, so the next `get*Ver` is treated as new.
- If the details key is absent from a `get*Details` payload, the node installs an empty set (`[]`) — that is how a service is cleared.

## Event names and payloads

### `get` events

No `get` event takes an additional payload. Each returns the payload shown below.

#### `getGroupInfo`

Gets the primary group ID (and any sub-group IDs) the node belongs to.

```javascript
{
    "pgrp": <primary group ID>,

    // only if node belongs to at least one subgroup
    // these are not necessarily sorted
    "subgrps": [<sub-group ID 1>, ...]
}
```

#### `getAlexaEn`

Check whether Alexa is enabled.

```javascript
{
    "enabled": true/false
}
```

#### `getGVAEn`

Check whether Google Voice Assistant notifications are enabled. Reported in the named shadow as [notify.gva](../state_management.md#named-shadow).

```javascript
{
    "enabled": true/false
}
```

#### `getSchedVer`

Gets the schedule version.

```javascript
{
    "version": <version number>
}
```

#### `getSchedDetails`

Gets the [schedule details](../services/builtin.md#schedules).

```javascript
{
    "version": <version number>,
    "Schedules": <JSON array of schedule objects>
}
```

#### `getTriggerVer`

Gets the trigger version.

```javascript
{
    "version": <version number>
}
```

#### `getTriggerDetails`

Gets the [automation trigger details](../services/builtin.md#automation-triggers).

```javascript
{
    "version": <version number>,
    "triggers": <JSON array of trigger objects>
}
```

#### `getTimeSync`

Gets the current server time, so the node can coarse-set its clock without waiting for SNTP (see [Time Synchronization](../time_sync.md)). Requested in the cloud handshake only when the clock is not yet valid at that time. The value is applied only if the system time is not already valid and the value itself is plausible (after the build-time reference floor); SNTP remains authoritative. Accuracy is bounded by cloud-to-node delivery latency.

```javascript
{
    "time": <server time, milliseconds since Unix epoch (UTC)>
}
```

### `set` events

#### `setNodeConfig`

Set the node configuration. The additional payload is the [node configuration payload format](../configuration.md#payload-format).

Return payload:

```json
{
    // ... and others
    "status": "success",
    // only available if status is not 'success'
    "message": "<error message>"
}
```

The node treats the exact string `"success"` as success; anything else is a failure, in which case `message` is surfaced to the requesting subsystem.

### Bridge events

Only present when `CONFIG_RMNG_BRIDGE_ENABLED` is set (default off). A bridge publishes these on its own `to_cloud` topic to manage the cloud Things of its bridged children:

```{list-table}
:header-rows: 1
:widths: auto

* - Event
  - Additional payload
  - Purpose

* - ``addChild``
  - ``{"request_id": "...", "child_suffix": "...", "child_local_id": "..."}``
  - Ask the cloud to create a Thing for a bridged child

* - ``removeChild``
  - ``{"request_id": "...", "child_node_id": "..."}``
  - Ask the cloud to remove a bridged child's Thing
```

Responses arrive on `from_cloud` as a `bridgeAck` event, correlated by `request_id`:

```javascript
{
    "request_id": <the request_id from the command>,
    "status": "success",                 // anything else is a failure
    "error": "<error message>",          // only when status is not 'success'
    "child_node_id": <assigned thing name>  // addChild success only
}
```

An ack whose `request_id` does not match a pending request is ignored (duplicate or stale ack). All other events (`getGroupInfo`, `setNodeConfig`, …) work per-child unchanged, addressed via the child's own topics.
