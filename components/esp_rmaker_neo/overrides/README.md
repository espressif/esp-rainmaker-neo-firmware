# Overrides

Build glue for upstream ESP Component Registry components consumed on
both ESP-IDF and POSIX. Nothing here is vendored source: each subdirectory
fetches its component from the registry at configure time and then *overrides
how it is built* so the same target name and the same entry points exist
off-target as on-target.

## Contents

- [`esp_schedule/`](esp_schedule/) — `espressif/esp_schedule`, built without its
  built-in ESP-IDF port so schedule timers run on `osal_scheduler` and are
  drivable from the virtual scheduler in tests. See its [README](esp_schedule/README.md).
- [`rmaker_console/`](rmaker_console/) — `espressif/rmaker_console`, built against the osal console
  shim, plus a portable implementation of the common commands.
