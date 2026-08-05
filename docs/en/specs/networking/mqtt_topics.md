# Complete MQTT Topic Reference

This section provides a comprehensive reference of all MQTT topics used by the firmware.

## Topic Variables

- **Thing Name**: synonymous with *node ID* / *client ID* used for MQTT connection.
  - `{thing_name}` in MQTT topics.
  - At most 128 characters.
- **Group Info String**: `<primary_group_id>[-<sub_group_id_1>-<sub_group_id_2>...]`
  - `{group_info_str}` in MQTT topics.
  - Group information is obtained from the ['getGroupInfo' event](cloud_communication.md#get-events).
  - Firmware limits: primary group ID up to 6 characters, each subgroup ID up to 3 characters, at most 3 subgroups — so the composed string is at most 18 characters. The whole topic is built into a 150-byte buffer; a topic that would not fit is treated as a build failure and the operation is skipped.
  - The subgroup IDs should be in alphabetical order from left to right. The subgrps array received from the cloud is not guaranteed to be in this order, and so local sorting should be done.
  - Access control is done by allowing access to MQTT topics with the group/subgroup IDs (e.g., topics matching `*<primary_group_id>*-<sub_group_id_1>*`).
  - **NOTE**: If the node is somehow not associated to any group, then **firmware will operate with empty group information string**:
    - e.g., [named shadow](../state_management.md#named-shadow) is created and maintained as "params-".
    - Once association is performed and the node receives new group information, then the [migration process](../state_management.md#migration-on-group-information-change) is obeyed, migrating from "" to the new string.

## Basic Ingest

Some publishes have two topic forms:

- **Basic-ingest topic**: prefixed with `$aws/rules/<rule_name>/`. The message is consumed directly by an AWS IoT rule and is **not** delivered to MQTT subscribers. Cheaper than a normal publish; used whenever subscribers do not need the message.
- **Non-basic-ingest topic**: standard MQTT topic without the `$aws/rules/<rule_name>/` prefix. The message is published normally and is visible to any subscribers.

Per-topic basic-ingest support is noted in each topic section below. Basic ingest is enabled by default (`CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST=y`); set it to `n` to fall back to the non-basic-ingest form for all supporting topics.

## Cloud Communication Topics

### To Cloud (Publish)

- **Topic** (supports [basic ingest](#basic-ingest)):
  - basic-ingest: `$aws/rules/node_to_cloud_rule/rainmaker/nodes/{thing_name}/to_cloud`
  - non-basic-ingest: `rainmaker/nodes/{thing_name}/to_cloud`
- **QoS**: QoS 1
- **Purpose**: Send events to the cloud (get/set events)
- **Payload**: JSON with event names and payloads

### From Cloud (Subscribe)

- **Topic**: `rainmaker/nodes/{thing_name}/from_cloud`
- **QoS**: QoS 1
- **Purpose**: Receive event responses and unsolicited events from the cloud
- **Payload**: JSON with event responses

## Parameter Control Topics

### Parameters to Node (Subscribe)

- **Topic**: `rainmaker/nodes/{thing_name}/user/params-{group_info_str}/params`
- **QoS**: QoS 1
- **Purpose**: Receive parameter update requests from users/applications
- **Payload**: JSON object with device ids and parameter values

## Group Control Topics

Group control topics allow a node to receive parameter updates addressed to its group or subgroup. The node subscribes to these topics in addition to the unicast [Parameters to Node](#parameters-to-node-subscribe) topic when it has valid group information (primary group ID from [getGroupInfo](cloud_communication.md#get-events)).

- **Primary group ID**: `{primary}` in the topic patterns below.
- **Subgroup ID**: `{subgroup}` for the subgroup-specific topic.

### Group Control Broadcast (Subscribe)

- **Topic**: `rainmaker/nodes/groups/{primary}/control`
- **QoS**: QoS 1
- **Purpose**: Receive control updates addressed to all devices in the primary group, addressed by device type
- **Payload**: See [Group Control Modifications](../state_management.md#group-control-modifications)

### Group Control Subgroup (Subscribe)

- **Topic**: `rainmaker/nodes/groups/{primary}/subgroups/{subgroup}/control`
- **QoS**: QoS 1
- **Purpose**: Receive control updates addressed to all devices in the specified subgroup, addressed by device type
- **Payload**: See [Group Control Modifications](../state_management.md#group-control-modifications)

A node subscribes to the broadcast topic and to one subgroup topic per subgroup in its [group info string](#topic-variables). Subgroup IDs in the topic must match the sorted subgroup list used in `{group_info_str}`.

## Shadow Topics

### Named Shadow Update (Publish)

- **Topic**: `$aws/things/{thing_name}/shadow/name/params-{group_info_str}/update`
- **QoS**: QoS 1
- **Purpose**: Update the named shadow with reported state
- **Payload**: AWS IoT Shadow JSON format

### Named Shadow Delete (Publish)

- **Topic**: `$aws/things/{thing_name}/shadow/name/params-{group_info_str}/delete`
- **QoS**: QoS 1
- **Purpose**: Delete the named shadow (used during group migration)
- **Payload**: `{}`

### Indexed Shadow Update (Publish)

- **Topic**: `$aws/things/{thing_name}/shadow/name/iparams/update`
- **QoS**: QoS 1
- **Purpose**: Update the indexed shadow with indexed parameters and tags
- **Payload**: AWS IoT Shadow JSON format

### Named Shadow Get (Publish) and Get Accepted (Subscribe)

- **Topics**:
  - request: `$aws/things/{thing_name}/shadow/name/params-{group_info_str}/get`
  - response: `$aws/things/{thing_name}/shadow/name/params-{group_info_str}/get/accepted`
- **QoS**: QoS 1
- **Purpose**: Read back the shadow's current `reported` document. The subscription is (re)established whenever the node sets up for new group information; the `get` publish is issued on demand. Only used when `CONFIG_RMNG_HOST_CTRL` is enabled.
- **Payload**: AWS IoT Shadow JSON format

### Indexed Shadow Get (Publish) and Get Accepted (Subscribe)

- **Topics**:
  - request: `$aws/things/{thing_name}/shadow/name/iparams/get`
  - response: `$aws/things/{thing_name}/shadow/name/iparams/get/accepted`
- **QoS**: QoS 1
- **Purpose**: Read back the indexed shadow's current `reported` document. Only used when `CONFIG_RMNG_HOST_CTRL` is enabled.
- **Payload**: AWS IoT Shadow JSON format

## Timeseries Topics

### Timeseries Report (Publish)

- **Topic** (supports [basic ingest](#basic-ingest)):
  - basic-ingest: `$aws/rules/node_ts_rule/rainmaker/nodes/{thing_name}/ts/{group_info_str}`
  - non-basic-ingest: `rainmaker/nodes/{thing_name}/ts/{group_info_str}`
- **QoS**: QoS 1
- **Purpose**: Publish timeseries data points
- **Payload**: JSON object with parameter path, data type, value, timestamp, and timezone

## Notification Topics

### Notifications (Publish)

- **Topic** (supports [basic ingest](#basic-ingest)):
  - basic-ingest: `$aws/rules/node_notify_rule/rainmaker/nodes/{thing_name}/notify/{group_info_str}`
  - non-basic-ingest: `rainmaker/nodes/{thing_name}/notify/{group_info_str}`
- **QoS**: QoS 1
- **Purpose**: Send direct notifications (e.g., automation trigger state updates)
- **Payload**: JSON object with notification groups and payloads

## Bridge Topics

Only present when `CONFIG_RMNG_BRIDGE_ENABLED` is set (default off). A bridge node proxies cloud connectivity for bridged children that have no MQTT connection of their own; `{bridge_thing_name}` is the bridge's own thing name.

- **Bridge To Cloud (Publish)**, supports [basic ingest](#basic-ingest):
  - basic-ingest: `$aws/rules/bridge_to_cloud_rule/rainmaker/bridges/{bridge_thing_name}/to_cloud`
  - non-basic-ingest: `rainmaker/bridges/{bridge_thing_name}/to_cloud`
- **Children From Cloud (Subscribe, filter)**: `rainmaker/bridges/{bridge_thing_name}/children/+/from_cloud`
- **Children Params (Subscribe, filter)**: `rainmaker/bridges/{bridge_thing_name}/children/+/user/+/params`
- **Group Control Subgroups (Subscribe, filter)**: `rainmaker/nodes/groups/{primary}/subgroups/+/control` — in bridge mode a single wildcard subscription replaces the per-subgroup ones, so the subscription count stays bounded regardless of subgroup membership.

Each bridged child is a full cloud Thing: its shadows, timeseries and notification topics use the ordinary node topic patterns above with the **child's** thing name and group info string.

## Subscription Patterns

### Initial Subscriptions

On first connection, the node subscribes to:

1.  [From Cloud](#from-cloud-subscribe) - Cloud events
2.  [Named Shadow Get Accepted](#named-shadow-get-publish-and-get-accepted-subscribe) - Shadow read-back (after group info received; only with `CONFIG_RMNG_HOST_CTRL`)
3.  [Parameters to Node](#parameters-to-node-subscribe) - Parameter updates (unicast; after group info received)
4.  [Group Control Topics](#group-control-topics) - Group control topics (when the node has valid group information)

### Subscription Timing

- **From Cloud**: Subscribed immediately after MQTT connection
- **Indexed Shadow Get Accepted**: Subscribed after the whole cloud-setup step (`from_cloud` subscription **and** the `get*` request bundle) has run — both are serial work-queue tasks queued on the first MQTT connection. Only when `CONFIG_RMNG_HOST_CTRL` is enabled
- **Named Shadow Get Accepted**: Subscribed when the node sets up for (new) group information, before the params subscription; only when `CONFIG_RMNG_HOST_CTRL` is enabled
- **Params Topic**: Subscribed after receiving group information
- **Group Control Topics**: Subscribed together with the params topic when group information includes a non-empty primary group ID; the node must be subscribed to all (unicast + broadcast + per-subgroup) before state-change listening is considered complete

### On Group Migration

When group information changes, the node unsubscribes from the old params topic, the old group control topics and the old named shadow `get/accepted` topic, then re-subscribes using the new group info string. If the **primary** group ID changed, the node instead forces an MQTT reconnect (to pick up the refreshed IoT policy) and lets the reconnect path re-establish the subscriptions.
