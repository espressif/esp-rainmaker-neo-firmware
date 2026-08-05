# osal/platform

The OS primitives everything else in the SDK is built on: tasks, queues,
semaphores, event groups, an event loop, a timer scheduler, time/ticks, RNG,
CRC32, logging, allocation and basic system info/control. FreeRTOS + ESP-IDF on
device, pthreads on the host.

## Public API

Headers in [`include/`](include/), one per primitive:

| Header | Provides |
|---|---|
| `osal_task.h` | `osal_task_create/delete/delay`, `osal_task_get_tick_count`, `osal_task_get_name` |
| `osal_queue.h` | `osal_queue_create`, `osal_queue_create_ext` (external RAM), `send`/`receive`/`peek`/`delete` |
| `osal_semaphore.h` | mutex / binary / counting create, `take`, `give`, `delete` |
| `osal_event_group.h` | `create`, `wait_bits`, `set_bits`, `clear_bits`, `get_bits`, `sync` |
| `osal_event_loop.h` | default loop: `create_default`, handler `register`/`unregister`, `osal_event_post` |
| `osal_scheduler.h` | one-shot / periodic timers: `schedule_task`, `schedule_task_periodic`, `reset_timer`, `stop_timer`, `cancel_task` |
| `osal_ticks.h`, `osal_tick_math.h`, `osal_timer_chain.h` | tick type + conversions, overflow-safe 64-bit tick maths, chaining a delay across several timer periods |
| `osal_time.h` | the swappable time provider: `osal_get_time`, `osal_get_time_ms` |
| `osal_random.h` | `osal_random_generate`, `..._generate_range`, `osal_random_fill` |
| `osal_crc.h` | `osal_crc32_generate`, `osal_crc32_validate` (IEEE 802.3, reflected) |
| `osal_log.h`, `osal_log_level.h` | `OSAL_LOGE/W/I/D/V`, `OSAL_LOG_BUFFER_HEX` |
| `osal_mem_alloc.h` | `OSAL_MALLOC_EXTRAM`, `OSAL_CALLOC_EXTRAM`, `OSAL_REALLOC_EXTRAM`, `OSAL_STRDUP_EXTRAM` |
| `osal_sysinfo.h`, `osal_sysctrl.h` | `osal_sysinfo_get_fw_version`, `..._get_project_name`, `osal_sysctrl_reboot` |
| `osal_app_desc_posix.h` | POSIX-only embedded app descriptor (magic + CRC32), read by `osal/ota` |

## Per-platform implementations

- ESP-IDF: [`src/esp/`](src/esp/) — task, semaphore, queue, event group and ticks
  over FreeRTOS; event loop over `esp_event`; sysinfo, sysctrl, random; scheduler
  on either `esp_timer` or FreeRTOS software timers.
- POSIX: [`src/posix/`](src/posix/) — pthreads, condvar-based queues/semaphores/
  event groups, a worker-thread event loop, a per-timer thread scheduler built on
  `pthread_cond_timedwait()`, and `log_posix.c`.
- Shared: [`src/common/`](src/common/) (`random_common.c`, `crc_common.c`,
  `time_common.c`).

Behavioural differences worth knowing:

- **Task attributes are advisory on POSIX.** `osal_task_create()` calls
  `pthread_create()` with default attributes; `stack_depth` and `priority` are
  only recorded as metadata (used by `osal_task_get_name()`).
- **Ticks are milliseconds on POSIX** — the conversions in `ticks_posix.c` are
  identity and `OSAL_MAX_DELAY` is `UINT32_MAX`; on ESP-IDF they go through the
  FreeRTOS tick rate.
- **Reboot.** `osal_sysctrl_reboot()` calls `esp_restart()` on ESP-IDF; on POSIX
  it logs a warning and `exit()`s with `POSIX_EXIT_REBOOT` (see
  `posix/include/posix_exit_codes.h`), which `osal/ota`'s mock bootloader
  interprets as a restart request.
- **External RAM.** The `*_EXTRAM` macros prefer PSRAM on ESP-IDF when
  `CONFIG_SPIRAM_SUPPORT` plus a SPIRAM alloc option is set, falling back to
  plain `malloc`; on POSIX they are always the plain libc calls.
- **Logging.** ESP-IDF forwards to `ESP_LOG_LEVEL_LOCAL`, so IDF's runtime level
  control applies. POSIX prints coloured lines to `stdout` (errors to `stderr`)
  with a `clock()` timestamp, filtered at compile time against
  `CONFIG_LOG_MAXIMUM_LEVEL`.
- **Firmware version / project name.** ESP-IDF reads the app descriptor;
  POSIX generates `sysinfo_posix.c` from `sysinfo_posix.c.in` with `PROJECT_VER`
  / `PROJECT_NAME` at configure time and embeds the descriptor (with a
  build-time CRC32 from `src/posix/app_desc_crc.py`) in the same translation unit
  so the linker keeps it.

[`posix/include/`](posix/include/) additionally lets vendored ESP-IDF sources
compile on the host: `esp_log.h`, `esp_event.h`, `esp_random.h` and `esp_check.h`
map onto the `osal_*` equivalents, while `freertos/FreeRTOS.h`, `task.h` and
`queue.h` are deliberately empty (only `portmacro.h` defines anything —
`portMAX_DELAY`). `posix/non_bsd_include/queue_extended.h` supplies
`SLIST_FOREACH_SAFE` and is force-included on non-Apple platforms;
`posix/bsd_include/endian.h` is added on Apple.

## Build gating

Always built. Two internal choices affect which sources are compiled:

- `OSAL_TIMER_IMPL` (ESP-IDF Kconfig choice) — `CONFIG_OSAL_TIMER_IMPL_FREERTOS`
  (default, FreeRTOS software timers) or `CONFIG_OSAL_TIMER_IMPL_ESP`
  (`esp_timer`, higher precision but shared with peripherals such as Wi-Fi).
- `OSAL_USING_VIRTUAL_SCHEDULER` / `CONFIG_OSAL_USING_VIRTUAL_SCHEDULER` —
  replaces the platform scheduler with
  [`virtual_scheduler/`](virtual_scheduler/), which keeps real time ticking but
  allows manual control via `osal_time_control_set_time()` /
  `osal_time_control_advance_time()`. **It is forced on whenever
  `CMAKE_TESTING_ENABLED` is set**, and when it is on `src/common/time_common.c`
  is dropped in favour of the virtual provider.

Tests live in [`test-platform-common/`](test-platform-common/).
