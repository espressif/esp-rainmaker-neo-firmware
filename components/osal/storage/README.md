# osal/storage

Key-value persistence, modelled on ESP-IDF NVS: partition → namespace → key.
Used for credentials, node state, timezone and anything else that must survive a
reboot.

## Public API

[`include/osal_storage.h`](include/osal_storage.h):

- Types: `osal_storage_handle_t`, `osal_storage_iterator_t`,
  `osal_storage_entry_t`, `osal_storage_open_mode_t` (read-only / read-write),
  `osal_storage_type_t` (`BINARY`, `U8`, `U16`, `I32`) and
  `OSAL_STORAGE_KEY_MAX_LENGTH` (16).
- Partition lifecycle: `osal_storage_init()`, `osal_storage_deinit()`,
  `osal_storage_reset()` (erase + deinit). A `NULL` label means the default
  partition.
- Handles: `osal_storage_open()`, `osal_storage_close()`,
  `osal_storage_commit()`.
- Values: `osal_storage_get()`, `osal_storage_set()`, `osal_storage_erase()`,
  `osal_storage_erase_all()`. For `OSAL_STORAGE_TYPE_BINARY`, passing a `NULL`
  value returns the required length in `*p_value_len`.
- Iteration: `osal_storage_entry_find()`, `osal_storage_entry_get_info()`,
  `osal_storage_entry_next()`, `osal_storage_release_iterator()`.
  `entry_get_info()` / `entry_next()` return `OSAL_ERR_NVS_KEY_NOT_FOUND` at the
  end of the partition.

Errors are the `OSAL_ERR_NVS_*` codes from `osal/errors`.

## Per-platform implementations

- ESP-IDF: [`src/nvs_esp_impl.c`](src/nvs_esp_impl.c) wraps `nvs_flash` / `nvs`
  and maps `ESP_ERR_NVS_*` onto `OSAL_ERR_*`. Note that `osal_storage_init()`
  handles a truncated partition by erasing and retrying — on
  `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` it calls
  `nvs_flash_erase()` and re-inits, so stale contents can be discarded rather
  than the call failing.
- POSIX: [`src/nvs_posix_impl.c`](src/nvs_posix_impl.c) is file-backed. Each
  partition/namespace pair is one file, `nvs_persistent/<partition>-<namespace>.bin`,
  holding an append-only sequence of `[u16 key_len][u32 val_len][u8 type][key][value]`
  records where the last record for a key wins. Access is serialized with a
  process-global mutex, and per-partition init state is tracked in a fixed table
  of 16 slots so deinitializing one label does not drop another (e.g. `fctry`
  versus the main partition).

The POSIX base directory is **relative to the process working directory**, which
is why a POSIX node must be run from the directory containing
`nvs_persistent/` (normally `build/`). A `NULL`/empty partition label maps to the
directory-name component `nvs` on POSIX.

## Build gating

Always built. Both sources are in the unconditional per-platform source lists and
`include/` is always on the interface include path.

Tests: [`test-nvs-common/`](test-nvs-common/), including partition-isolation
cases.
