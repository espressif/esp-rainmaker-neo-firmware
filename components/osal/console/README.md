# osal/console

A POSIX shim for the subset of the ESP-IDF `esp_console` API used by the SDK and
by vendored ESP-IDF components, so the same `esp_console_*` code compiles and
runs unchanged on the host.

## Public API

[`include/esp_console.h`](include/esp_console.h) — deliberately keeps the
ESP-IDF names and layouts rather than introducing `osal_*` ones:

- Types: `esp_console_cmd_t`, `esp_console_cmd_func_t`, `esp_console_config_t`,
  `esp_console_repl_config_t`, `esp_console_dev_uart_config_t`,
  `esp_console_repl_t`, plus the `*_DEFAULT()` initializer macros.
- Registry: `esp_console_init()`, `esp_console_deinit()`,
  `esp_console_cmd_register()`, `esp_console_register_help_command()`,
  `esp_console_run()`.
- REPL: `esp_console_new_repl_uart()`, `esp_console_start_repl()`.

## Per-platform implementations

POSIX only: [`src/esp_console_shim.c`](src/esp_console_shim.c) keeps a growable
command array and spawns a REPL task that reads lines from `stdin` with
`fgets()` and dispatches them. On ESP-IDF the native `esp_console.h` /
`esp_console` component is used instead, and this header is not on the include
path.

Behavioural gaps versus ESP-IDF, all documented in the header: no line editing,
history or hints (`max_history_len`, `history_save_path`, `hint_color`,
`hint_bold` are accepted and ignored); `esp_console_cmd_t.argtable` is accepted
for source compatibility but unused; the UART device config is ignored because
`stdin` is always the input. Only the line/argument limits from
`esp_console_config_t` and the REPL prompt/stack/priority are honoured.

## Build gating

Always built on POSIX (unconditionally added to the osal library); never built
on ESP-IDF.

Unit tests live in [`test-esp_console-posix/`](test-esp_console-posix/) and are
added when testing is enabled.
