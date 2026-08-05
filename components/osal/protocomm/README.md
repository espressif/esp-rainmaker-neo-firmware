# osal/protocomm

POSIX-compatible `protocomm` vendored from ESP-IDF, with a CivetWeb-backed HTTP
server. Transports other than HTTP are stubbed as `OSAL_ERR_NOT_SUPPORTED`.

Builds only on POSIX, as its own static library (`protocomm-posix`): consumers of
the control plane (the local endpoints service in `esp_rmaker_neo`) link it explicitly, so
plain `osal` consumers don't pay for it. On ESP-IDF the real IDF `protocomm` is
used instead.

> Only one protocomm HTTP server instance can run per process, so the local
> endpoints service owns and refcounts it.

The vendored upstream sources are fetched at configure time into
[`vendor/`](vendor/README.md) and are not committed.
