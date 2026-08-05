# State Management

## Incoming State Modifications

These are requests to change one or more logical device parameters from sources external to the node.

- **Source**: User applications, Alexa, Google Home, or cloud automations etc.

- **Topic**: See [Parameters to Node topic](networking/mqtt_topics.md#parameters-to-node-subscribe) (The group-specific params topic)

  - `{thing_name}`: see ["Thing Name"](networking/mqtt_topics.md#topic-variables).
  - See [named shadows](#named-shadow) for `{shadow_name}` convention.

- **Payload Specification**: A JSON object where keys are logical device ids and values are objects of parameter-value pairs.

  ```javascript
  {
    "<device id>": {
      "<parameter id>": <new value>,
      // ... other parameters for this device
    },
    // ... other devices with modified parameters
  }
  ```

- **Node Action**: Upon receiving this message, the node should apply the changes and then [report its new state](#outgoing-state-reporting).

## Group Control Modifications

Requests to change parameters on **all devices of a given type** within a group or subgroup, addressed by device type rather than device id.

- **Source**: Cloud group/subgroup control flows.

- **Topic**: See [Group Control Broadcast](networking/mqtt_topics.md#group-control-broadcast-subscribe) and [Group Control Subgroup](networking/mqtt_topics.md#group-control-subgroup-subscribe).

- **Payload Specification**: Top-level keys are **device types** (not device ids). Each value is a control envelope; only the `params` envelope is currently defined. Inside `params`, keys are **parameter types** and values are the new param values.

  ```javascript
  {
    "<device type>": {
      "params": {
        "<parameter type>": <new value>,
        // ... other parameter types for this device type
      }
    },
    // ... other device types
  }
  ```

  Example:

  ```json
  {
    "esp.device.light": { "params": { "esp.param.power": true, "esp.param.brightness": 75 } },
    "esp.device.fan":   { "params": { "esp.param.power": false } }
  }
  ```

- **Node Action**: Apply the changes and then [report new state](#outgoing-state-reporting). For each top-level device-type key matching a device on the node, apply the params to every such device. Device-types not present on the node are ignored. A device-type entry without a `params` envelope is silently skipped (reserved for future envelopes). Cross-device-type filtering is enforced on the node.

## Outgoing State Reporting

### Reporting `online` state

The node should set a state variable `online = true` to signal that it is in a state to receive commands (i.e. all subscriptions and initial handshake is complete). This is part of the [shadow payloads](#shadows). Clearing it is a cloud responsibility — see [When online = false is Set](#when-online--false-is-set).

## Node Online/Offline State Management

### Conditions for Setting `online = true`

The node sets `online = true` only after all of the following conditions are met:

1.  **SDK Started**: the start task has completed, which implies the MQTT connection was established (see [Initialization and Startup Sequence](initialization.md))
2.  **Node Registered**: a node has been registered with the SDK
3.  **Cloud Subscription**: Successful subscription to [from_cloud topic](networking/mqtt_topics.md#from-cloud-subscribe)
4.  **Params Subscription**: Successful subscription to the [Parameters to Node topic](networking/mqtt_topics.md#parameters-to-node-subscribe) **and** to every applicable [group control topic](networking/mqtt_topics.md#group-control-topics) — the params flag is only set once the last of those SUBACKs arrives

The check runs on each event that could satisfy it. All checks must pass before the node reports `online = true` in both named and indexed shadows; if any check fails, the local online flag is cleared instead (without publishing `online = false`).

### When `online = false` is Set

The cloud sets `online = false` when it detects the node has disconnected (via AWS IoT presence events). The node never publishes it: on an MQTT disconnection it clears its local online flag only.

### Online State and Operations

`online = true` signals readiness, but gates nothing on the node. Parameter updates are received and processed, and state changes are reported, regardless of the flag — including before it is set and after a disconnection clears it.

### Shadows

State reporting is done by reporting on **two AWS IoT shadows** per node. (With `CONFIG_RMNG_BRIDGE_ENABLED`, each bridged child is a cloud Thing of its own and so has its own pair of shadows, reported over the bridge's connection.)

#### Shadow Update Behavior

Shadow updates are triggered when:

- Parameter values change (local or remote)
- Node configuration changes (`ncfg_ver` update, queued after the cloud acknowledges `setNodeConfig`)
- Online state changes
- Node tags are updated (indexed shadow only)

Notification settings do **not** trigger a report on their own: `notify` is attached to the next report that carries parameter updates.

**Update Timing**:

- **Coalescing Window**: A state change schedules the report after `CONFIG_RMAKER_STATE_REPORT_DELAY_MS` (default 500 ms). Any further change inside that window restarts the timer, so a burst of updates collapses into one report
- **Batching**: All changes pending when the timer fires go into a single shadow update per shadow
- **Separate Updates**: Named shadow and indexed shadow are published as two independent MQTT messages

**Partial Updates**:

- **Delta Updates**: Only changed parameters are included in shadow updates (not full state)
- **Selective Reporting**:
  - Named shadow includes all changed parameters
  - Indexed shadow includes only changed parameters with `indexed` property. The `params` object is omitted entirely when none of the pending changes are `indexed`
- **Empty Payloads Skipped**: A shadow whose payload carries nothing beyond the `state.reported` wrapper is not published
- **Full State on Startup**: On initial connection, full state is reported to both shadows

**Update Frequency and Throttling**:

- **Coalescing Only**: Beyond the coalescing window above there is no rate limit on shadow updates
- **MQTT Budgeting Impact**: If MQTT budgeting is enabled, shadow updates may be dropped when budget is exhausted
- **Multiple Changes**: Multiple parameter changes trigger a single shadow update containing all changes

**Shadow Update Acknowledgment**:

- **QoS 1**: Shadow updates use QoS 1 for at least-once delivery
- **Accepted Topics**: The node subscribes to the shadow `get/accepted` topics (see [Shadow Topics](networking/mqtt_topics.md#named-shadow-get-publish-and-get-accepted-subscribe)) but does not require acknowledgment before proceeding
- **Publish Failure**: A publish that fails synchronously keeps the node's pending-update list and change flags, so the retry repeats the *original* scope rather than escalating to a full report (see [Error Handling and Recovery Strategies](error_handling.md))
- **PUBACK Failure**: A failed shadow PUBACK schedules a subsequent **full** state report, retried with exponential backoff (base = the coalescing delay, ×2 per attempt, capped at 5 minutes, with up to 1 s of jitter). For the indexed shadow the pending node-tag checksum is **not** committed, so the tag set is re-emitted in that full report

### Named Shadow

This shadow represents the complete state of the node within its group context.

- **Shadow Name**: `params-{group_info_str}`

  - `{group_info_str}`: see ["Group Info String"](networking/mqtt_topics.md#topic-variables).

- **Purpose**: Contains the full reported state of all logical device parameters. This is the primary source of truth for the node's state.

- **Update Topic**: See [Named Shadow Update topic](networking/mqtt_topics.md#named-shadow-update-publish)

  - `{thing_name}`: see ["Thing Name"](networking/mqtt_topics.md#topic-variables).

- **Payload Specification**: The wrapper is always `state.reported` with optional `online`, `ncfg_ver`, and `notify`. **``params``** is device id → { parameter id → value }:

  ```javascript
  {
    "state": {
      "reported": {
          // include to report current status of node
          "online": true,

          // include if node configuration has been updated
          "ncfg_ver": <node configuration checksum>,

          // include if any parameters are updated
          "params": {
              <device id>: {
                  <parameter id>: <parameter value>,
                  // ... other updated parameters for this device
              },
              // ... other devices with updated parameters

              // include if any notification setting is enabled
              "notify": <notify_payload>
          }
      }
    }
  }
  ```

  - `ncfg_ver`: The SHA-256 checksum of the [node configuration payload](configuration.md#payload-format), as a 64-character lowercase hex string (no `0x`). It is a change token, not a timestamp — the cloud compares it for equality and needs no valid wall clock on the node. It is emitted on the next state report **after** the cloud has acknowledged the corresponding `setNodeConfig` for that configuration.
  - `notify` is nested **inside** `params`, and is therefore only emitted when the report also carries at least one parameter update.

#### Notify Payload

The `notify` field in the named shadow provides metadata about notification-related configurations and settings. This allows the cloud to track notification preferences and versions without requiring separate MQTT communications.

- **Purpose**: Contains notification configuration state and version information for various notification services.
- **Scope**: Named shadow only, and only for the device's own node. The indexed shadow never carries `notify`.
- **When emitted**: Only when at least one notification integration is enabled (`alexa` or `gva`). If all are disabled, the whole `notify` object is omitted.
- **Payload Structure**:

```javascript
{
    "version": <version number>,
    <notification type>: <configuration value>,
    // ... additional notification types as needed
}
```

| Field | Description | Type |
|---|---|---|
| `version` | Millisecond monotonic timestamp divided by 10, i.e., in hundredths of a second. Uniqueness is the only requirement, so this is deliberately not gated on time synchronisation — before the clock is valid it is boot-relative rather than a UNIX timestamp. The version number must change from the previous update in order to trigger cloud actions. | integer |
| `alexa` | Indicates whether Alexa notifications are enabled for this node. Follows the value received from [getAlexaEn](networking/cloud_communication.md#get-events). | boolean |
| `gva` | Indicates whether Google Voice Assistant notifications are enabled for this node. Follows the value received from [getGVAEn](networking/cloud_communication.md#get-events). | boolean |

Both `alexa` and `gva` are always present whenever `notify` is emitted, even when only one of them is enabled.

**Note**: Additional notification types may be added in the future to support other smart home platforms or custom notification services.

#### Migration on group information change

The group information can be modified at any time, e.g., when the node is moved from one group to another. When this happens, based on the new group information, the node migrates the *named* shadow document:

1.  Delete the old named shadow document (publish `{}` to the old [delete topic](networking/mqtt_topics.md#named-shadow-update-publish)), while the old group info string is still in effect.
2.  Unsubscribe from the old params topic, the old group control topics and the old named shadow `get/accepted` topic.
3.  Persist the new group info string, which is what constructs the new named shadow name.
4.  Re-subscribe on the new group info string and send a full state update (as per start-up), which creates the new named shadow document.

Two special cases:

- **Primary group ID changed**: step 4 is *not* run inline. Instead the node forces an MQTT reconnect so the broker re-evaluates the device's IoT policy for the new primary group, and the reconnect path performs the subscriptions and the full report.
- **Group info unchanged**: if the node is not currently subscribed to the params topic (for example after a restart where the string was already in NVS), step 4 is still run so the subscriptions and full report are established.

The **indexed** shadow is never migrated: its name is static (`iparams`).

### Indexed Shadow

This shadow holds a small, curated subset of the node's state for fast querying and filtering by the backend.

- **Shadow Name**: `iparams` (static)

  - Unlike named shadows, this indexed shadow is not accessed by the user and has no group/subgroup IDs in its name for access control.

- **Purpose**: Contains only parameters with the `indexed` property in the node configuration, as well as top-level node tags.

- **Update Topic**: See [Indexed Shadow Update topic](networking/mqtt_topics.md#indexed-shadow-update-publish)

  - `{thing_name}`: see ["Thing Name"](networking/mqtt_topics.md#topic-variables).

- **Payload Specification**: Same wrapper as the named shadow, with `data`, `online`, `ncfg_ver` and `params`. There is **no** `notify` in the indexed shadow. **``params``** is device id → { parameter id → value }, restricted to parameters with the `indexed` property.

  ```javascript
  {
    "state": {
      "reported": {
          // include if the node's tag set has changed
          "data": {
              "device": {
                  "t": {
                      // node-updated tags go here
                      <tag name>: <tag value>
                  }
              }
          },

          // include to report current status of node
          "online": true,

          // include if node configuration has been updated
          "ncfg_ver": <node configuration checksum>,

          // include if any indexed parameters are updated
          "params": { /* structure as above, indexed params only */ }
      }
    }
  }
  ```

`online` and `ncfg_ver` carry exactly the same values as in the named shadow — both shadows are populated from the same pending-update list in one pass.

#### Node Tags

Node tags are included in the indexed shadow to allow for fleet indexing by attributes other than the node's parameters, and are under `state : reported : data : <who> : t`, with the following possible keys for `<who>`:

- `admin`: tags set by administrators
- `device`: tags set by the node
- `user`: tags set by users of the node

In firmware, only `device` tags are updated.

Behaviour:

- **Change detection by checksum**: the node hashes its sorted `name:value;` tag list and compares it against the last committed hash. When they differ, the **entire** current tag set is emitted under `data.device.t` — there is no per-tag delta. When they match, `data` is omitted.
- **Reserved names**: `name`, `type`, `fw_version` and `model` are reserved. They are populated automatically from the node's [info](configuration.md) and cannot be set as application tags.
- The committed hash is only updated once the indexed shadow publish is acknowledged, so a failed publish re-sends the tag set.
