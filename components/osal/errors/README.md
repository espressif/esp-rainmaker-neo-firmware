# osal/errors

Defines `osal_err_t`, the return type used throughout the SDK's common
components. The numeric values are identical on both platforms, and on ESP-IDF
they are laid out to coincide with `esp_err_t` so the two can be mixed without
translation.

## Public API

- [`include/osal_err_values.h`](include/osal_err_values.h) — the shared numeric
  values (`OSAL_ERR_VAL_*`), grouped into ranges: core (`FAIL`, `OK`, `NO_MEM`,
  `INVALID_ARG`, `INVALID_STATE`, `TIMEOUT`, `NOT_FOUND`, `NOT_SUPPORTED`,
  `INVALID_RESPONSE`, aligned with `esp_err_t` `0x101`–`0x107`), HTTP
  (`0xD001–`), MQTT (`0xD010–`), NVS (`0xD020–`), OTA (`0xD030–`), and the
  RainMaker range based at `0x7000` (same space as `ESP_ERR_RMAKER_BASE`).
- `osal_err.h` — the `osal_err_t` typedef, the `OSAL_ERR_*` constants and
  `osal_err_strerror()`. Two variants exist, one per platform (see below).
- [`include/posix/esp_err.h`](include/posix/esp_err.h) — a POSIX `esp_err_t` /
  `ESP_OK` / `ESP_ERROR_CHECK()` shim so vendored ESP-IDF code compiles on the
  host. `ESP_ERROR_CHECK()` prints to `stderr` and `abort()`s.

## Per-platform implementations

The include path picks the header: ESP-IDF adds
[`include/esp-idf/`](include/esp-idf/osal_err.h), where `osal_err_t` is a
typedef of `esp_err_t` and the core constants alias the `ESP_ERR_*` macros;
POSIX adds [`include/posix/`](include/posix/osal_err.h), where `osal_err_t` is
`int32_t` and every constant is cast from `OSAL_ERR_VAL_*`.

[`src/os_err_strerror.c`](src/os_err_strerror.c) is shared. The one behavioural
difference is the fall-through for unrecognised codes: on ESP-IDF it defers to
`esp_err_to_name()`, so IDF-native errors still stringify; on POSIX it returns
the literal `"Unknown error"`.

## Build gating

Always built. `os_err_strerror.c` is in the platform-independent source list and
the include directory is unconditional.
