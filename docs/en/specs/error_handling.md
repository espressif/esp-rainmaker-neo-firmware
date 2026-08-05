# Error Handling and Recovery Strategies

## General Error Handling Approach

The node implements error handling for various failure scenarios:

- **MQTT Operations**: Failed publish/subscribe operations are retried by the owning subsystem's retry context; the retry policy is per-operation, not global
- **Shadow Updates**: A failed shadow publish schedules a retry at the **same** scope as the failed attempt; a failed PUBACK schedules a retry that re-reports **full** state
- **Event Publishing**: Failed event publishing is retried where a retry context exists (cloud setup, node configuration) and logged otherwise
- **Partial Failures**: The node continues operating even if some operations fail

## Common Backoff Shape

Most retry contexts share one backoff shape: an initial delay, doubling per attempt (×2), capped at **5 minutes**, plus up to **1 s** of random jitter, and retrying **forever**. Individual contexts override the initial delay; the time-sync poll overrides the whole shape to a flat cadence.

## Retry Strategies

### MQTT Connection Retry

- **Automatic Retry**: Connection failures trigger automatic reconnection with exponential backoff
- **Retry Forever**: Reconnection attempts continue indefinitely
- **Backoff Algorithm**: With coreMQTT, base delay `CONFIG_OSAL_MQTT_CORE_RETRY_BACKOFF_BASE_MS` (default 500 ms) up to `CONFIG_OSAL_MQTT_CORE_RETRY_MAX_BACKOFF_DELAY_MS` (default 5 000 ms), with no attempt limit. With `esp-mqtt`, reconnection is handled by that client.

### Shadow Update Failures

- **Automatic Retry**: A publish error, or a PUBACK reporting failure, schedules another state report using the common backoff shape (initial delay = `CONFIG_RMAKER_STATE_REPORT_DELAY_MS`)
- **Publish Error Keeps the Scope**: A synchronous publish failure leaves the node's pending-update list and change flags untouched, so the retry repeats the original scope (changed-only stays changed-only). Nothing is lost — the same updates are still queued
- **PUBACK Failure Escalates to a Full Report**: A publish that is accepted locally but reports failure at PUBACK time has already had its pending list and change flags cleared, so the retry re-reports **all** parameters
- **Tag Checksum Not Committed**: For the indexed shadow, the node-tag checksum is only committed on a successful PUBACK, so tags are re-emitted on the retry
- **State Consistency**: The node continues with local state even if shadow updates fail

### Timeseries Publish Failures

- **Requeued**: The point is pushed back onto the timeseries queue and the drain task is rescheduled with exponentially growing delay, up to `CONFIG_RMAKER_TIMESERIES_PUBLISH_MAX_DELAY_MS`
- **Dropped**: A point whose MQTT topic cannot be built is dropped, not retried

### Event Publishing Failures

- **Cloud setup** (`from_cloud` subscribe + the `get*` handshake bundle) has its own retry context. If it ultimately cannot run, cloud information is reset to defaults (empty group info, notifications disabled, versions unknown) and the corresponding event flags are cleared
- **Node configuration** (`setNodeConfig`) has its own retry context. Both a publish failure and a cloud error response leave the entry pending so the next retry tick republishes. The stored checksum is only advanced after a clean acknowledgement — and if the configuration changed while the publish was in flight, the acknowledgement is treated as stale and the fresh document is republished
- **Other Events**: Logged on failure and not retried
- **Error Responses**: Set events receive `status`/`message` responses from the cloud indicating failure

## Error Recovery Flows

### Critical Failures

- **MQTT Connection Loss**: Triggers automatic reconnection flow
- **Network Loss**: Node attempts to reconnect when network is restored
- **Initialization Failures**: `esp_rmaker_node_init()` tears down and returns an error; a start-task failure leaves the SDK in the error state (see [Initialization and Startup Sequence](initialization.md))

### Partial Failures

- **One Shadow Succeeds, Another Fails**: Each shadow update is a separate publish, but they are not independent — a failed *named* publish short-circuits the indexed publish for that node entirely, and the whole node is retried at the original scope. Only the reverse split (named accepted, indexed failed) or an asymmetric PUBACK outcome produces a genuine one-succeeds-one-fails state; the PUBACK case then escalates to a full report
- **Some Parameters Update, Others Fail**: Parameter updates are processed independently
- **Service Data Load Failures**: Schedules and automation triggers differ here — a malformed *schedule* entry is skipped with the rest of the set still installed, whereas a malformed *trigger* aborts the whole install and the node keeps its previous trigger set. A failure to load the persisted set at startup aborts the start task

## Error Reporting

- **No Explicit Error Topic**: There is no dedicated error reporting topic. Errors are handled locally and logged; the only error state that reaches the cloud is what the shadow payloads and notifications already carry
