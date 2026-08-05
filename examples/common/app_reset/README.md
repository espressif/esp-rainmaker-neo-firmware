# app_reset

Hold-to-reset on the application button: hold past one threshold for a **network reset**,
keep holding past a second for a **factory reset**.

## Enabled by default

`APP_RESET_ENABLED` defaults to **y**, so any example that registers this on its button ships
with both resets active:

| Hold duration | Action |
|---|---|
| `APP_RESET_NETWORK_TIME_MS` (default **5 s**) | **Network reset** — wipes network credentials, reboots after `APP_RESET_REBOOT_S` (default 2 s) |
| `APP_RESET_FACTORY_TIME_MS` (default **10 s**) | **Factory reset** — wipes RMNG data and network credentials, then reboots |

A factory reset does **not** erase the factory NVS partition, so device credentials survive.
Set `APP_RESET_ENABLED=n` to disable both.

The two thresholds are latched, so releasing after crossing into factory-reset territory does
**not** also fire the network reset.

## Usage

```c
#include "app_reset.h"

app_reset_button_register(my_button_handle);   /* ESP-IDF only */
```

`app_reset_button_register()` attaches the thresholds to a button you already created, and
registers the network-reset routine with RMNG so the SDK and the `reset-network` console
command can trigger it too. It is a no-op returning `OSAL_ERR_OK` when the feature is
disabled, so the call needs no `#if`.

If your application has its own long-press action on the same button, skip it while
`app_reset_hold_in_progress()` is true — otherwise a user holding for a reset also triggers
the long press on release. `app_button` does this for you.

## Platforms

`app_reset_button_register()` is ESP-IDF only: there is no physical button on the host.
`app_reset_hold_in_progress()` exists on both and always returns false on POSIX, so portable
code can call it unguarded.

## Relationship to esp-rainmaker

This is the RMNG counterpart of `espressif/rmaker_app_reset`, and deliberately not a
consumer of it. Upstream calls esp-rainmaker's `esp_rmaker_wifi_reset()` /
`esp_rmaker_factory_reset()`, which do not exist in this SDK; the resets here go through
RMNG's own `esp_rmaker_system_ctrl_*` API. Upstream also configures its timeouts as
call arguments in seconds and has no threshold-crossing feedback, where this component is
Kconfig-driven in milliseconds and logs each threshold as it is crossed.
