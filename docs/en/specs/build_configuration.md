# Build-Time Configuration

Every build-time option in this SDK is a Kconfig symbol. The names follow a small set of rules.

This page describes the naming rules. For what an individual option does, read its help text.

## Prefixes

A symbol's prefix names the component that **defines** it, not the components that read it:

| Prefix | Owner |
| --- | --- |
| `ESP_RMAKER_NEO_` | the node components: `esp_rmaker_neo`, `esp_rmaker_neo_common`, `esp_rmaker_neo_ota` |
| `OSAL_` | the OS abstraction layer `osal`, and every area under it |
| `APP_` | the example helper components |
| `TEST_` | the test-only components |

Vendored code and copies of ESP-IDF symbols keep the upstream name.

## Migration from older names

A component's `sdkconfig.rename` maps old symbols to the current ones. The ESP-IDF build then converts an old `sdkconfig` without manual edits. A file has one or both of these blocks:

- Symbols that an earlier RainMaker Neo release used.
- RainMaker classic symbols with a 1:1 equivalent, for example `CONFIG_ESP_RMAKER_SCHEDULING_MAX_SCHEDULES` to `CONFIG_ESP_RMAKER_NEO_SCHEDULING_MAX_SCHEDULES`.

A classic symbol with a different meaning, type or default has no entry. Set its Neo equivalent by hand.

The POSIX build does not read `sdkconfig.rename`. Update a POSIX `sdkconfig` by hand.

Two migrations need manual edits also on ESP-IDF:

- `espressif/rmaker_app_network` defaults `APP_NETWORK_FACTORY_NVS_PARTITION_NAME` and `APP_NETWORK_PROV_DATA_NAMESPACE` to the classic `ESP_RMAKER_FACTORY_*` names. A project that rebuilds from its `sdkconfig.defaults` gets the literal string `"ESP_RMAKER_FACTORY_PARTITION_NAME"`, and cannot open the factory partition. Add the four lines from `examples/common/app_network/README.md` to `sdkconfig.defaults`. A project that keeps its `sdkconfig` is not affected.
- Neo has no self claiming. In classic, `ESP_RMAKER_ASSISTED_CLAIM` is one option of the `ESP_RMAKER_CLAIM_TYPE` choice. The rename keeps the entry for earlier Neo projects, so a classic project with self claiming gets `CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM=n` and no device credentials. Set `CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM=y`, or pre-flash the device credentials.
