# Fault Injection

Test-only fault injection for the SDK. Nothing here is a product feature — it exists so that firmware tests can make
things fail on demand and assert what the SDK does next (reconnect backoff, retry, state reporting). Everything in this
directory is built only when host control is enabled (`CONFIG_RMNG_HOST_CTRL` on ESP-IDF, `-DRMNG_HOST_CTRL=ON` on
POSIX); see [`../Kconfig`](../Kconfig). With host control off none of it reaches a production image.

| Directory | Injects | Docs |
|---|---|---|
| [`socket_mocks/`](socket_mocks/) | Failure of `connect` / `send` / `recv` at the socket layer | [README](socket_mocks/README.md) |

## License

Apache-2.0, same as the rest of the SDK — see [`LICENSE`](../../../LICENSE).
