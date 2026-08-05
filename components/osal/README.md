# osal

The OS abstraction layer for the RMNG SDK: **one component**, exposing the
`osal_*` API, with per-area ESP-IDF and POSIX implementations behind it. Builds
as a single ESP-IDF component or a single POSIX static library.

| Area | Provides |
|---|---|
| `platform/` | Tasks, queues, semaphores, event groups, event loop, scheduler, time/ticks, random, CRC, log, mem, sysinfo, sysctrl |
| `errors/` | `osal_err_t` and error helpers |
| `storage/` | Key-value persistence (NVS) |
| `json/` | JSON generation/parsing (managed `json_generator`/`json_parser`) |
| `timesync/` | NTP/SNTP time synchronization + timezone DB |
| `netstatus/` | Network connectivity status |
| `discovery/` | mDNS / Thread SRP service discovery |
| `mqtt/` | MQTT client (coreMQTT-Agent based) |
| `ota/` | App-image partitions and boot slots |
| `http/`, `ext-io/`, `heap-monitor/` | Optional: HTTP client, serial I/O, heap monitoring (`OSAL_INCLUDE_*` / `CONFIG_OSAL_INCLUDE_*`) |
| `console/`, `ca-bundle/` | POSIX shims (native on ESP-IDF) |
| `protocomm/` | POSIX-compatible protocomm (separate static lib; see its [README](protocomm/README.md)) |

Layout convention: each area has `include/` (public headers) and `src/` with
platform-suffixed sources (`*_esp.c`, `*_posix.c`, `*_freertos.c`,
`*_common.c`); `platform/` keeps per-backend subdirectories.

Applications normally don't consume osal directly — it arrives transitively via
`esp_rmaker_neo`. Portable example/helper code (see `examples/common/`) links it for
tasks, logging, storage and the scheduler.
