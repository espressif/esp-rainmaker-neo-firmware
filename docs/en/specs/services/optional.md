# Optional Services

## Timezone

### Overview

Provides device-wide timezone management exposed as a service named `Time` with parameters `TZ` (IANA timezone) and `TZ-POSIX` (POSIX TZ string). Both parameters are readable and writable.

### Service and Parameters

- Service name: `Time`
- Parameters (under `params : Time`):
  - `TZ` (type string): IANA timezone name, e.g., `America/New_York`
  - `TZ-POSIX` (type string): POSIX timezone string, e.g., `EST5EDT,M3.2.0,M11.1.0`

Both parameters have properties: `read`, `write`.

### Behavior

1)  Incoming parameter updates (from cloud)

- When `TZ` is written:
  - Node applies the timezone using the IANA name.
  - On success, node updates local state and derives the corresponding POSIX value, updating `TZ-POSIX` as well.
  - On failure, no state change is reported for either parameter.
- When `TZ-POSIX` is written:
  - Node applies the timezone using the provided POSIX string.
  - On success, node updates local state for `TZ-POSIX`.
  - The IANA `TZ` will not change since it cannot be derived from the POSIX string; only `TZ-POSIX` is guaranteed to update.

Whenever possible, it is preferred to **update ``TZ`` instead of ``TZ-POSIX``**.

2)  Local updates (from device)

- When the device changes timezone locally (e.g., via internal API):
  - Node emits a timezone change event that updates the corresponding parameter(s) in the service state, ensuring shadows reflect the change.

3)  State reporting

- After successful changes (from cloud or local), node publishes updated parameters in the named shadow under `params : { "Time": { ... } }`.
- Indexed shadow is not used for timezone parameters unless explicitly marked `indexed` in node configuration (default: not indexed).

### Examples

1)  Write only `TZ` from cloud

```text
// Incoming params request
{
  "Time": {
    "TZ": "America/New_York"
  }
}

// Expected named shadow reported
{
  "Time": {
    "TZ": "America/New_York",
    "TZ-POSIX": "EST5EDT,M3.2.0,M11.1.0"
  }
}
```

2)  Write only `TZ-POSIX` from cloud

```text
// Incoming params request
{
  "Time": {
    "TZ-POSIX": "CAT-2"
  }
}

// Expected named shadow reported
{
  "Time": {
    "TZ-POSIX": "CAT-2"
  }
}
```

3)  Local update to IANA timezone

```text
// Local API updates TZ to America/New_York
// Expected named shadow reported
{
  "Time": {
    "TZ": "America/New_York",
    "TZ-POSIX": "EST5EDT,M3.2.0,M11.1.0"
  }
}
```

### Error Handling

- If applying a timezone fails, the node should not update `TZ` or `TZ-POSIX` in reported state for that request.
- When `TZ` is accepted and applied but the POSIX string cannot be derived from it, `TZ` is still updated in reported state and `TZ-POSIX` is left unchanged.

## System

### Overview

Provides device-wide system control operations exposed as a service named `System` with parameters for reboot, network reset, and factory reset. All parameters are boolean values that trigger actions when set to `true`.

### Service and Parameters

- Service name: `System`
- Parameters (under `params : System`):
  - `Reboot` (type boolean): Triggers device reboot when set to `true`
  - `Network-Reset` (type boolean): Triggers network reset when set to `true`
  - `Factory-Reset` (type boolean): Triggers factory reset when set to `true`

All parameters have properties: `read`, `write`.

### Behavior

1)  Incoming parameter updates (from cloud)

- When `Reboot` is set to `true`:
  - Node initiates a reboot sequence after the configured delay
- When `Network-Reset` is set to `true`:
  - Node resets network configuration and may reboot after the configured delays
- When `Factory-Reset` is set to `true`:
  - Node performs a factory reset, clearing all configuration and may reboot after the configured delays

2)  State reporting

- After successful parameter writes, the node reports the parameter value back in the named shadow under `params : { "System": { ... } }`
- Indexed shadow is not used for system parameters unless explicitly marked `indexed` in node configuration (default: not indexed)

### Configuration

The system service is enabled with a configuration struct whose `flags` field selects which params the service exposes. A param that is not selected is not created at all, so it does not appear in the node configuration:

- `SYSTEM_SERV_FLAG_REBOOT`: Exposes the `Reboot` param
- `SYSTEM_SERV_FLAG_NETWORK_RESET`: Exposes the `Network-Reset` param
- `SYSTEM_SERV_FLAG_FACTORY_RESET`: Exposes the `Factory-Reset` param
- `SYSTEM_SERV_FLAGS_ALL`: All three

At least one flag is required. The rest of the configuration sets the timing and the platform hook:

- `reboot_seconds`: delay before the reboot triggered by `Reboot`
- `reset_seconds`: delay before a network/factory reset takes effect
- `reset_reboot_seconds`: delay before the reboot that follows a reset; a negative value means "do not reboot"
- `network_reset_fn`: callback that clears the platform's network credentials. Required (non-NULL) whenever the `NETWORK_RESET` or `FACTORY_RESET` flag is set

### Examples

1)  Trigger reboot from cloud

```text
{
  "System": {
    "Reboot": true
  }
}
```

2)  Trigger network reset from cloud

```text
{
  "System": {
    "Network-Reset": true
  }
}
```

3)  Trigger factory reset from cloud

```text
{
  "System": {
    "Factory-Reset": true
  }
}
```

### Error Handling

- If a system operation fails to initiate, the parameter write should be rejected and no state change reported.

## Local Control

### Overview

Lets a client control the node over the local network, with no cloud round trip. This
section covers the **service the node reports to the cloud**; the on-the-wire protocol a
client speaks is specified separately in
[Local Control Endpoint Protocol](../local_ctrl_endpoint_protocol.md), which is the authority on endpoints,
framing and discovery.

- Service name: `Local Control`
- Transport: HTTP (protocomm)

### Service and Parameters

| Parameter | Type | Access | Description |
|---|---|---|---|
| `Type` | int | read-only | protocomm security version in use: `1` or `2` |
| `POP` | string | read-only | Proof of Possession for the session |
| `Username` | string | read-only | SRP6a username; present only with security version `2` (the default) |

`POP` is reported for both security versions. Whether a PoP is actually *required* comes
from the `no_pop` capability on the version endpoint, not from this parameter — a
security-1-without-PoP node reports an empty string here.

Security 0 is not supported.

### Behavior

- The service is added to the node when local control is enabled, and removed when it is
  disabled.
- Challenge-response for on-network user-node association shares the same protocomm
  instance and session; it is an independently enable-able endpoint set, not a separate
  service. See the endpoint protocol spec for the four enabled/disabled combinations.
- The HTTP port defaults to `CONFIG_ESP_RMAKER_LOCAL_CTRL_HTTP_PORT`.
- The PoP is generated once and persisted, unless the application sets its own with
  `esp_rmaker_local_ctrl_set_pop()` before enabling the service — which is how a device
  reuses the PoP printed on it (see the guides on factory provisioning).
