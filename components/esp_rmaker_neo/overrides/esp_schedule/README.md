# esp_schedule Integration

Build glue and an OSAL port for the registry component `espressif/esp_schedule ~1.5.0`. The component itself is not
vendored here — it is fetched from the ESP Component Registry.

Two things about this integration are non-obvious enough to be worth stating up front, because both are decisions
rather than defaults.

## esp_rmaker_neo owns schedule persistence, not esp_schedule

The port leaves `.nvs = {0}`, which disables the component's own storage. esp_schedule persists the *trigger config*
only — it has no idea what a fired schedule is supposed to do, because the action is a cloud JSON payload esp_rmaker_neo attaches
as `priv_data`. Letting the component persist would therefore restore schedules that fire and then do nothing.

So `services/schedules.c` stores the whole details JSON itself and re-serialises it from the live handles, which is also what carries each one-shot's
computed `ts` across a reboot. See the file header there for the pruning rules.

## Timers run on osal_scheduler, via the port

`esp_schedule_init_with_config(esp_schedule_port_osal_get(), ...)` is called instead of `esp_schedule_init()`, so the
component never installs its built-in ESP-IDF port. That does two things:

- schedule timers land on `osal_scheduler`, so firmware tests drive them from the **virtual scheduler** and can move
  the clock at will — the reason the arm/fire paths are unit-testable at all;
- the component's FreeRTOS-timer / `nvs_flash` / `esp_log` / SNTP dependencies stay out of the link.

`port/esp_schedule_port_osal.c` is SDK code, not part of the component, so it is compiled into esp_rmaker_neo — see
`esp_rmaker_neo/sources.cmake`, not the `CMakeLists.txt` here.

### The callback mutex is load-bearing

`__cb_mutex` in the port is not just a cancel barrier. `esp_schedule.h` requires that **at most one timer callback runs
at a time across all timers**: the component detects a schedule deleting itself from its own trigger callback via a
single global `s_dispatching`, and two concurrent dispatches would defeat that and free a schedule whose callback is
still running. The POSIX backend runs one thread per timer, so that serialization is not free — the mutex provides it.
It is recursive because the contract also allows a callback to call back into `start`/`cancel` on the same task.
