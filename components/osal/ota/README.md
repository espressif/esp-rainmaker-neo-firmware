# osal/ota

Abstracts app-image partitions and boot-slot selection for firmware updates. On
ESP-IDF it wraps the native `esp_ota_*` / `esp_partition` APIs; on POSIX it
emulates OTA slots with files plus a mock bootloader, so the OTA logic in
`esp_rmaker_neo_ota` can be exercised on a host.

## Public API

[`include/osal_ota.h`](include/osal_ota.h):

- Types: `osal_ota_partition_t` (label, address, size, type, subtype,
  encrypted), `osal_ota_handle_t`, `osal_ota_img_states_t`,
  `osal_ota_app_desc_t`, `osal_ota_hash_type_t` (SHA-256 / MD5), and the
  `OSAL_OTA_SIZE_UNKNOWN` / `OSAL_OTA_WITH_SEQUENTIAL_WRITES` sentinels.
- Write path: `osal_ota_begin()`, `osal_ota_resume()` (continue from an offset
  without erasing), `osal_ota_write_with_offset()`, `osal_ota_end()`,
  `osal_ota_abort()`.
- Partition queries: `osal_ota_get_boot_partition()`,
  `..._get_running_partition()`, `..._get_next_update_partition()`,
  `..._get_last_invalid_partition()`, `..._get_app_partition_count()`,
  `..._get_partition_description()`, `..._get_partition_hash()`,
  `..._get_state_partition()`.
- Boot / rollback: `osal_ota_set_boot_partition()`,
  `osal_ota_mark_app_valid_cancel_rollback()`,
  `osal_ota_mark_app_invalid_rollback_and_reboot()`,
  `osal_ota_erase_last_boot_app_partition()`,
  `osal_ota_check_rollback_is_possible()`.

## Per-platform implementations

- ESP-IDF: [`src/osal_ota_esp.c`](src/osal_ota_esp.c), over `esp_ota_ops` /
  `esp_partition` / `app_update`.
- POSIX: [`src/osal_ota_posix.c`](src/osal_ota_posix.c) plus
  [`src/osal_ota_posix_config.c`](src/osal_ota_posix_config.c). Two fixed slots
  (`ota_0`, `ota_1` — see
  [`priv_include/osal_ota_posix_shared.h`](priv_include/osal_ota_posix_shared.h))
  are backed by files under `partitions/` in the working directory, rotated
  round-robin; boot slot, last-valid slot and per-slot state persist in
  `ota_config.bin`. Partition hashes are computed with the PSA API from the
  vendored mbedTLS. Because there is no `esp_app_desc_t` at a fixed offset, the
  image's own descriptor is embedded via a custom section and magic
  (`platform/include/osal_app_desc_posix.h`) and located by scanning the file.

Where POSIX genuinely differs in behaviour:
`osal_ota_erase_last_boot_app_partition()` returns `OSAL_ERR_NOT_SUPPORTED`
(no flash to erase), `osal_ota_check_rollback_is_possible()` always returns
`false`, `osal_ota_get_last_invalid_partition()` always returns `NULL`, and
`osal_ota_mark_app_invalid_rollback_and_reboot()` records the state and then
`exit()`s with `POSIX_EXIT_REBOOT`.

That exit code is what makes reboots work: `mock_bootloader_posix` — built here
as a separate executable — `fork()`s and `execv()`s the file for the configured
boot slot, forwards terminating signals, and relaunches on exit code
`POSIX_EXIT_REBOOT`. If the configured slot's file is missing it falls back to
the last-valid slot, which is how rollback is observed on the host.

## Build gating

Always built. On ESP-IDF the source is compiled into the osal component; on
POSIX [`CMakeLists.txt`](CMakeLists.txt) here builds an `osal_ota` static
library (linked publicly by osal) plus the `mock_bootloader_posix` executable,
and links mbedTLS, which is fetched at configure time by
`third_party/install_scripts/mbedtls.cmake`.

The POSIX sub-build exports the bootloader name, partition folder and default
first partition as global properties; `esp_rmaker_neo_ota` re-exports them as
`RMNG_OTA_POSIX_*` for examples to use.
