# osal/discovery

Advertises the node's local-control service on the local network, hiding the very
different backends behind one API: mDNS or Thread SRP on ESP-IDF, Avahi or
mDNSResponder on POSIX.

## Public API

[`include/osal_discovery.h`](include/osal_discovery.h):

- Types: `osal_discovery_service_config_t` (instance name, service type,
  protocol, port, TXT records), `osal_discovery_txt_item_t` /
  `osal_discovery_txt_items_t`, and `osal_discovery_transport_config_t` (today
  only `OSAL_DISCOVERY_TRANSPORT_HTTPD`).
- Lifecycle: `osal_discovery_init()` / `osal_discovery_deinit()`.
- Services: `osal_discovery_add_service()`, `osal_discovery_remove_service()`.
- Transport hooks: `osal_discovery_on_start()`, `osal_discovery_on_stop()`.

`osal_discovery_on_stop()` is a lifecycle notification, not a teardown call: the
header states only the SRP backend acts on it, and callers must still call
`osal_discovery_deinit()` to release resources.

## Per-platform implementations

One backend is compiled per build:

| Build | Source | Backend |
|---|---|---|
| ESP-IDF, `CONFIG_OSAL_DISCOVERY_DISCOVERY_TYPE_MDNS` | [`src/mdns_esp.c`](src/mdns_esp.c) | IDF `espressif/mdns` |
| ESP-IDF, `CONFIG_OSAL_DISCOVERY_DISCOVERY_TYPE_SRP` | [`src/srp_esp.c`](src/srp_esp.c) | OpenThread SRP client |
| POSIX, Avahi found | [`src/mdns_avahi.c`](src/mdns_avahi.c) | `avahi-client` (via pkg-config) |
| POSIX, `dns_sd.h` found | [`src/mdns_sd.c`](src/mdns_sd.c) | mDNSResponder |
| POSIX, neither found | [`src/mdns_stub.c`](src/mdns_stub.c) | none |

The POSIX backend is chosen at configure time in the osal `CMakeLists.txt`,
preferring Avahi over mDNSResponder; if neither is present CMake emits a warning
and the stub is used. The stub is not a silent no-op: `init()` and `on_start()`
log a warning and return `OSAL_ERR_FAIL`, and `add_service()` returns
`OSAL_ERR_NOT_SUPPORTED`, so a host without an mDNS daemon will not appear
discoverable.

`add_service()` is deliberately the odd one out. `OSAL_ERR_FAIL` means advertising
was attempted and broke; `OSAL_ERR_NOT_SUPPORTED` means this build has no mDNS
backend at all, which is a standing property of the platform rather than a fault.
Callers whose service stays reachable by IP treat the latter as non-fatal — see
`svc_local_ctrl.c` and `on_network_chal_resp.c`, which both warn and carry on.

## Build gating

Always built — there is no `OSAL_INCLUDE_*` switch. On ESP-IDF the
`OSAL_DISCOVERY_DISCOVERY_TYPE` Kconfig choice selects mDNS or SRP; the SRP
option requires `CONFIG_OPENTHREAD_ENABLED`.
