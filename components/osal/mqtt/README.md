# osal/mqtt

The MQTT client behind the SDK's cloud connection. The public surface is a
vtable of function pointers, so the same callers work over either backend
(coreMQTT-Agent or esp-mqtt), plus a shared layer for events, topic matching and
subscription bookkeeping.

## Public API

- [`include/osal_mqtt_prototypes.h`](include/osal_mqtt_prototypes.h) — the types:
  `osal_mqtt_QoS_t`, `osal_mqtt_conn_params_t` (host/port, ALPN, credentials,
  client cert/key, `ds_data`, `clean_session`), `osal_mqtt_lwt_t`,
  `osal_mqtt_event_loop_registration_info_t`, `osal_mqtt_event_loop_channel_t`,
  and `osal_mqtt_impl_t` — the vtable of `init`, `deinit`, `connect`,
  `disconnect`, `drop`, `force_reconnect`, `publish`, `subscribe`,
  `unsubscribe`.
- [`include/osal_mqtt_impl.h`](include/osal_mqtt_impl.h) —
  `osal_mqtt_impl_setup()` to populate the vtable, plus `osal_mqtt_pre_init()` /
  `osal_mqtt_post_deinit()` used by the backends.
- [`include/osal_mqtt_events.h`](include/osal_mqtt_events.h) — event-group bits
  (`OSAL_MQTT_NETWORK_CONNECTED_BIT`, `OSAL_MQTT_CLIENT_CONNECTED_BIT`, ...) and
  set/clear/wait helpers.
- [`include/osal_mqtt_subscription_manager.h`](include/osal_mqtt_subscription_manager.h)
  — tracks topic filter → callback, dispatches incoming publishes
  (`osal_mqtt_subscription_handle_publish()`) and can resubscribe or simulate
  SUBACKs after a reconnect.
- [`include/osal_mqtt_util.h`](include/osal_mqtt_util.h) —
  `osal_mqtt_match_topic()` (MQTT wildcard matching).
- [`include/osal_mqtt_config.h`](include/osal_mqtt_config.h) — maps the Kconfig
  values to `configOSAL_MQTT_*` macros.

## Per-platform implementations

The common sources (`osal_mqtt_impl.c`, `osal_mqtt_events.c`,
`osal_mqtt_util.c`, `osal_mqtt_subscription_manager.c`) are shared; the backend
providing `osal_mqtt_impl_setup()` differs:

- [`src/osal_mqtt_core_impl.c`](src/osal_mqtt_core_impl.c) — coreMQTT-Agent,
  built when `CONFIG_OSAL_MQTT_IMPL_CORE` is set. **The only backend available
  on POSIX**, where [`CMakeLists.txt`](CMakeLists.txt) here builds an
  `osal_mqtt` static library and pulls in the vendored `backoffAlgorithm` and
  `coreMQTT-Agent`.
- [`src/osal_mqtt_esp_impl.c`](src/osal_mqtt_esp_impl.c) — esp-mqtt, ESP-IDF
  only, built when `CONFIG_OSAL_MQTT_IMPL_ESP` is set.

Real behavioural difference: `drop()` (ungraceful disconnect, so the broker
publishes the LWT) is fully supported by the coreMQTT backend, which tears the
TLS transport down directly. With esp-mqtt it is best-effort only — the public
API cannot close TCP without first sending a DISCONNECT, so the implementation
falls back to a graceful disconnect, logs a warning, and the LWT is **not**
published.

TLS trust anchors also differ, in the vendored transport port under
`libraries/coreMQTT-Agent/port/`: `esp/network_transport.c` attaches
`esp_crt_bundle`, `posix/network_transport.c` uses mbedTLS with the bundle from
`osal/ca-bundle`.

## Build gating

Always built — there is no `OSAL_INCLUDE_*` switch.

[`Kconfig.mqtt`](Kconfig.mqtt) carries the `OSAL_MQTT_IMPL` choice plus
subscription-manager size, agent task stack/priority, keep-alive interval
(range 40–1200 s; the lower bound is 40 rather than AWS IoT's 30 to avoid
spurious disconnects) and per-backend buffer/timeout options.
`CONFIG_OSAL_MQTT_CORE_FORCE_IPV4` is POSIX-only and exists for hosts whose IPv6
egress is black-holed.

## Notes

- `backoffAlgorithm` and `coreMQTT-Agent` under `libraries/` are **fetched at
  configure time** by `libraries/makeAvailable.cmake` at pinned commits; only the
  wrappers, config headers and the transport port are committed.
- Buffers pointed to by `osal_mqtt_conn_params_t` (and the LWT) must stay alive
  until the client is deinitialized; the implementation does not copy them.
- Tests: [`test-mqtt-common/`](test-mqtt-common/), added by the test app that
  needs them rather than from this `CMakeLists.txt`.
