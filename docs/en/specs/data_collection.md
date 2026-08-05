# Timeseries Data Collection

Timeseries data allows nodes to publish timestamped parameter values for analytics, monitoring, and historical tracking. Parameters with the time_series property in the node configuration can publish their values as timeseries data.

## Topic Structure

- **Topic**: See [Timeseries Report topic](networking/mqtt_topics.md#timeseries-report-publish)
  - `{thing_name}`: see ["Thing Name"](networking/mqtt_topics.md#topic-variables).
  - `{group_info_str}`: see ["Group Info String"](networking/mqtt_topics.md#topic-variables).
- **QoS**: QoS 1 (at least once delivery)

## Payload Format

Each timeseries data point is published as its **own** JSON object, in one MQTT message. The **``k``** (path) value is `"<device_id>.<param_id>"`, e.g. `"Light.Power"`.

The path separator is `.`; device and parameter ids must not contain `.`. The reporting key is `k` to keep the payload compact.

```javascript
{
    "k": <path>,                 // e.g. "Light.Power"
    "dt": <data type>,           // "int", "float", "bool", or "string"
    "v": <parameter value>,      // The actual value matching the data type
    "cumulative": <boolean>,     // true if value is cumulative (e.g., energy meter)
    "t": <timestamp>,            // Unix timestamp in milliseconds (UTC), int64
    "tz": <timezone IANA>        // IANA timezone string (e.g., "Etc/UTC", "America/New_York")
}
```

## Publishing Behavior

- A data point is enqueued when a parameter with the `time_series` property changes value. Enqueuing happens during the [state reporting flow](state_management.md#outgoing-state-reporting), but publishing does **not**: the queue is drained by its own throttled task.
- **One point per MQTT message.** Points are never merged into a single payload.
- **Throttling**: the drain task publishes one queued point, then reschedules after `CONFIG_RMAKER_TIMESERIES_PUBLISH_INITIAL_DELAY_MS` (default 100 ms). That success-path reschedule is flat — no jitter is applied. On a publish failure the point is requeued and the delay grows exponentially up to `CONFIG_RMAKER_TIMESERIES_PUBLISH_MAX_DELAY_MS` (default 300 000 ms), with up to one initial-delay's worth of random jitter (0--100 ms by default) added to each retry.
- **Bounded queue**: the queue holds `CONFIG_RMAKER_TIMESERIES_DATA_QUEUE_LENGTH` points (default 100). Points pushed to a full queue are dropped.
- **Timestamp validity**: the timestamp is captured when the parameter update is recorded, not at publish time. A point whose timestamp predates a valid wall clock is dropped rather than published (see [Time Synchronization](time_sync.md)).
- If MQTT budgeting is enabled and budget is exhausted, timeseries messages may be dropped.

## Data Type Handling

The `dt` field indicates the data type:

- `"int"`: Integer values
- `"float"`: Floating-point values
- `"bool"`: Boolean values (true/false)
- `"string"`: String values

The `v` field contains the value matching the specified data type.

Object and array parameters are **not** supported for timeseries — such points are rejected when pushed.

## Timezone Information

The `tz` field contains the IANA timezone string representing the timezone context for the timestamp. This allows the cloud to properly interpret the timestamp for display and analysis purposes.

It is read from the node's stored IANA timezone at push time (see [Timezone Service](services/optional.md#timezone)). A point cannot be pushed if no IANA timezone is stored.

## Notifications

**Direct notifications** from the node are *published* through an *MQTT topic*.

- **Topic**: See [Notifications topic](networking/mqtt_topics.md#notifications-publish)
  - `{thing_name}`: see ["Thing Name"](networking/mqtt_topics.md#topic-variables).
  - `{group_info_str}`: see ["Group Info String"](networking/mqtt_topics.md#topic-variables).
- **Payload**:

```javascript
{
    "notify": {
        <notification group>: <notification payload>,
        // ... one per notification group
    }
}
```

| Group | Payload |
|---|---|
| `"automation"` | See [trigger state updating payload](services/builtin.md#updating-trigger-state). |
