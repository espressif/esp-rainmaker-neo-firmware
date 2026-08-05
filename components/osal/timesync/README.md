# osal/timesync

Wall-clock time and timezone handling: whether the clock is trustworthy, how to
set it, and how to translate a location string such as `Asia/Shanghai` into a
POSIX `TZ` string. Schedules and time-series data depend on it.

## Public API

[`include/osal_timesync.h`](include/osal_timesync.h):

- Config: `osal_timesync_config_t` (`server_name`, `sync_time_cb`, event-loop
  registration info for the `tz_changed` / `tz_posix_changed` events).
- Lifecycle: `osal_timesync_init()`, `osal_timesync_deinit()`,
  `osal_timesync_is_initialized()`.
- Clock validity: `osal_timesync_is_synced()`,
  `osal_timesync_epoch_ms_is_valid()`, `osal_timesync_wait_for_sync()`,
  `osal_timesync_set_time()` (coarse, best-effort, e.g. a time delivered from the
  cloud).
- Timezone: `osal_timesync_set_timezone()` (location string),
  `osal_timesync_set_timezone_posix()`, and the matching getters, which return
  heap strings the caller must free.
- Printing: `osal_timesync_get_local_time_str()`,
  `osal_timesync_print_current_time()`.

All the `int`-returning calls use `0` for success, negative for failure.

## Per-platform implementations

- Shared: [`src/timesync_common_impl.c`](src/timesync_common_impl.c) holds the
  validity check, `osal_timesync_set_time()` (`settimeofday()`), timezone set/get
  with persistence into the `timesync` storage namespace (keys `tz`, `tz_posix`),
  `TZ` + `tzset()` handling and event posting.
  [`src/timezone_db.c`](src/timezone_db.c) is the location → POSIX-`TZ` table
  (vendored from `micro_tz_db`, MIT-licensed — the only non-Apache-2.0 file
  here).
- ESP-IDF: [`src/time_esp.c`](src/time_esp.c) runs SNTP (`esp_sntp`, or
  `lwip/apps/sntp` below IDF 5.1) against a built-in server list
  (`time.google.com`, `time.cloudflare.com`, `pool.ntp.org`,
  `time.windows.com`), overridable via `config->server_name`.
- POSIX: [`src/time_posix.c`](src/time_posix.c) starts **no** time client. The
  host OS owns the clock, so `init()` just records whether the current clock is
  valid, `server_name` and `sync_time_cb` are unused, and
  `osal_timesync_wait_for_sync()` does not wait: it returns `0` if the clock is
  already valid and otherwise logs "check NTP configuration" and returns `-1`
  immediately.

`osal_timesync_set_time()` uses `settimeofday()` on both platforms, so on a POSIX
host it fails without sufficient privileges.

## Build gating

Always built: `timezone_db.c` and `timesync_common_impl.c` are in the shared
source list, with the per-platform file added alongside.

## Notes

- "Time is synced" means "the wall clock is past a reference floor", not "this
  SDK synchronised it" — so an externally managed clock counts as synced.
- That floor is **generated at build time**:
  [`include/osal_timesync_ref_time.h.in`](include/osal_timesync_ref_time.h.in) is
  configured with `string(TIMESTAMP ... UTC)` into a *public* binary include dir,
  so a clock earlier than the moment the project was configured is treated as
  invalid.
- `CONFIG_OSAL_TIMESYNC_DEFAULT_TZ` (default `"Asia/Shanghai"`) is the timezone
  applied when nothing is stored.
- `priv_include/osal_timesync_internal.h` is added as a private include dir of
  osal; the tests in [`test-timesync-common/`](test-timesync-common/) borrow it
  explicitly, while the generated reference-time header reaches them through
  osal's public includes.
