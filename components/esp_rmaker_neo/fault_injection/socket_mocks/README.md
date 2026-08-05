# Socket Mocks

Test-only injection of socket failures: `connect` / `send` / `recv` fail on demand so firmware tests can assert what
the SDK does next (reconnect backoff, retry, state reporting). Built only when host control is enabled
(`CONFIG_RMNG_HOST_CTRL` on ESP-IDF, `-DRMNG_HOST_CTRL=ON` on POSIX) — see [`../README.md`](../README.md) for how this
fits with the rest of the fault injection code.

One entry point, [`include/socket_mocks.h`](include/socket_mocks.h):

```c
void socket_mock_force_failure(bool enable_fault);
```

While enabled, `connect` fails with `ETIMEDOUT` (unreachable server) and `send` / `recv` fail with `ECONNRESET`
(connection dropped). Passing `false` restores the real calls. The transport never sees a mock socket — the real
socket is created and kept open, only the three traffic calls are diverted, so the code under test fails the way it
would against a dead peer rather than against a bad file descriptor.

## Interposition strategy per platform

| Target | Symbols interposed | Mechanism | Source |
|---|---|---|---|
| ESP-IDF | `lwip_connect`, `lwip_send`, `lwip_recv` | `-Wl,--wrap` | [`src/lwip.c`](src/lwip.c) |
| Linux (GCC/Clang) | `connect`, `send`, `recv` | `-Wl,--wrap` | [`src/standard.c`](src/standard.c), `USE_LINKER_WRAP` |
| macOS | `connect`, `send`, `recv` | shadow definitions + `dlsym(RTLD_NEXT, …)` | [`src/standard.c`](src/standard.c) |
| Windows (MSVC) | — | unsupported; configure fails | — |

## The fault flag is global and unsynchronized

A single `volatile bool` per process, with no per-socket or per-fd granularity: every socket in the process fails while
the flag is set, including any that a test is not interested in. It is written from the host-control task and read from
whichever task is doing I/O, with no barrier beyond `volatile` — adequate for a coarse test toggle, not a model to copy
for anything finer-grained.

`lwip.c` and `standard.c` each carry their own copy of the flag; exactly one of the two compiles on a given platform.

## Limitations

- **MSVC is a hard error**, not a silent no-op — `message(FATAL_ERROR …)` at configure time.
- **The `dlsym` path is only wired up for Apple.** `_GNU_SOURCE` in `standard.c` is defined after the first include, so
  it has no effect; a glibc target needing `RTLD_NEXT` without `--wrap` would have to fix that first.

## License

Apache-2.0, same as the rest of the SDK — see [`LICENSE`](../../../../LICENSE).
