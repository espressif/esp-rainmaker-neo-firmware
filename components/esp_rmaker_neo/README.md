# esp_rmaker_neo

The main RMNG SDK component: node lifecycle, data model, cloud connectivity and
services for ESP RainMaker Neo firmware. Public API lives in
[`include/`](include/) (`esp_rmaker_core.h`, `esp_rmaker_data_model.h`, …);
everything under `priv_include/` and `src/` is internal.

## Consuming

- **ESP-IDF**: make the component visible (e.g. `EXTRA_COMPONENT_DIRS`) and add
  `esp_rmaker_neo` to your `PRIV_REQUIRES`. Dependencies (`esp_rmaker_neo_common`, `osal`, managed
  components) resolve via the manifest.
- **POSIX**: `add_subdirectory(components/esp_rmaker_neo)` and
  `target_link_libraries(<app> PRIVATE esp_rmaker_neo)`. Configuration uses the same
  Kconfig system (`menuconfig` target, `SDKCONFIG_DEFAULTS`).

See the [firmware guides](https://docs.neo.rainmaker.espressif.com/docs/firmware/)
for setup/build walkthroughs and [`docs/en/specs/`](../../docs/en/specs/index.md)
for the node behavior specification. A node needs a factory NVS partition
before it can connect — see
[factory NVS generation](../../tools/factory_nvs_gen/README.md).

## Source layout

`src/` is organized by feature: `node/`, `network/` (MQTT connection, shadows,
reporting), `data_model/`, `services/` (schedules, timezone, system),
`bridge/`, `chal_resp/`, `prov_helpers/`, `console/`, `host_ctrl/`
(serial-driven control plane, `CONFIG_RMNG_HOST_CTRL`). Managed-component glue
lives in [`overrides/`](overrides/README.md); test-only socket fault injection in
[`fault_injection/`](fault_injection/README.md); unit tests in `test_rmaker_neo/`.

## Notable options

- `CONFIG_RMNG_HOST_CTRL` — drive the SDK over serial
  ([host control](src/host_ctrl/host_ctrl_python/README.md)); selects heap
  monitoring and the virtual scheduler.
- `CONFIG_RMNG_BRIDGE_ENABLED` — bridged child devices.
- `CONFIG_RMNG_CONSOLE_ENABLED` — RMNG serial console commands (default on).
