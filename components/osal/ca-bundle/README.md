# osal/ca-bundle

Gives POSIX builds a root-certificate store equivalent to ESP-IDF's
`esp_crt_bundle`, so TLS clients on the host can verify a server without the
caller shipping its own CA file.

## Public API

[`include/osal_ca_bundle.h`](include/osal_ca_bundle.h) — a single accessor,
`osal_ca_bundle_get(&start, &end)`, returning the bounds of the embedded PEM
bundle (`end - start` bytes, null-terminated).

## Per-platform implementations

POSIX only: [`src/ca_bundle_posix.c`](src/ca_bundle_posix.c) returns the linker
symbols of a bundle embedded as binary data by CMake. There is no ESP-IDF
source and the header is not on the ESP-IDF include path — on ESP-IDF, callers
use `esp_crt_bundle_attach()` directly. The two in-tree consumers show the
split: `osal/http`'s libcurl backend and the POSIX coreMQTT network transport
call `osal_ca_bundle_get()`, while their ESP counterparts attach the IDF bundle.

## Build gating

Always built on POSIX (unconditionally added to the osal library); never built
on ESP-IDF.

## Notes

- The bundle is **downloaded at configure time** from ESP-IDF
  `release/v5.5` (`components/mbedtls/esp_crt_bundle/cacrt_all.pem`) and
  embedded into the osal library; it is not committed. Configuring needs
  network access unless the file is already cached.
- `CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE` (+
  `..._PATH`, relative to the project root) appends a custom PEM file to the
  default bundle. The path is required when the option is on and CMake fails
  hard if it is missing or does not exist. These symbols come from
  [`Kconfig`](Kconfig), which is registered for POSIX menuconfig only.
