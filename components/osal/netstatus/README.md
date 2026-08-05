# osal/netstatus

A one-shot "wait until the device first has network connectivity" trapdoor. It
exists so startup code can block on first connectivity without knowing which
network stack (if any) manages the link.

## Public API

[`include/osal_netstatus.h`](include/osal_netstatus.h) — two functions, both
returning `int` (`0` on success):

- `osal_netstatus_arm()` — register the handlers that latch "connected" the first
  time an IP address is obtained. Call as early as possible at boot so the first
  event cannot be missed. Idempotent.
- `osal_netstatus_trap()` — block indefinitely until the latch has flipped, then
  unregister and release resources. Must follow a prior `arm()`.

The latch only ever moves from "not connected" to "connected"; later disconnects
are ignored, because steady-state reconnection is the transport's job.

## Per-platform implementations

- ESP-IDF: [`src/netstatus_esp.c`](src/netstatus_esp.c) allocates one bit per
  enabled network type — Wi-Fi STA, Thread, Ethernet — each guarded by the
  corresponding IDF option (`CONFIG_ESP_WIFI_ENABLED` /
  `CONFIG_ESP32_WIFI_ENABLED` / `CONFIG_ESP_WIFI_REMOTE_ENABLED`,
  `CONFIG_OPENTHREAD_ENABLED`, `CONFIG_ETH_ENABLED`) and served by its own event
  handler. `arm()` also latches an interface that is already connected;
  `trap()` unblocks as soon as *any one* network is connected.
- POSIX: [`src/netstatus_posix.c`](src/netstatus_posix.c) is an intentional
  no-op. The host network is already up and no connectivity event will ever be
  signalled, so `arm()` does nothing and `trap()` returns `0` immediately rather
  than blocking forever.

## Build gating

Always built. Both sources are in the unconditional per-platform source lists,
and `include/` is always on the interface include path.

On ESP-IDF the osal `CMakeLists.txt` adds `esp_wifi` / `esp_wifi_remote` /
`openthread` as optional private requirements only for the network types that
are actually enabled, so a Wi-Fi-only build does not drag in OpenThread.
