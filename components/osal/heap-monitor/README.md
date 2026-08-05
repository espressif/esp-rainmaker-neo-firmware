# osal/heap-monitor

Reports heap totals for diagnostics (used, for example, by the host-control heap
command). Device-only in substance: there is no host equivalent, so the POSIX
side is a stub.

## Public API

[`include/osal_heap_monitor.h`](include/osal_heap_monitor.h):

- `osal_heap_monitor_common_status_t` — `total_size`, `allocated_size`,
  `free_size`, `largest_block_size`, `lowest_free_size`, all in bytes.
- `osal_heap_monitor_common_print_status(tag)` — log a human-readable summary.
- `osal_heap_monitor_common_get_status(&status)` — returns `bool`.

## Per-platform implementations

- ESP-IDF: [`src/heap_monitor_esp.c`](src/heap_monitor_esp.c) uses
  `heap_caps_get_info()`. It sums `MALLOC_CAP_INTERNAL` and (when
  `CONFIG_SPIRAM` is set) `MALLOC_CAP_SPIRAM` — disjoint regions, so no
  double-counting — and reduces `largest_block_size` with max() rather than a
  sum, since one allocation cannot span regions. `print_status()` logs one block
  per region.
- POSIX: [`src/heap_monitor_posix.c`](src/heap_monitor_posix.c) is a stub.
  `print_status()` does nothing. `get_status()` zeroes the struct **and returns
  `false`** — deliberately, so "no instrumentation" is not mistaken for a
  genuinely empty heap. Callers must check the return value instead of reading
  the struct unconditionally.

## Build gating

Optional and **off by default**: `OSAL_INCLUDE_HEAP_MONITOR` (CMake) /
`CONFIG_OSAL_INCLUDE_HEAP_MONITOR` (Kconfig, `default n`). Enabling it adds the
per-platform source and the `include/` directory.

Note that the Kconfig option also `select`s `HEAP_TRACING_ENABLE_STANDALONE` and
`HEAP_PLACE_FUNCTION_INTO_FLASH`, although the implementation itself only calls
`heap_caps_get_info()`.
