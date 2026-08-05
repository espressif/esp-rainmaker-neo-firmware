# osal/ext-io

A byte-oriented external serial channel used to drive the node from a host tool
(see `esp_rmaker_neo`'s host_ctrl). ESP-IDF talks over a UART; POSIX exposes a
pseudo-terminal instead.

## Public API

[`include/osal_ext_io.h`](include/osal_ext_io.h) — four functions:
`osal_ext_io_init()`, `osal_ext_io_deinit()`, `osal_ext_io_write_line()` and the
blocking `osal_ext_io_read_until_sync(buffer, max_len, until_char, &read_len)`.
All return `bool`, not `osal_err_t`.

[`esp/include/osal_ext_io_packet_constants_esp.h`](esp/include/osal_ext_io_packet_constants_esp.h)
defines the framing bytes (header, trailer, received-ping) shared with the host
side; it is only on the include path for ESP-IDF builds.

## Per-platform implementations

- ESP-IDF: [`src/osal_ext_io_esp.c`](src/osal_ext_io_esp.c), over
  `CONFIG_OSAL_EXT_IO_ESP_UART_PORT_NUM`. When that port is `0` (the default —
  the same UART as the console monitor) the implementation runs in *multiplexed*
  mode: every write is wrapped in header + big-endian `uint16` length + trailer,
  a recursive mutex serializes RPC frames against IDF log output so they cannot
  interleave, and the write is retried until the host echoes the received-ping
  (5 attempts, 500 ms each) — otherwise it returns `false`. On a non-zero port
  the bytes are written raw and the call always reports success.
- POSIX: [`src/osal_ext_io_posix.c`](src/osal_ext_io_posix.c) creates a PTY with
  `openpty()`, sets the slave side to raw mode, closes the slave fd, logs the
  slave path and also writes it to `port.out` in the current working directory
  for tools to pick up. Only Linux and macOS are supported; elsewhere `init()`
  logs an error and returns `false`. Writes go straight to the master fd with no
  framing and no acknowledgement, and no CRLF is appended despite the header's
  description.

## Build gating

Optional and **off by default**: `OSAL_INCLUDE_EXT_IO` (CMake) /
`CONFIG_OSAL_INCLUDE_EXT_IO` (Kconfig, `default n`). When enabled, ESP-IDF
compiles the UART source into osal and adds a private dependency on
`esp_driver_uart`; POSIX builds [`CMakeLists.txt`](CMakeLists.txt) here as a
separate `osal_ext_io` static library that osal links publicly.

[`Kconfig.ext_io`](Kconfig.ext_io) holds the UART port, baud rate, buffer size
and RX/TX pins. Its symbols are ESP-only — the POSIX PTY path reads none of
them, and `platform/Kconfig.posix` supplies hidden fallbacks so the shared file
still parses for POSIX menuconfig.

Host-side helper scripts for the multiplexed stream live in
[`esp/python/`](esp/python/README.md).
